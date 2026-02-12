/**
 * @file virtio_blk_device_test.cpp
 * @brief VirtIO 块设备统一接口测试
 * @copyright Copyright The device_framework Contributors
 *
 * 测试 VirtIO 块设备通过统一 BlockDevice 接口的操作：
 * Open/ReadBlock/WriteBlock/ReadBlocks/WriteBlocks/Read/Write/Release 及错误路径
 */

#include "device_framework/driver/virtio/device/virtio_blk_device.hpp"

#include <cstdarg>
#include <cstdint>

#include "device_framework/driver/virtio/traits.hpp"
#include "test.h"
#include "uart.h"

namespace {

/// @brief RISC-V 平台环境 Traits 实现
struct RiscvTraits {
  static auto Log(const char* fmt, ...) -> int {
    uart_puts("[BLK-DEV] ");
    va_list ap;
    va_start(ap, fmt);
    int ret = uart_vprintf(fmt, ap);
    va_end(ap);
    uart_puts("\n");
    return ret;
  }
  static auto Mb() -> void { asm volatile("fence iorw, iorw" ::: "memory"); }
  static auto Rmb() -> void { asm volatile("fence ir, ir" ::: "memory"); }
  static auto Wmb() -> void { asm volatile("fence ow, ow" ::: "memory"); }
  static auto VirtToPhys(void* p) -> uintptr_t {
    return reinterpret_cast<uintptr_t>(p);
  }
  static auto PhysToVirt(uintptr_t a) -> void* {
    return reinterpret_cast<void*>(a);
  }
};

/// QEMU virt 机器上的 VirtIO MMIO 设备起始地址
constexpr uint64_t kVirtioMmioBase = 0x10001000;
/// 每个 VirtIO MMIO 设备之间的地址间隔
constexpr uint64_t kVirtioMmioSize = 0x1000;
/// 扫描的最大设备数量
constexpr int kMaxDevices = 8;
/// 块设备的 Device ID
constexpr uint32_t kBlockDeviceId = 2;

/// 静态 DMA 内存区域
alignas(4096) uint8_t g_dev_vq_dma_buf[32768];

/// 数据缓冲区
alignas(16) uint8_t g_dev_data_buf[device_framework::virtio::blk::kSectorSize];

/// 多扇区缓冲区
alignas(16) uint8_t
    g_dev_multi_buf[4 * device_framework::virtio::blk::kSectorSize];

/**
 * @brief 扫描 MMIO 设备，找到块设备的基地址
 */
auto find_blk_device_for_dev_test() -> uint64_t {
  for (int i = 0; i < kMaxDevices; ++i) {
    uint64_t base = kVirtioMmioBase + i * kVirtioMmioSize;
    auto magic = *reinterpret_cast<volatile uint32_t*>(base);
    if (magic != device_framework::virtio::kMmioMagicValue) {
      continue;
    }
    auto device_id = *reinterpret_cast<volatile uint32_t*>(
        base + device_framework::virtio::MmioTransport<>::MmioReg::kDeviceId);
    if (device_id == kBlockDeviceId) {
      return base;
    }
  }
  return 0;
}

/**
 * @brief 清零缓冲区
 */
void dev_memzero(void* ptr, size_t len) {
  auto* p = static_cast<volatile uint8_t*>(ptr);
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

}  // namespace

void test_virtio_blk_device() {
  uart_puts("\n");
  uart_puts("╔════════════════════════════════════════╗\n");
  uart_puts("║   VirtIO BlockDevice Interface Test    ║\n");
  uart_puts("╚════════════════════════════════════════╝\n");

  test_framework_init();

  // === 测试 1: 查找块设备 ===
  uint64_t blk_base = find_blk_device_for_dev_test();
  EXPECT_TRUE(blk_base != 0, "Find VirtIO block device");
  if (blk_base == 0) {
    LOG("No block device found, skipping remaining tests");
    test_framework_print_summary();
    return;
  }

  // === 测试 2: VirtioBlkDevice::Create() ===
  dev_memzero(g_dev_vq_dma_buf, sizeof(g_dev_vq_dma_buf));

  using DeviceType =
      device_framework::virtio::blk::VirtioBlkDevice<RiscvTraits>;
  auto dev_result = DeviceType::Create(blk_base, g_dev_vq_dma_buf);
  EXPECT_TRUE(dev_result.has_value(), "VirtioBlkDevice::Create() succeeds");
  if (!dev_result.has_value()) {
    LOG("VirtioBlkDevice::Create() failed, skipping remaining tests");
    test_framework_print_summary();
    return;
  }
  auto& dev = *dev_result;

  // === 测试 3: 未打开时操作应失败 ===
  {
    auto read_result = dev.ReadBlock(
        0, std::span<uint8_t>(g_dev_data_buf, sizeof(g_dev_data_buf)));
    EXPECT_FALSE(read_result.has_value(),
                 "ReadBlock before Open fails (not open)");
    if (!read_result.has_value()) {
      EXPECT_EQ(
          static_cast<uint32_t>(device_framework::ErrorCode::kDeviceNotOpen),
          static_cast<uint32_t>(read_result.error().code),
          "Error code is kDeviceNotOpen");
    }
  }

  // === 测试 4: OpenReadWrite ===
  {
    auto open_result = dev.OpenReadWrite();
    EXPECT_TRUE(open_result.has_value(), "OpenReadWrite() succeeds");
  }

  // === 测试 5: 重复 Open 应失败 ===
  {
    auto second_open = dev.OpenReadWrite();
    EXPECT_FALSE(second_open.has_value(),
                 "Second OpenReadWrite() fails (already open)");
    if (!second_open.has_value()) {
      EXPECT_EQ(
          static_cast<uint32_t>(
              device_framework::ErrorCode::kDeviceAlreadyOpen),
          static_cast<uint32_t>(second_open.error().code),
          "Error code is kDeviceAlreadyOpen");
    }
  }

  // === 测试 6: GetBlockSize / GetBlockCount / GetCapacity ===
  {
    size_t block_size = dev.GetBlockSize();
    EXPECT_EQ(static_cast<uint64_t>(512), static_cast<uint64_t>(block_size),
              "GetBlockSize() returns 512");

    uint64_t block_count = dev.GetBlockCount();
    EXPECT_TRUE(block_count > 0, "GetBlockCount() > 0");
    LOG_HEX("Block count", block_count);

    uint64_t capacity = dev.GetCapacity();
    EXPECT_EQ(block_size * block_count, capacity,
              "GetCapacity() == BlockSize * BlockCount");
    LOG_HEX("Capacity (bytes)", capacity);
  }

  // === 测试 7: WriteBlock 写入单个块（扇区 80） ===
  {
    for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
      g_dev_data_buf[i] = static_cast<uint8_t>(0xBB + (i & 0x0F));
    }

    auto write_result = dev.WriteBlock(
        80, std::span<const uint8_t>(g_dev_data_buf, sizeof(g_dev_data_buf)));
    EXPECT_TRUE(write_result.has_value(), "WriteBlock(80) succeeds");
  }

