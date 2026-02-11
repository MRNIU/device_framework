/**
 * @file virtio_blk_test.cpp
 * @brief VirtIO 块设备驱动测试
 * @copyright Copyright The virtio_driver Contributors
 *
 * 测试流程：
 * 1. 扫描 MMIO 设备，找到块设备 (Device ID == 2)
 * 2. 通过 VirtioBlk::Create() 一步完成初始化
 * 3. 读取设备配置空间（容量等）
 * 4. 同步写入/读取操作并验证数据一致性
 */

#include "virtio_driver/device/virtio_blk.hpp"

#include <cstdarg>
#include <cstdint>

#include "test.h"
#include "uart.h"
#include "virtio_driver/traits.hpp"

namespace {

/// @brief RISC-V 平台环境 Traits 实现
struct RiscvTraits {
  static auto Log(const char* fmt, ...) -> int {
    uart_puts("[BLK] ");
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

/**
 * @brief 静态 DMA 内存区域
 *
 * 在裸机环境中无法使用 malloc，使用静态缓冲区模拟 DMA 内存。
 * 需要页对齐以满足 DMA 要求。
 */
alignas(4096) uint8_t g_vq_dma_buf[32768];

/// 数据缓冲区（一个扇区大小）
alignas(16) uint8_t g_data_buf[virtio_driver::blk::kSectorSize];

/**
 * @brief 扫描 MMIO 设备，找到块设备的基地址
 * @return 块设备的 MMIO 基地址，未找到返回 0
 */
auto find_blk_device() -> uint64_t {
  for (int i = 0; i < kMaxDevices; ++i) {
    uint64_t base = kVirtioMmioBase + i * kVirtioMmioSize;
    auto magic = *reinterpret_cast<volatile uint32_t*>(base);
    if (magic != virtio_driver::kMmioMagicValue) {
      continue;
    }
    auto device_id = *reinterpret_cast<volatile uint32_t*>(
        base + virtio_driver::MmioTransport<>::MmioReg::kDeviceId);
    if (device_id == kBlockDeviceId) {
      return base;
    }
  }
  return 0;
}

/**
 * @brief 清零缓冲区
 */
void memzero(void* ptr, size_t len) {
  auto* p = static_cast<volatile uint8_t*>(ptr);
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

}  // namespace

void test_virtio_blk() {
  uart_puts("\n");
  uart_puts("╔════════════════════════════════════════╗\n");
  uart_puts("║   VirtIO Block Device Test            ║\n");
  uart_puts("╚════════════════════════════════════════╝\n");

  test_framework_init();

  // === 测试 1: 查找块设备 ===
  uint64_t blk_base = find_blk_device();
  EXPECT_TRUE(blk_base != 0, "Find VirtIO block device");
  if (blk_base == 0) {
    LOG("No block device found, skipping remaining tests");
    test_framework_print_summary();
    return;
  }
  LOG_HEX("Block device found at", blk_base);

  // === 测试 2: VirtioBlk::Create() 一步初始化 ===
  memzero(g_vq_dma_buf, sizeof(g_vq_dma_buf));

  using VirtioBlkType = virtio_driver::blk::VirtioBlk<RiscvTraits>;
  uint64_t extra_features =
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kSegMax) |
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kSizeMax) |
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kBlkSize) |
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kFlush) |
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kGeometry);
  auto blk_result =
      VirtioBlkType::Create(blk_base, g_vq_dma_buf, 1, 128, extra_features);
  EXPECT_TRUE(blk_result.has_value(), "VirtioBlk::Create() succeeds");
  if (!blk_result.has_value()) {
    LOG("VirtioBlk::Create() failed, skipping remaining tests");
    test_framework_print_summary();
    return;
  }
  auto& blk = *blk_result;

  // === 测试 3: 验证协商特性 ===
  {
    uint64_t features = blk.GetNegotiatedFeatures();
    bool has_version1 =
        (features &
         static_cast<uint64_t>(virtio_driver::ReservedFeature::kVersion1)) != 0;
    EXPECT_TRUE(has_version1, "Negotiated features include VERSION_1");
    LOG_HEX("Negotiated features", features);
  }

  // === 测试 4: 读取设备配置空间 ===
  {
    auto config = blk.ReadConfig();
    LOG_HEX("Device capacity (sectors)", config.capacity);
    LOG_HEX("Block size", config.blk_size);
    LOG_HEX("Size max", config.size_max);
    LOG_HEX("Seg max", config.seg_max);
    EXPECT_TRUE(config.capacity > 0, "Device capacity should be > 0");
  }

  // === 测试 5: GetCapacity() 快捷方法 ===
  {
    uint64_t capacity = blk.GetCapacity();
    EXPECT_TRUE(capacity > 0, "GetCapacity() should return > 0");
    LOG_HEX("Capacity via GetCapacity()", capacity);
  }

  // === 测试 6: 同步写入扇区 0 ===
  {
    for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
      g_data_buf[i] = static_cast<uint8_t>(0xAA + (i & 0x0F));
    }
    auto write_result = blk.Write(0, g_data_buf);
    EXPECT_TRUE(write_result.has_value(), "Write sector 0 succeeds");
  }

  // === 测试 7: 同步读取扇区 0 并验证数据 ===
  {
    memzero(g_data_buf, sizeof(g_data_buf));
    auto read_result = blk.Read(0, g_data_buf);
    EXPECT_TRUE(read_result.has_value(), "Read sector 0 succeeds");

    if (read_result.has_value()) {
      bool data_match = true;
      for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
        if (g_data_buf[i] != static_cast<uint8_t>(0xAA + (i & 0x0F))) {
          data_match = false;
          LOG_HEX("Data mismatch at byte", i);
          LOG_HEX("  Expected", static_cast<uint8_t>(0xAA + (i & 0x0F)));
          LOG_HEX("  Got", g_data_buf[i]);
          break;
        }
      }
      EXPECT_TRUE(data_match, "Read data matches written data");
    }
  }

  // === 测试 8: 注册 VirtIO 中断处理函数 ===
  {
    auto dev_idx =
        static_cast<uint32_t>((blk_base - kVirtioMmioBase) / kVirtioMmioSize);
    static decltype(&blk) s_blk_ptr = nullptr;
    s_blk_ptr = &blk;
    g_virtio_irq_handlers[dev_idx] = []() {
      if (s_blk_ptr != nullptr) {
        s_blk_ptr->HandleInterrupt();
      }
    };
    EXPECT_TRUE(g_virtio_irq_handlers[dev_idx] != nullptr,
                "VirtIO IRQ handler registered");
    LOG_HEX("Registered IRQ handler for device index", dev_idx);
  }

  // === 测试 9: 顺序写入多个扇区 (sectors 10-13) ===
  {
    LOG("Writing 4 sectors (10-13) with distinct patterns...");
    constexpr size_t kMultiSectorCount = 4;
    bool all_writes_ok = true;

    for (size_t s = 0; s < kMultiSectorCount; ++s) {
      uint64_t sector = 10 + s;
      uint8_t pattern = static_cast<uint8_t>(0x10 + s);
      for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
        g_data_buf[i] = static_cast<uint8_t>(pattern + (i & 0x0F));
      }
      auto result = blk.Write(sector, g_data_buf);
      if (!result.has_value()) {
        all_writes_ok = false;
        LOG_HEX("Write failed for sector", sector);
        break;
      }
    }
    EXPECT_TRUE(all_writes_ok, "Write 4 sectors (10-13) sequentially");
  }

  // === 测试 10: 顺序读回多个扇区并验证数据一致性 ===
  {
    LOG("Reading back 4 sectors (10-13) and verifying...");
    constexpr size_t kMultiSectorCount = 4;
    bool all_reads_ok = true;

    for (size_t s = 0; s < kMultiSectorCount; ++s) {
      uint64_t sector = 10 + s;
      uint8_t pattern = static_cast<uint8_t>(0x10 + s);

      memzero(g_data_buf, sizeof(g_data_buf));
      auto result = blk.Read(sector, g_data_buf);
      if (!result.has_value()) {
        all_reads_ok = false;
        LOG_HEX("Read failed for sector", sector);
        break;
      }

      for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
        uint8_t expected = static_cast<uint8_t>(pattern + (i & 0x0F));
        if (g_data_buf[i] != expected) {
          all_reads_ok = false;
          LOG_HEX("Data mismatch at sector", sector);
          LOG_HEX("  byte offset", i);
          LOG_HEX("  expected", expected);
          LOG_HEX("  got", g_data_buf[i]);
          break;
        }
      }
      if (!all_reads_ok) {
        break;
      }
    }
    EXPECT_TRUE(all_reads_ok, "Read-verify 4 sectors (10-13) sequentially");
  }

  // === 测试 11: 写入不连续扇区 (100, 200, 500, 1000) ===
  {
    LOG("Writing non-contiguous sectors (100, 200, 500, 1000)...");
    constexpr uint64_t sectors[] = {100, 200, 500, 1000};
    constexpr size_t num_sectors = sizeof(sectors) / sizeof(sectors[0]);
    bool all_ok = true;

    for (size_t s = 0; s < num_sectors; ++s) {
      uint8_t pattern = static_cast<uint8_t>(sectors[s] & 0xFF);
      for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
        g_data_buf[i] = static_cast<uint8_t>(pattern ^ (i & 0xFF));
      }
      auto result = blk.Write(sectors[s], g_data_buf);
      if (!result.has_value()) {
        all_ok = false;
        break;
      }
    }
    EXPECT_TRUE(all_ok, "Write non-contiguous sectors");

    // 读回验证
    LOG("Verifying non-contiguous sectors...");
    for (size_t s = 0; s < num_sectors && all_ok; ++s) {
      uint8_t pattern = static_cast<uint8_t>(sectors[s] & 0xFF);
      memzero(g_data_buf, sizeof(g_data_buf));

      auto result = blk.Read(sectors[s], g_data_buf);
      if (!result.has_value()) {
        all_ok = false;
        break;
      }

      for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
        uint8_t expected = static_cast<uint8_t>(pattern ^ (i & 0xFF));
        if (g_data_buf[i] != expected) {
          all_ok = false;
          LOG_HEX("Non-contiguous verify failed at sector", sectors[s]);
          break;
        }
      }
    }
    EXPECT_TRUE(all_ok, "Read-verify non-contiguous sectors");
  }

  // === 测试 12: 覆写已有数据并验证 ===
  {
    LOG("Overwrite sector 10 with new pattern and verify...");
    for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
      g_data_buf[i] = static_cast<uint8_t>(0xDD - (i & 0x0F));
    }

    auto write_result = blk.Write(10, g_data_buf);
    EXPECT_TRUE(write_result.has_value(), "Overwrite submit succeeds");

    // 读回验证新数据
    memzero(g_data_buf, sizeof(g_data_buf));
    auto read_result = blk.Read(10, g_data_buf);
    EXPECT_TRUE(read_result.has_value(), "Overwrite read-back succeeds");

    if (read_result.has_value()) {
      bool match = true;
      for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
        if (g_data_buf[i] != static_cast<uint8_t>(0xDD - (i & 0x0F))) {
          match = false;
          LOG_HEX("Overwrite verify mismatch at byte", i);
          break;
        }
      }
      EXPECT_TRUE(match, "Overwrite data matches new pattern");
    }
  }

  // 清理：注销中断处理函数
  {
    auto dev_idx =
        static_cast<uint32_t>((blk_base - kVirtioMmioBase) / kVirtioMmioSize);
    g_virtio_irq_handlers[dev_idx] = nullptr;
  }

  // 打印测试摘要
  test_framework_print_summary();
}
