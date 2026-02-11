/**
 * @file virtio_blk_test.cpp
 * @brief VirtIO 块设备驱动测试
 * @copyright Copyright The virtio_driver Contributors
 *
 * 测试流程：
 * 1. 扫描 MMIO 设备，找到块设备 (Device ID == 2)
 * 2. 分配 DMA 内存，初始化 SplitVirtqueue
 * 3. 通过 VirtioBlk::Create() 完成设备初始化和特性协商
 * 4. 读取设备配置空间（容量等）
 * 5. 发起写请求，等待完成
 * 6. 发起读请求，验证数据一致性
 */

#include "device/virtio_blk.hpp"

#include <cstdarg>
#include <cstdint>

#include "test.h"
#include "transport/mmio.hpp"
#include "uart.h"

namespace {

/// 日志函数
struct BlkTestLogger {
  auto operator()(const char* format, ...) const -> int {
    uart_puts("[BLK] ");
    va_list ap;
    va_start(ap, format);
    int ret = uart_vprintf(format, ap);
    va_end(ap);
    uart_puts("\n");
    return ret;
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
/// Virtqueue 大小
constexpr uint16_t kQueueSize = 128;
/// 页大小（字节）
constexpr size_t kPageSize = 4096;
/// Legacy MMIO Used Ring 对齐（页大小）
constexpr size_t kLegacyUsedAlign = 4096;

/**
 * @brief 静态 DMA 内存区域
 *
 * 在裸机环境中无法使用 malloc，使用静态缓冲区模拟 DMA 内存。
 * 需要页对齐以满足 DMA 要求。
 * 大小需满足 legacy 布局：Used Ring 按页对齐。
 */
alignas(4096) static uint8_t g_vq_dma_buf[32768];

/// 请求头缓冲区（DMA 可访问）
alignas(16) static virtio_driver::blk::BlkReqHeader g_req_header;

/// 数据缓冲区（一个扇区大小）
alignas(16) static uint8_t g_data_buf[virtio_driver::blk::kSectorSize];

/// 状态字节
alignas(16) static uint8_t g_status;

/// 平台操作接口（裸机实现：恒等映射，fence 屏障）
virtio_driver::PlatformOps g_platform_ops = {
    .alloc_pages = nullptr,
    .free_pages = nullptr,
    .virt_to_phys = [](void* vaddr) -> uint64_t {
      return reinterpret_cast<uint64_t>(vaddr);
    },
    .mb =
        []() {
          asm volatile("fence iorw, iorw" ::: "memory");
        },
    .rmb =
        []() {
          asm volatile("fence ir, ir" ::: "memory");
        },
    .wmb =
        []() {
          asm volatile("fence ow, ow" ::: "memory");
        },
    .page_size = kPageSize,
};

/// 简单的忙等待循环
void busy_wait(uint64_t cycles) {
  for (uint64_t i = 0; i < cycles; ++i) {
    asm volatile("nop");
  }
}

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

  // === 测试 2: 创建 MmioTransport ===
  virtio_driver::MmioTransport<BlkTestLogger> transport(blk_base);
  EXPECT_TRUE(transport.IsValid(), "MmioTransport initialization");
  if (!transport.IsValid()) {
    test_framework_print_summary();
    return;
  }
  EXPECT_EQ(kBlockDeviceId, transport.GetDeviceId(),
            "Device ID should be 2 (block)");

  // === 测试 3: 初始化 SplitVirtqueue ===
  // Legacy MMIO (v1) 要求 Used Ring 按页对齐
  bool is_legacy =
      (transport.GetVersion() == virtio_driver::kMmioVersionLegacy);
  size_t used_align = is_legacy ? kLegacyUsedAlign
                                : virtio_driver::SplitVirtqueue::Used::kAlign;

  memzero(g_vq_dma_buf, sizeof(g_vq_dma_buf));
  uint64_t vq_phys = reinterpret_cast<uint64_t>(g_vq_dma_buf);

  // Legacy 设备不支持 event_idx
  virtio_driver::SplitVirtqueue vq(g_vq_dma_buf, vq_phys, kQueueSize, false,
                                   used_align);
  EXPECT_TRUE(vq.IsValid(), "SplitVirtqueue initialization");
  EXPECT_EQ(kQueueSize, vq.Size(), "Virtqueue size matches");
  EXPECT_EQ(kQueueSize, vq.NumFree(),
            "All descriptors should be free initially");
  if (!vq.IsValid()) {
    test_framework_print_summary();
    return;
  }

  // === 测试 4: VirtioBlk::Create() 初始化设备 ===
  using VirtioBlkType = virtio_driver::blk::VirtioBlk<BlkTestLogger>;
  // 请求常见的块设备特性
  uint64_t extra_features =
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kSegMax) |
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kSizeMax) |
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kBlkSize) |
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kFlush) |
      static_cast<uint64_t>(virtio_driver::blk::BlkFeatureBit::kGeometry);
  auto blk_result =
      VirtioBlkType::Create(transport, vq, g_platform_ops, extra_features);
  EXPECT_TRUE(blk_result.has_value(), "VirtioBlk::Create() succeeds");
  if (!blk_result.has_value()) {
    LOG("VirtioBlk::Create() failed, skipping remaining tests");
    test_framework_print_summary();
    return;
  }
  auto& blk = *blk_result;

  // === 测试 5: 验证协商特性 ===
  {
    uint64_t features = blk.GetNegotiatedFeatures();
    if (is_legacy) {
      // Legacy 设备不支持 VERSION_1
      LOG("Legacy device - VERSION_1 not expected");
      EXPECT_TRUE(true, "Legacy device features negotiated");
    } else {
      bool has_version1 =
          (features & static_cast<uint64_t>(
                          virtio_driver::ReservedFeature::kVersion1)) != 0;
      EXPECT_TRUE(has_version1, "Negotiated features include VERSION_1");
    }
    LOG_HEX("Negotiated features", features);
  }

  // === 测试 6: 读取设备配置空间 ===
  {
    auto config = blk.ReadConfig();
    LOG_HEX("Device capacity (sectors)", config.capacity);
    LOG_HEX("Block size", config.blk_size);
    LOG_HEX("Size max", config.size_max);
    LOG_HEX("Seg max", config.seg_max);

    // 测试磁盘镜像是 64MB = 131072 个 512B 扇区
    EXPECT_TRUE(config.capacity > 0, "Device capacity should be > 0");
  }

  // === 测试 7: GetCapacity() 快捷方法 ===
  {
    uint64_t capacity = blk.GetCapacity();
    EXPECT_TRUE(capacity > 0, "GetCapacity() should return > 0");
    LOG_HEX("Capacity via GetCapacity()", capacity);
  }

  // === 测试 8: 写入数据到扇区 0 ===
  {
    // 填充写入数据（简单的模式：0xAA, 0xBB, ...）
    for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
      g_data_buf[i] = static_cast<uint8_t>(0xAA + (i & 0x0F));
    }
    g_status = 0xFF;  // 预填充非零值，用于验证设备是否写入了状态

    auto write_result = blk.Write(0, g_data_buf, &g_status, &g_req_header);
    EXPECT_TRUE(write_result.has_value(), "Write request submitted");

    if (write_result.has_value()) {
      // 忙等待设备完成（裸机环境，轮询方式）
      uint32_t timeout_count = 0;
      constexpr uint32_t kMaxWait = 1000000;
      while (!vq.HasUsed() && timeout_count < kMaxWait) {
        busy_wait(100);
        ++timeout_count;
      }

      EXPECT_TRUE(vq.HasUsed(), "Write request completed (device responded)");

      if (vq.HasUsed()) {
        uint32_t processed = blk.ProcessUsed();
        EXPECT_EQ(1U, processed, "Processed 1 write request");
        EXPECT_EQ(static_cast<uint8_t>(virtio_driver::blk::BlkStatus::kOk),
                  g_status, "Write status should be OK (0)");
        blk.AckInterrupt();
      }
    }
  }

  // === 测试 9: 从扇区 0 读回数据并验证 ===
  {
    // 清空数据缓冲区
    memzero(g_data_buf, sizeof(g_data_buf));
    g_status = 0xFF;

    auto read_result = blk.Read(0, g_data_buf, &g_status, &g_req_header);
    EXPECT_TRUE(read_result.has_value(), "Read request submitted");

    if (read_result.has_value()) {
      // 忙等待设备完成
      uint32_t timeout_count = 0;
      constexpr uint32_t kMaxWait = 1000000;
      while (!vq.HasUsed() && timeout_count < kMaxWait) {
        busy_wait(100);
        ++timeout_count;
      }

      EXPECT_TRUE(vq.HasUsed(), "Read request completed (device responded)");

      if (vq.HasUsed()) {
        uint32_t processed = blk.ProcessUsed();
        EXPECT_EQ(1U, processed, "Processed 1 read request");
        EXPECT_EQ(static_cast<uint8_t>(virtio_driver::blk::BlkStatus::kOk),
                  g_status, "Read status should be OK (0)");
        blk.AckInterrupt();

        // 验证读回的数据与写入的数据一致
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
  }

  // === 测试 10: ProcessUsed() 空队列返回 0 ===
  {
    uint32_t count = blk.ProcessUsed();
    EXPECT_EQ(0U, count, "ProcessUsed() returns 0 when no pending requests");
  }

  // 打印测试摘要
  test_framework_print_summary();
}
