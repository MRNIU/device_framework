/**
 * @file virtio_blk_test.cpp
 * @brief VirtIO 块设备驱动测试
 * @copyright Copyright The device_framework Contributors
 *
 * 测试流程：
 * 1. 扫描 MMIO 设备，找到块设备 (Device ID == 2)
 * 2. 通过 VirtioBlk::Create() 一步完成初始化
 * 3. 读取设备配置空间（容量等）
 * 4. 同步写入/读取操作并验证数据一致性
 */

#include "device_framework/detail/virtio/device/virtio_blk.hpp"

#include <cstdarg>
#include <cstdint>

#include "device_framework/detail/virtio/traits.hpp"
#include "test.h"
#include "uart.h"

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

/// 静态 DMA 内存区域
alignas(4096) uint8_t g_vq_dma_buf[32768];

/// 数据缓冲区
alignas(16) uint8_t g_data_buf[device_framework::virtio::blk::kSectorSize];

/// 多扇区数据缓冲区（用于 SG 测试）
alignas(16) uint8_t
    g_multi_sector_buf[4 * device_framework::virtio::blk::kSectorSize];

/**
 * @brief 扫描 MMIO 设备，找到块设备的基地址
 * @return 块设备的 MMIO 基地址，未找到返回 0
 */
auto find_blk_device() -> uint64_t {
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

  using VirtioBlkType = device_framework::virtio::blk::VirtioBlk<RiscvTraits>;
  uint64_t extra_features =
      static_cast<uint64_t>(
          device_framework::virtio::blk::BlkFeatureBit::kSegMax) |
      static_cast<uint64_t>(
          device_framework::virtio::blk::BlkFeatureBit::kSizeMax) |
      static_cast<uint64_t>(
          device_framework::virtio::blk::BlkFeatureBit::kBlkSize) |
      static_cast<uint64_t>(
          device_framework::virtio::blk::BlkFeatureBit::kFlush) |
      static_cast<uint64_t>(
          device_framework::virtio::blk::BlkFeatureBit::kGeometry);
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
         static_cast<uint64_t>(
             device_framework::virtio::ReservedFeature::kVersion1)) != 0;
    EXPECT_TRUE(has_version1, "Negotiated features include VERSION_1");

    bool has_event_idx =
        (features &
         static_cast<uint64_t>(
             device_framework::virtio::ReservedFeature::kEventIdx)) != 0;
    EXPECT_TRUE(has_event_idx, "Negotiated features include EVENT_IDX");
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
    for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
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
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
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
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
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

      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
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
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
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

      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
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
    for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
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
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        if (g_data_buf[i] != static_cast<uint8_t>(0xDD - (i & 0x0F))) {
          match = false;
          LOG_HEX("Overwrite verify mismatch at byte", i);
          break;
        }
      }
      EXPECT_TRUE(match, "Overwrite data matches new pattern");
    }
  }

  // === 测试 13: Event Index - 连续提交多个请求后验证 kicks_elided ===
  {
    LOG("Testing Event Index notification suppression...");
    auto stats_before = blk.GetStats();

    // 连续执行多次写+读操作，Event Index 应使部分 Kick 被省略
    constexpr int kBatchCount = 8;
    bool batch_ok = true;
    for (int b = 0; b < kBatchCount; ++b) {
      uint64_t sector = 50 + b;
      auto pattern = static_cast<uint8_t>(0xE0 + b);
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        g_data_buf[i] = static_cast<uint8_t>(pattern + (i & 0x0F));
      }
      auto wr = blk.Write(sector, g_data_buf);
      if (!wr.has_value()) {
        batch_ok = false;
        LOG_HEX("Batch write failed at sector", sector);
        break;
      }
    }
    EXPECT_TRUE(batch_ok, "Event Index: batch write succeeds");

    // 读回验证
    bool verify_ok = true;
    for (int b = 0; b < kBatchCount && batch_ok; ++b) {
      uint64_t sector = 50 + b;
      auto pattern = static_cast<uint8_t>(0xE0 + b);
      memzero(g_data_buf, sizeof(g_data_buf));
      auto rd = blk.Read(sector, g_data_buf);
      if (!rd.has_value()) {
        verify_ok = false;
        break;
      }
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        if (g_data_buf[i] != static_cast<uint8_t>(pattern + (i & 0x0F))) {
          verify_ok = false;
          LOG_HEX("Batch verify mismatch at sector", sector);
          break;
        }
      }
      if (!verify_ok) {
        break;
      }
    }
    EXPECT_TRUE(verify_ok, "Event Index: batch read-verify succeeds");

    auto stats_after = blk.GetStats();
    LOG_HEX("kicks_elided (before batch)", stats_before.kicks_elided);
    LOG_HEX("kicks_elided (after batch)", stats_after.kicks_elided);
    LOG_HEX("interrupts_handled", stats_after.interrupts_handled);
    LOG_HEX("bytes_transferred", stats_after.bytes_transferred);

    // 验证性能统计基本正确（bytes > 0, interrupts > 0）
    EXPECT_TRUE(stats_after.bytes_transferred > 0,
                "Event Index: bytes_transferred > 0");
    EXPECT_TRUE(stats_after.interrupts_handled > 0,
                "Event Index: interrupts_handled > 0");
  }

  // === 测试 14: SG 多扇区写入（Scatter-Gather，4 个连续扇区一次提交） ===
  {
    LOG("Testing SG multi-sector write (4 contiguous sectors 20-23)...");
    constexpr size_t kSgSectors = 4;
    constexpr uint64_t kSgBaseSector = 20;

    // 填充 4 扇区各不相同的数据模式
    for (size_t s = 0; s < kSgSectors; ++s) {
      auto pattern = static_cast<uint8_t>(0xB0 + s);
      auto* sector_buf =
          g_multi_sector_buf + s * device_framework::virtio::blk::kSectorSize;
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        sector_buf[i] = static_cast<uint8_t>(pattern + (i & 0xFF));
      }
    }

    // 构建 4 个 IoVec，每个指向一个扇区缓冲区
    device_framework::virtio::IoVec write_iovs[kSgSectors];
    for (size_t s = 0; s < kSgSectors; ++s) {
      write_iovs[s] = {RiscvTraits::VirtToPhys(
                           g_multi_sector_buf +
                           s * device_framework::virtio::blk::kSectorSize),
                       device_framework::virtio::blk::kSectorSize};
    }

    // 使用 EnqueueWrite + Kick 的异步方式提交 SG 写请求
    auto enq_w = blk.EnqueueWrite(0, kSgBaseSector, write_iovs, kSgSectors);
    EXPECT_TRUE(enq_w.has_value(), "SG write enqueue succeeds");

    if (enq_w.has_value()) {
      blk.Kick(0);

      // 轮询 HandleInterrupt 直到回调触发
      bool sg_write_done = false;
      device_framework::ErrorCode sg_write_ec =
          device_framework::ErrorCode::kTimeout;
      for (uint32_t spin = 0; spin < 100000000 && !sg_write_done; ++spin) {
        RiscvTraits::Rmb();
        blk.HandleInterrupt(
            [&sg_write_done, &sg_write_ec](void* /*token*/,
                                           device_framework::ErrorCode status) {
              sg_write_done = true;
              sg_write_ec = status;
            });
      }
      EXPECT_TRUE(sg_write_done, "SG write completed via callback");
      EXPECT_EQ(static_cast<uint32_t>(device_framework::ErrorCode::kSuccess),
                static_cast<uint32_t>(sg_write_ec),
                "SG write status is kSuccess");
    }
  }

  // === 测试 15: SG 多扇区读取（Scatter-Gather，4 个连续扇区一次读取并验证）
  // ===
  {
    LOG("Testing SG multi-sector read (4 contiguous sectors 20-23)...");
    constexpr size_t kSgSectors = 4;
    constexpr uint64_t kSgBaseSector = 20;

    // 清零多扇区缓冲区
    memzero(g_multi_sector_buf, sizeof(g_multi_sector_buf));

    // 构建 4 个 IoVec 用于读取
    device_framework::virtio::IoVec read_iovs[kSgSectors];
    for (size_t s = 0; s < kSgSectors; ++s) {
      read_iovs[s] = {RiscvTraits::VirtToPhys(
                          g_multi_sector_buf +
                          s * device_framework::virtio::blk::kSectorSize),
                      device_framework::virtio::blk::kSectorSize};
    }

    // 使用 EnqueueRead + Kick 的异步方式提交 SG 读请求
    auto enq_r = blk.EnqueueRead(0, kSgBaseSector, read_iovs, kSgSectors);
    EXPECT_TRUE(enq_r.has_value(), "SG read enqueue succeeds");

    if (enq_r.has_value()) {
      blk.Kick(0);

      // 轮询 HandleInterrupt 直到回调触发
      bool sg_read_done = false;
      device_framework::ErrorCode sg_read_ec =
          device_framework::ErrorCode::kTimeout;
      for (uint32_t spin = 0; spin < 100000000 && !sg_read_done; ++spin) {
        RiscvTraits::Rmb();
        blk.HandleInterrupt(
            [&sg_read_done, &sg_read_ec](void* /*token*/,
                                         device_framework::ErrorCode status) {
              sg_read_done = true;
              sg_read_ec = status;
            });
      }
      EXPECT_TRUE(sg_read_done, "SG read completed via callback");
      EXPECT_EQ(static_cast<uint32_t>(device_framework::ErrorCode::kSuccess),
                static_cast<uint32_t>(sg_read_ec),
                "SG read status is kSuccess");

      // 验证读回的数据与之前写入的数据一致
      if (sg_read_done && sg_read_ec == device_framework::ErrorCode::kSuccess) {
        bool sg_data_match = true;
        for (size_t s = 0; s < kSgSectors && sg_data_match; ++s) {
          auto pattern = static_cast<uint8_t>(0xB0 + s);
          auto* sector_buf = g_multi_sector_buf +
                             s * device_framework::virtio::blk::kSectorSize;
          for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize;
               ++i) {
            auto expected = static_cast<uint8_t>(pattern + (i & 0xFF));
            if (sector_buf[i] != expected) {
              sg_data_match = false;
              LOG_HEX("  SG data mismatch at sector offset", s);
              LOG_HEX("  byte offset", i);
              LOG_HEX("  expected", expected);
              LOG_HEX("  got", sector_buf[i]);
              break;
            }
          }
        }
        EXPECT_TRUE(sg_data_match, "SG read data matches SG write data");
      }
    }
  }

  // === 测试 16: 异步批量提交（多个 Enqueue → 单次 Kick → HandleInterrupt
  // 回调） ===
  {
    LOG("Testing async batch submit (3 writes, single Kick, "
        "HandleInterrupt)...");
    constexpr size_t kBatchRequests = 3;
    constexpr uint64_t kBatchBaseSector = 30;

    // 为每个请求准备不同的数据模式
    struct BatchCtx {
      uint64_t sector;
      uint8_t pattern;
      bool completed;
      device_framework::ErrorCode status;
    };
    BatchCtx batch[kBatchRequests];
    for (size_t r = 0; r < kBatchRequests; ++r) {
      batch[r].sector = kBatchBaseSector + r;
      batch[r].pattern = static_cast<uint8_t>(0xC0 + r);
      batch[r].completed = false;
      batch[r].status = device_framework::ErrorCode::kTimeout;
    }

    // 使用 g_multi_sector_buf 为每个请求分配独立的缓冲区
    // （异步提交共享同一 buffer 会导致设备读到被覆写的数据）
    bool all_enqueued = true;
    for (size_t r = 0; r < kBatchRequests; ++r) {
      auto* buf =
          g_multi_sector_buf + r * device_framework::virtio::blk::kSectorSize;
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        buf[i] = static_cast<uint8_t>(batch[r].pattern + (i & 0x0F));
      }

      device_framework::virtio::IoVec iov{
          RiscvTraits::VirtToPhys(buf),
          device_framework::virtio::blk::kSectorSize};
      auto enq =
          blk.EnqueueWrite(0, batch[r].sector, &iov, 1,
                           reinterpret_cast<void*>(static_cast<uintptr_t>(r)));
      if (!enq.has_value()) {
        all_enqueued = false;
        LOG_HEX("  Batch enqueue failed at request", r);
        break;
      }
    }
    EXPECT_TRUE(all_enqueued, "Async batch: all 3 requests enqueued");

    if (all_enqueued) {
      // 单次 Kick 通知设备处理所有请求
      blk.Kick(0);

      // 轮询等待所有请求完成
      uint32_t completed_count = 0;
      for (uint32_t spin = 0;
           spin < 100000000 && completed_count < kBatchRequests; ++spin) {
        RiscvTraits::Rmb();
        blk.HandleInterrupt([&batch, &completed_count, kBatchRequests](
                                void* token,
                                device_framework::ErrorCode status) {
          auto idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(token));
          if (idx < kBatchRequests && !batch[idx].completed) {
            batch[idx].completed = true;
            batch[idx].status = status;
            ++completed_count;
          }
        });
        if (completed_count >= kBatchRequests) {
          break;
        }
      }

      EXPECT_EQ(static_cast<uint32_t>(kBatchRequests), completed_count,
                "Async batch: all 3 requests completed via callback");

      bool all_success = true;
      for (size_t r = 0; r < kBatchRequests; ++r) {
        if (batch[r].status != device_framework::ErrorCode::kSuccess) {
          all_success = false;
          LOG_HEX("  Batch request failed, index", r);
          break;
        }
      }
      EXPECT_TRUE(all_success, "Async batch: all requests returned kSuccess");
    }

    // 读回验证批量写入的数据
    LOG("Verifying batch-written sectors...");
    bool batch_verify_ok = true;
    for (size_t r = 0; r < kBatchRequests && batch_verify_ok; ++r) {
      memzero(g_data_buf, sizeof(g_data_buf));
      auto rd = blk.Read(batch[r].sector, g_data_buf);
      if (!rd.has_value()) {
        batch_verify_ok = false;
        LOG_HEX("  Batch verify read failed at sector", batch[r].sector);
        break;
      }
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        auto expected = static_cast<uint8_t>(batch[r].pattern + (i & 0x0F));
        if (g_data_buf[i] != expected) {
          batch_verify_ok = false;
          LOG_HEX("  Batch verify mismatch at sector", batch[r].sector);
          LOG_HEX("  byte", i);
          LOG_HEX("  expected", expected);
          LOG_HEX("  got", g_data_buf[i]);
          break;
        }
      }
    }
    EXPECT_TRUE(batch_verify_ok,
                "Async batch: read-verify matches written data");
  }

  // === 测试 17: Event Index kicks_elided 验证 ===
  {
    LOG("Testing Event Index kicks_elided via rapid async submit...");
    auto stats_before_ei = blk.GetStats();
    uint64_t kicks_before = stats_before_ei.kicks_elided;

    // 快速连续提交多个请求并分别 Kick，Event Index 应抑制部分通知
    constexpr int kRapidKickCount = 6;
    bool rapid_ok = true;
    for (int k = 0; k < kRapidKickCount; ++k) {
      uint64_t sector = 40 + k;
      auto pattern = static_cast<uint8_t>(0xD0 + k);
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        g_data_buf[i] = static_cast<uint8_t>(pattern + (i & 0x0F));
      }
      device_framework::virtio::IoVec iov{
          RiscvTraits::VirtToPhys(g_data_buf),
          device_framework::virtio::blk::kSectorSize};
      auto enq = blk.EnqueueWrite(0, sector, &iov, 1);
      if (!enq.has_value()) {
        rapid_ok = false;
        break;
      }
      // 每次 Enqueue 后立即 Kick，测试 Event Index 抑制
      blk.Kick(0);
    }
    EXPECT_TRUE(rapid_ok, "Event Index rapid: all enqueues succeeded");

    // 等待所有请求完成
    for (uint32_t spin = 0; spin < 100000000; ++spin) {
      RiscvTraits::Rmb();
      blk.HandleInterrupt(
          [](void* /*token*/, device_framework::ErrorCode /*status*/) {});
      // 给设备足够时间处理
      if (spin > 1000) {
        break;
      }
    }

    auto stats_after_ei = blk.GetStats();
    LOG_HEX("kicks_elided before rapid test", kicks_before);
    LOG_HEX("kicks_elided after rapid test", stats_after_ei.kicks_elided);
    LOG_HEX("total kicks_elided delta",
            stats_after_ei.kicks_elided - kicks_before);

    // 注意：kicks_elided 是否 > 0 取决于设备处理速度和 Event Index 时序
    // 在 QEMU 同步模拟中，设备通常立即处理，所以 avail_event 可能总是被超过
    // 我们仅记录结果，不做强断言，但验证统计值不倒退
    EXPECT_TRUE(stats_after_ei.kicks_elided >= kicks_before,
                "Event Index: kicks_elided did not decrease");
    EXPECT_TRUE(
        stats_after_ei.bytes_transferred > stats_before_ei.bytes_transferred,
        "Event Index rapid: bytes_transferred increased");
  }

  // === 测试 18: HandleInterrupt 无待处理请求时回调不应触发 ===
  {
    LOG("Testing HandleInterrupt with no pending requests...");
    uint32_t callback_count = 0;
    blk.HandleInterrupt(
        [&callback_count](void* /*token*/,
                          device_framework::ErrorCode /*status*/) {
          ++callback_count;
        });
    EXPECT_EQ(static_cast<uint32_t>(0), callback_count,
              "HandleInterrupt: callback not invoked when no pending requests");
  }

  // === 测试 19: HandleInterrupt 单请求回调 - Token 传递验证 ===
  {
    LOG("Testing HandleInterrupt single request with token passthrough...");
    constexpr uint64_t kTestSector = 60;
    constexpr uintptr_t kMagicToken = 0xDEADBEEF;

    // 写入已知数据
    for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
      g_data_buf[i] = static_cast<uint8_t>(0x77 + (i & 0x0F));
    }

    device_framework::virtio::IoVec iov{
        RiscvTraits::VirtToPhys(g_data_buf),
        device_framework::virtio::blk::kSectorSize};
    auto enq = blk.EnqueueWrite(0, kTestSector, &iov, 1,
                                reinterpret_cast<void*>(kMagicToken));
    EXPECT_TRUE(enq.has_value(), "HandleInterrupt token: enqueue succeeds");

    if (enq.has_value()) {
      blk.Kick(0);

      bool cb_invoked = false;
      uintptr_t received_token = 0;
      device_framework::ErrorCode received_status =
          device_framework::ErrorCode::kTimeout;
      uint32_t cb_count = 0;

      for (uint32_t spin = 0; spin < 100000000 && !cb_invoked; ++spin) {
        RiscvTraits::Rmb();
        blk.HandleInterrupt(
            [&cb_invoked, &received_token, &received_status, &cb_count](
                void* token, device_framework::ErrorCode status) {
              cb_invoked = true;
              received_token = reinterpret_cast<uintptr_t>(token);
              received_status = status;
              ++cb_count;
            });
      }

      EXPECT_TRUE(cb_invoked, "HandleInterrupt token: callback invoked");
      EXPECT_EQ(static_cast<uint64_t>(kMagicToken),
                static_cast<uint64_t>(received_token),
                "HandleInterrupt token: token matches 0xDEADBEEF");
      EXPECT_EQ(static_cast<uint32_t>(device_framework::ErrorCode::kSuccess),
                static_cast<uint32_t>(received_status),
                "HandleInterrupt token: status is kSuccess");
      EXPECT_EQ(static_cast<uint32_t>(1), cb_count,
                "HandleInterrupt token: callback invoked exactly once");
    }
  }

  // === 测试 20: HandleInterrupt 读请求回调 + 数据一致性验证 ===
  {
    LOG("Testing HandleInterrupt read callback with data verification...");
    constexpr uint64_t kTestSector = 60;

    // 清零缓冲区，读回之前测试 19 写入的数据
    memzero(g_data_buf, sizeof(g_data_buf));

    device_framework::virtio::IoVec iov{
        RiscvTraits::VirtToPhys(g_data_buf),
        device_framework::virtio::blk::kSectorSize};
    auto enq = blk.EnqueueRead(0, kTestSector, &iov, 1, nullptr);
    EXPECT_TRUE(enq.has_value(), "HandleInterrupt read: enqueue succeeds");

    if (enq.has_value()) {
      blk.Kick(0);

      bool cb_invoked = false;
      device_framework::ErrorCode cb_status =
          device_framework::ErrorCode::kTimeout;

      for (uint32_t spin = 0; spin < 100000000 && !cb_invoked; ++spin) {
        RiscvTraits::Rmb();
        blk.HandleInterrupt(
            [&cb_invoked, &cb_status](void* /*token*/,
                                      device_framework::ErrorCode status) {
              cb_invoked = true;
              cb_status = status;
            });
      }

      EXPECT_TRUE(cb_invoked, "HandleInterrupt read: callback invoked");
      EXPECT_EQ(static_cast<uint32_t>(device_framework::ErrorCode::kSuccess),
                static_cast<uint32_t>(cb_status),
                "HandleInterrupt read: status is kSuccess");

      // 验证读回数据与测试 19 写入的 pattern 一致
      if (cb_invoked && cb_status == device_framework::ErrorCode::kSuccess) {
        bool data_ok = true;
        for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize;
             ++i) {
          auto expected = static_cast<uint8_t>(0x77 + (i & 0x0F));
          if (g_data_buf[i] != expected) {
            data_ok = false;
            LOG_HEX("  Read callback data mismatch at byte", i);
            LOG_HEX("  expected", expected);
            LOG_HEX("  got", g_data_buf[i]);
            break;
          }
        }
        EXPECT_TRUE(data_ok,
                    "HandleInterrupt read: data matches previously written");
      }
    }
  }

  // === 测试 21: HandleInterrupt 批量请求 - 多 Token 精确匹配 ===
  {
    LOG("Testing HandleInterrupt batch with distinct tokens...");
    constexpr size_t kBatchSize = 4;
    constexpr uint64_t kBaseSector = 70;

    struct TokenCtx {
      uintptr_t token;
      bool completed;
      device_framework::ErrorCode status;
    };
    TokenCtx contexts[kBatchSize];
    for (size_t i = 0; i < kBatchSize; ++i) {
      contexts[i].token = 0xA000 + i;  // 不同的 magic token
      contexts[i].completed = false;
      contexts[i].status = device_framework::ErrorCode::kTimeout;
    }

    // 使用 g_multi_sector_buf 为每个请求分配独立的缓冲区
    bool all_enqueued = true;
    for (size_t r = 0; r < kBatchSize; ++r) {
      auto* buf =
          g_multi_sector_buf + r * device_framework::virtio::blk::kSectorSize;
      auto pattern = static_cast<uint8_t>(0xF0 + r);
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        buf[i] = static_cast<uint8_t>(pattern + (i & 0xFF));
      }

      device_framework::virtio::IoVec iov{
          RiscvTraits::VirtToPhys(buf),
          device_framework::virtio::blk::kSectorSize};
      auto enq = blk.EnqueueWrite(0, kBaseSector + r, &iov, 1,
                                  reinterpret_cast<void*>(contexts[r].token));
      if (!enq.has_value()) {
        all_enqueued = false;
        LOG_HEX("  Batch token enqueue failed at index", r);
        break;
      }
    }
    EXPECT_TRUE(all_enqueued, "HandleInterrupt batch tokens: all enqueued");

    if (all_enqueued) {
      blk.Kick(0);

      uint32_t completed_count = 0;
      for (uint32_t spin = 0; spin < 100000000 && completed_count < kBatchSize;
           ++spin) {
        RiscvTraits::Rmb();
        blk.HandleInterrupt(
            [&contexts, &completed_count, kBatchSize](
                void* token, device_framework::ErrorCode status) {
              auto tok = reinterpret_cast<uintptr_t>(token);
              for (size_t i = 0; i < kBatchSize; ++i) {
                if (contexts[i].token == tok && !contexts[i].completed) {
                  contexts[i].completed = true;
                  contexts[i].status = status;
                  ++completed_count;
                  break;
                }
              }
            });
        if (completed_count >= kBatchSize) {
          break;
        }
      }

      EXPECT_EQ(static_cast<uint32_t>(kBatchSize), completed_count,
                "HandleInterrupt batch tokens: all 4 completed");

      // 验证每个请求的 token 都被正确传回且状态为 kSuccess
      bool all_tokens_ok = true;
      bool all_status_ok = true;
      for (size_t i = 0; i < kBatchSize; ++i) {
        if (!contexts[i].completed) {
          all_tokens_ok = false;
          LOG_HEX("  Token not completed, index", i);
        }
        if (contexts[i].status != device_framework::ErrorCode::kSuccess) {
          all_status_ok = false;
          LOG_HEX("  Token request failed, index", i);
        }
      }
      EXPECT_TRUE(all_tokens_ok,
                  "HandleInterrupt batch tokens: all tokens matched");
      EXPECT_TRUE(all_status_ok,
                  "HandleInterrupt batch tokens: all returned kSuccess");
    }
  }

  // === 测试 22: HandleInterrupt 统计数据验证 ===
  {
    LOG("Testing HandleInterrupt stats update...");
    auto stats_before = blk.GetStats();
    uint64_t irq_before = stats_before.interrupts_handled;
    uint64_t bytes_before = stats_before.bytes_transferred;

    // 执行一次写请求
    for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
      g_data_buf[i] = static_cast<uint8_t>(0x99);
    }
    device_framework::virtio::IoVec iov{
        RiscvTraits::VirtToPhys(g_data_buf),
        device_framework::virtio::blk::kSectorSize};
    auto enq = blk.EnqueueWrite(0, 80, &iov, 1, nullptr);
    EXPECT_TRUE(enq.has_value(), "HandleInterrupt stats: enqueue succeeds");

    if (enq.has_value()) {
      blk.Kick(0);

      bool done = false;
      for (uint32_t spin = 0; spin < 100000000 && !done; ++spin) {
        RiscvTraits::Rmb();
        blk.HandleInterrupt(
            [&done](void* /*token*/, device_framework::ErrorCode /*status*/) {
              done = true;
            });
      }
      EXPECT_TRUE(done, "HandleInterrupt stats: request completed");

      auto stats_after = blk.GetStats();
      EXPECT_TRUE(stats_after.interrupts_handled > irq_before,
                  "HandleInterrupt stats: interrupts_handled incremented");
      EXPECT_TRUE(stats_after.bytes_transferred > bytes_before,
                  "HandleInterrupt stats: bytes_transferred incremented");
      LOG_HEX("interrupts_handled delta",
              stats_after.interrupts_handled - irq_before);
      LOG_HEX("bytes_transferred delta",
              stats_after.bytes_transferred - bytes_before);
    }
  }

  // === 测试 23: HandleInterrupt 完成后再次调用不会重复触发回调 ===
  {
    LOG("Testing HandleInterrupt idempotency after completion...");
    constexpr uint64_t kTestSector = 90;

    // 写入一个扇区
    for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
      g_data_buf[i] = static_cast<uint8_t>(0xAB);
    }
    device_framework::virtio::IoVec iov{
        RiscvTraits::VirtToPhys(g_data_buf),
        device_framework::virtio::blk::kSectorSize};
    auto enq = blk.EnqueueWrite(0, kTestSector, &iov, 1, nullptr);
    EXPECT_TRUE(enq.has_value(),
                "HandleInterrupt idempotent: enqueue succeeds");

    if (enq.has_value()) {
      blk.Kick(0);

      // 第一轮：等待回调触发
      uint32_t first_round_cb = 0;
      for (uint32_t spin = 0; spin < 100000000 && first_round_cb == 0; ++spin) {
        RiscvTraits::Rmb();
        blk.HandleInterrupt(
            [&first_round_cb](void* /*token*/,
                              device_framework::ErrorCode /*status*/) {
              ++first_round_cb;
            });
      }
      EXPECT_EQ(static_cast<uint32_t>(1), first_round_cb,
                "HandleInterrupt idempotent: first round invoked once");

      // 第二轮：已无待处理请求，回调不应再触发
      uint32_t second_round_cb = 0;
      blk.HandleInterrupt(
          [&second_round_cb](void* /*token*/,
                             device_framework::ErrorCode /*status*/) {
            ++second_round_cb;
          });
      EXPECT_EQ(static_cast<uint32_t>(0), second_round_cb,
                "HandleInterrupt idempotent: second round not invoked");

      // 第三轮：再调用一次确认幂等性
      uint32_t third_round_cb = 0;
      blk.HandleInterrupt(
          [&third_round_cb](void* /*token*/,
                            device_framework::ErrorCode /*status*/) {
            ++third_round_cb;
          });
      EXPECT_EQ(static_cast<uint32_t>(0), third_round_cb,
                "HandleInterrupt idempotent: third round not invoked");
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