  // === 测试 8: ReadBlock 读取单个块并验证 ===
  {
    dev_memzero(g_dev_data_buf, sizeof(g_dev_data_buf));

    auto read_result = dev.ReadBlock(
        80, std::span<uint8_t>(g_dev_data_buf, sizeof(g_dev_data_buf)));
    EXPECT_TRUE(read_result.has_value(), "ReadBlock(80) succeeds");

    if (read_result.has_value()) {
      bool match = true;
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        if (g_dev_data_buf[i] != static_cast<uint8_t>(0xBB + (i & 0x0F))) {
          match = false;
          LOG_HEX("Data mismatch at byte", i);
          break;
        }
      }
      EXPECT_TRUE(match, "ReadBlock data matches WriteBlock data");
    }
  }

  // === 测试 9: WriteBlocks / ReadBlocks 多块操作 ===
  {
    constexpr size_t kNumBlocks = 4;
    constexpr uint64_t kBaseBlock = 90;

    // 填充 4 个扇区的不同数据
    for (size_t s = 0; s < kNumBlocks; ++s) {
      auto pattern = static_cast<uint8_t>(0xC0 + s);
      auto* buf =
          g_dev_multi_buf + s * device_framework::virtio::blk::kSectorSize;
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        buf[i] = static_cast<uint8_t>(pattern + (i & 0xFF));
      }
    }

    auto write_result =
        dev.WriteBlocks(kBaseBlock,
                        std::span<const uint8_t>(g_dev_multi_buf,
                                                 sizeof(g_dev_multi_buf)),
                        kNumBlocks);
    EXPECT_TRUE(write_result.has_value(), "WriteBlocks(4 blocks) succeeds");
    if (write_result.has_value()) {
      EXPECT_EQ(static_cast<uint64_t>(kNumBlocks),
                static_cast<uint64_t>(*write_result),
                "WriteBlocks returned correct block count");
    }

    // 读回验证
    dev_memzero(g_dev_multi_buf, sizeof(g_dev_multi_buf));
    auto read_result = dev.ReadBlocks(
        kBaseBlock,
        std::span<uint8_t>(g_dev_multi_buf, sizeof(g_dev_multi_buf)),
        kNumBlocks);
    EXPECT_TRUE(read_result.has_value(), "ReadBlocks(4 blocks) succeeds");

    if (read_result.has_value()) {
      bool all_match = true;
      for (size_t s = 0; s < kNumBlocks && all_match; ++s) {
        auto pattern = static_cast<uint8_t>(0xC0 + s);
        auto* buf =
            g_dev_multi_buf + s * device_framework::virtio::blk::kSectorSize;
        for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize;
             ++i) {
          auto expected = static_cast<uint8_t>(pattern + (i & 0xFF));
          if (buf[i] != expected) {
            all_match = false;
            LOG_HEX("Multi-block mismatch at block", s);
            LOG_HEX("  byte", i);
            break;
          }
        }
      }
      EXPECT_TRUE(all_match, "ReadBlocks data matches WriteBlocks data");
    }
  }

  // === 测试 10: 字节级 Write(offset) → 块桥接 ===
  {
    // offset = 512 * 95 = 扇区 95
    constexpr size_t kOffset = 95 * device_framework::virtio::blk::kSectorSize;

    for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
      g_dev_data_buf[i] = static_cast<uint8_t>(0xDD + (i & 0x0F));
    }

    auto write_result = dev.Write(
        std::span<const uint8_t>(g_dev_data_buf, sizeof(g_dev_data_buf)),
        kOffset);
    EXPECT_TRUE(write_result.has_value(),
                "Write(offset=sector95) byte-level succeeds");
    if (write_result.has_value()) {
      EXPECT_EQ(static_cast<uint64_t>(sizeof(g_dev_data_buf)),
                static_cast<uint64_t>(*write_result),
                "Write returned correct byte count");
    }
  }

  // === 测试 11: 字节级 Read(offset) → 块桥接 ===
  {
    constexpr size_t kOffset = 95 * device_framework::virtio::blk::kSectorSize;

    dev_memzero(g_dev_data_buf, sizeof(g_dev_data_buf));

    auto read_result = dev.Read(
        std::span<uint8_t>(g_dev_data_buf, sizeof(g_dev_data_buf)), kOffset);
    EXPECT_TRUE(read_result.has_value(),
                "Read(offset=sector95) byte-level succeeds");

    if (read_result.has_value()) {
      bool match = true;
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        if (g_dev_data_buf[i] != static_cast<uint8_t>(0xDD + (i & 0x0F))) {
          match = false;
          LOG_HEX("Byte-level read mismatch at byte", i);
          break;
        }
      }
      EXPECT_TRUE(match, "Byte-level Read data matches Write data");
    }
  }

  // === 测试 12: 非对齐偏移的字节级 Read 应失败 ===
  {
    auto read_result =
        dev.Read(std::span<uint8_t>(g_dev_data_buf, sizeof(g_dev_data_buf)),
                 100);  // 非 512 对齐
    EXPECT_FALSE(read_result.has_value(),
                 "Read(offset=100 non-aligned) fails");
    if (!read_result.has_value()) {
      EXPECT_EQ(
          static_cast<uint32_t>(
              device_framework::ErrorCode::kDeviceBlockUnaligned),
          static_cast<uint32_t>(read_result.error().code),
          "Error code is kDeviceBlockUnaligned");
    }
  }

  // === 测试 13: Release ===
  {
    auto release_result = dev.Release();
    EXPECT_TRUE(release_result.has_value(), "Release() succeeds");
  }

  // === 测试 14: Release 后操作应失败 ===
  {
    auto read_result = dev.ReadBlock(
        0, std::span<uint8_t>(g_dev_data_buf, sizeof(g_dev_data_buf)));
    EXPECT_FALSE(read_result.has_value(),
                 "ReadBlock after Release fails (not open)");
  }

  // === 测试 15: Release 后重新 Open 应成功 ===
  {
    auto reopen = dev.OpenReadWrite();
    EXPECT_TRUE(reopen.has_value(), "Re-open after Release() succeeds");
    if (reopen.has_value()) {
      (void)dev.Release();
    }
  }

  test_framework_print_summary();
}
