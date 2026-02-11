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

#include "virtio_driver/device/virtio_blk.hpp"

#include <cstdarg>
#include <cstdint>

#include "test.h"
#include "uart.h"
#include "virtio_driver/transport/mmio.hpp"

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

/**
 * @brief 静态 DMA 内存区域
 *
 * 在裸机环境中无法使用 malloc，使用静态缓冲区模拟 DMA 内存。
 * 需要页对齐以满足 DMA 要求。
 */
alignas(4096) static uint8_t g_vq_dma_buf[32768];

/// 请求头缓冲区（DMA 可访问）
alignas(16) static virtio_driver::blk::BlkReqHeader g_req_header;

/// 数据缓冲区（一个扇区大小）
alignas(16) static uint8_t g_data_buf[virtio_driver::blk::kSectorSize];

/// 多扇区测试用的较大数据缓冲区
constexpr size_t kMultiSectorCount = 4;
alignas(16) static uint8_t
    g_multi_data_buf[virtio_driver::blk::kSectorSize * kMultiSectorCount];

/// 状态字节
alignas(16) static uint8_t g_status;

/// 平台操作接口（裸机实现：恒等映射，fence 屏障）
virtio_driver::PlatformOps g_platform_ops = {
    .virt_to_phys = [](void* vaddr) -> uint64_t {
      return reinterpret_cast<uint64_t>(vaddr);
    }};

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
  memzero(g_vq_dma_buf, sizeof(g_vq_dma_buf));
  uint64_t vq_phys = reinterpret_cast<uint64_t>(g_vq_dma_buf);

  virtio_driver::SplitVirtqueue vq(g_vq_dma_buf, vq_phys, kQueueSize, false);
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
    bool has_version1 =
        (features &
         static_cast<uint64_t>(virtio_driver::ReservedFeature::kVersion1)) != 0;
    EXPECT_TRUE(has_version1, "Negotiated features include VERSION_1");
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

  // === 测试 11: 注册 VirtIO 中断处理函数 ===
  // 通过 MMIO 设备地址计算设备索引，注册中断回调
  {
    uint32_t dev_idx =
        static_cast<uint32_t>((blk_base - kVirtioMmioBase) / kVirtioMmioSize);
    // 使用 lambda 包装 blk.AckInterrupt()
    // 由于 lambda 不捕获，可以转换为函数指针
    // 但 AckInterrupt 需要 blk 引用，所以用静态指针中转
    static decltype(&blk) s_blk_ptr = nullptr;
    s_blk_ptr = &blk;
    g_virtio_irq_handlers[dev_idx] = []() {
      if (s_blk_ptr != nullptr) {
        s_blk_ptr->AckInterrupt();
      }
    };
    EXPECT_TRUE(g_virtio_irq_handlers[dev_idx] != nullptr,
                "VirtIO IRQ handler registered");
    LOG_HEX("Registered IRQ handler for device index", dev_idx);
  }

  // === 测试 12: 顺序写入多个扇区 (sectors 10-13) ===
  {
    LOG("Writing 4 sectors (10-13) with distinct patterns...");
    bool all_writes_ok = true;

    for (size_t s = 0; s < kMultiSectorCount; ++s) {
      uint64_t sector = 10 + s;
      // 每个扇区用不同的模式填充：sector 10 -> 0x10, sector 11 -> 0x11, ...
      uint8_t pattern = static_cast<uint8_t>(0x10 + s);
      for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
        g_data_buf[i] = static_cast<uint8_t>(pattern + (i & 0x0F));
      }
      g_status = 0xFF;

      auto write_result =
          blk.Write(sector, g_data_buf, &g_status, &g_req_header);
      if (!write_result.has_value()) {
        all_writes_ok = false;
        LOG_HEX("Write submit failed for sector", sector);
        break;
      }

      // 忙等待完成
      uint32_t timeout_count = 0;
      constexpr uint32_t kMaxWait = 1000000;
      while (!vq.HasUsed() && timeout_count < kMaxWait) {
        busy_wait(100);
        ++timeout_count;
      }

      if (!vq.HasUsed()) {
        all_writes_ok = false;
        LOG_HEX("Write timeout for sector", sector);
        break;
      }

      uint32_t processed = blk.ProcessUsed();
      if (processed != 1 ||
          g_status !=
              static_cast<uint8_t>(virtio_driver::blk::BlkStatus::kOk)) {
        all_writes_ok = false;
        LOG_HEX("Write failed for sector", sector);
        LOG_HEX("  status", g_status);
        break;
      }
    }
    EXPECT_TRUE(all_writes_ok, "Write 4 sectors (10-13) sequentially");
  }

  // === 测试 13: 顺序读回多个扇区并验证数据一致性 ===
  {
    LOG("Reading back 4 sectors (10-13) and verifying...");
    bool all_reads_ok = true;

    for (size_t s = 0; s < kMultiSectorCount; ++s) {
      uint64_t sector = 10 + s;
      uint8_t pattern = static_cast<uint8_t>(0x10 + s);

      memzero(g_data_buf, sizeof(g_data_buf));
      g_status = 0xFF;

      auto read_result = blk.Read(sector, g_data_buf, &g_status, &g_req_header);
      if (!read_result.has_value()) {
        all_reads_ok = false;
        LOG_HEX("Read submit failed for sector", sector);
        break;
      }

      uint32_t timeout_count = 0;
      constexpr uint32_t kMaxWait = 1000000;
      while (!vq.HasUsed() && timeout_count < kMaxWait) {
        busy_wait(100);
        ++timeout_count;
      }

      if (!vq.HasUsed()) {
        all_reads_ok = false;
        LOG_HEX("Read timeout for sector", sector);
        break;
      }

      uint32_t processed = blk.ProcessUsed();
      if (processed != 1 ||
          g_status !=
              static_cast<uint8_t>(virtio_driver::blk::BlkStatus::kOk)) {
        all_reads_ok = false;
        LOG_HEX("Read failed for sector", sector);
        break;
      }

      // 验证数据
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

  // === 测试 14: 写入不连续扇区 (100, 200, 500, 1000) ===
  {
    LOG("Writing non-contiguous sectors (100, 200, 500, 1000)...");
    constexpr uint64_t sectors[] = {100, 200, 500, 1000};
    constexpr size_t num_sectors = sizeof(sectors) / sizeof(sectors[0]);
    bool all_ok = true;

    for (size_t s = 0; s < num_sectors; ++s) {
      // 填充：以扇区号低字节为基础模式
      uint8_t pattern = static_cast<uint8_t>(sectors[s] & 0xFF);
      for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
        g_data_buf[i] = static_cast<uint8_t>(pattern ^ (i & 0xFF));
      }
      g_status = 0xFF;

      auto result = blk.Write(sectors[s], g_data_buf, &g_status, &g_req_header);
      if (!result.has_value()) {
        all_ok = false;
        break;
      }

      uint32_t timeout_count = 0;
      constexpr uint32_t kMaxWait = 1000000;
      while (!vq.HasUsed() && timeout_count < kMaxWait) {
        busy_wait(100);
        ++timeout_count;
      }

      if (!vq.HasUsed()) {
        all_ok = false;
        break;
      }

      blk.ProcessUsed();
      if (g_status !=
          static_cast<uint8_t>(virtio_driver::blk::BlkStatus::kOk)) {
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
      g_status = 0xFF;

      auto result = blk.Read(sectors[s], g_data_buf, &g_status, &g_req_header);
      if (!result.has_value()) {
        all_ok = false;
        break;
      }

      uint32_t timeout_count = 0;
      constexpr uint32_t kMaxWait = 1000000;
      while (!vq.HasUsed() && timeout_count < kMaxWait) {
        busy_wait(100);
        ++timeout_count;
      }

      if (!vq.HasUsed()) {
        all_ok = false;
        break;
      }

      blk.ProcessUsed();
      if (g_status !=
          static_cast<uint8_t>(virtio_driver::blk::BlkStatus::kOk)) {
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

  // === 测试 15: 覆写已有数据并验证 ===
  {
    LOG("Overwrite sector 10 with new pattern and verify...");
    // 先写入新模式
    for (size_t i = 0; i < virtio_driver::blk::kSectorSize; ++i) {
      g_data_buf[i] = static_cast<uint8_t>(0xDD - (i & 0x0F));
    }
    g_status = 0xFF;

    auto write_result = blk.Write(10, g_data_buf, &g_status, &g_req_header);
    EXPECT_TRUE(write_result.has_value(), "Overwrite submit succeeds");

    if (write_result.has_value()) {
      uint32_t timeout_count = 0;
      constexpr uint32_t kMaxWait = 1000000;
      while (!vq.HasUsed() && timeout_count < kMaxWait) {
        busy_wait(100);
        ++timeout_count;
      }
      EXPECT_TRUE(vq.HasUsed(), "Overwrite completed");
      if (vq.HasUsed()) {
        blk.ProcessUsed();
        EXPECT_EQ(static_cast<uint8_t>(virtio_driver::blk::BlkStatus::kOk),
                  g_status, "Overwrite status OK");
      }
    }

    // 读回验证新数据
    memzero(g_data_buf, sizeof(g_data_buf));
    g_status = 0xFF;

    auto read_result = blk.Read(10, g_data_buf, &g_status, &g_req_header);
    EXPECT_TRUE(read_result.has_value(), "Overwrite read-back submit succeeds");

    if (read_result.has_value()) {
      uint32_t timeout_count = 0;
      constexpr uint32_t kMaxWait = 1000000;
      while (!vq.HasUsed() && timeout_count < kMaxWait) {
        busy_wait(100);
        ++timeout_count;
      }
      EXPECT_TRUE(vq.HasUsed(), "Overwrite read-back completed");
      if (vq.HasUsed()) {
        blk.ProcessUsed();
        EXPECT_EQ(static_cast<uint8_t>(virtio_driver::blk::BlkStatus::kOk),
                  g_status, "Overwrite read-back status OK");

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
  }

  // 清理：注销中断处理函数
  {
    uint32_t dev_idx =
        static_cast<uint32_t>((blk_base - kVirtioMmioBase) / kVirtioMmioSize);
    g_virtio_irq_handlers[dev_idx] = nullptr;
  }

  // 打印测试摘要
  test_framework_print_summary();
}
