/**
 * @file virtio_blk_device_test.cpp
 * @brief VirtIO 块设备统一接口测试
 * @copyright Copyright The device_framework Contributors
 *
 * 测试 VirtIO 块设备通过统一 BlockDevice 接口的操作：
 * Open/ReadBlock/WriteBlock/ReadBlocks/WriteBlocks/Read/Write/Release
 * 及错误路径
 */

#include <cstdint>

#include "device_framework/virtio_blk.hpp"
#include "test.h"
#include "test_env.h"

void test_virtio_blk_device() {
  TEST_SUITE_BEGIN("VirtIO BlockDevice Interface");

  // === 测试 1: 查找块设备 ===
  uint64_t blk_base = FindBlkDevice();
  EXPECT_TRUE(blk_base != 0, "Find VirtIO block device");
  if (blk_base == 0) {
    LOG("No block device found, skipping remaining tests");
    TEST_SUITE_END();
    return;
  }

  // === 测试 2: VirtioBlkDevice::Create() ===
  using DeviceType =
      device_framework::virtio::blk::VirtioBlkDevice<RiscvTraits>;
  constexpr size_t kRequiredDmaSize = DeviceType::CalcDmaSize();
  static_assert(kDmaBufSize >= kRequiredDmaSize,
                "g_dma_buf too small for VirtioBlkDevice");
  Memzero(g_dma_buf, kRequiredDmaSize);

  auto dev_result = DeviceType::Create(blk_base, g_dma_buf);
  EXPECT_TRUE(dev_result.has_value(), "VirtioBlkDevice::Create() succeeds");
  if (!dev_result.has_value()) {
    LOG("VirtioBlkDevice::Create() failed, skipping remaining tests");
    TEST_SUITE_END();
    return;
  }
  auto& dev = *dev_result;

  // === 测试 3: 未打开时操作应失败 ===
  {
    auto read_result =
        dev.ReadBlock(0, std::span<uint8_t>(g_data_buf, sizeof(g_data_buf)));
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
      EXPECT_EQ(static_cast<uint32_t>(
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
      g_data_buf[i] = static_cast<uint8_t>(0xBB + (i & 0x0F));
    }

    auto write_result = dev.WriteBlock(
        80, std::span<const uint8_t>(g_data_buf, sizeof(g_data_buf)));
    EXPECT_TRUE(write_result.has_value(), "WriteBlock(80) succeeds");
  }

  // === 测试 8: ReadBlock 读取单个块并验证 ===
  {
    Memzero(g_data_buf, sizeof(g_data_buf));

    auto read_result =
        dev.ReadBlock(80, std::span<uint8_t>(g_data_buf, sizeof(g_data_buf)));
    EXPECT_TRUE(read_result.has_value(), "ReadBlock(80) succeeds");

    if (read_result.has_value()) {
      bool match = true;
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        if (g_data_buf[i] != static_cast<uint8_t>(0xBB + (i & 0x0F))) {
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

    for (size_t s = 0; s < kNumBlocks; ++s) {
      auto pattern = static_cast<uint8_t>(0xC0 + s);
      auto* buf = g_multi_buf + s * device_framework::virtio::blk::kSectorSize;
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        buf[i] = static_cast<uint8_t>(pattern + (i & 0xFF));
      }
    }

    auto write_result = dev.WriteBlocks(
        kBaseBlock, std::span<const uint8_t>(g_multi_buf, sizeof(g_multi_buf)),
        kNumBlocks);
    EXPECT_TRUE(write_result.has_value(), "WriteBlocks(4 blocks) succeeds");
    if (write_result.has_value()) {
      EXPECT_EQ(static_cast<uint64_t>(kNumBlocks),
                static_cast<uint64_t>(*write_result),
                "WriteBlocks returned correct block count");
    }

    Memzero(g_multi_buf, sizeof(g_multi_buf));
    auto read_result = dev.ReadBlocks(
        kBaseBlock, std::span<uint8_t>(g_multi_buf, sizeof(g_multi_buf)),
        kNumBlocks);
    EXPECT_TRUE(read_result.has_value(), "ReadBlocks(4 blocks) succeeds");

    if (read_result.has_value()) {
      bool all_match = true;
      for (size_t s = 0; s < kNumBlocks && all_match; ++s) {
        auto pattern = static_cast<uint8_t>(0xC0 + s);
        auto* buf =
            g_multi_buf + s * device_framework::virtio::blk::kSectorSize;
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

  // === 测试 10: 字节级 Write(offset) -> 块桥接 ===
  {
    constexpr size_t kOffset = 95 * device_framework::virtio::blk::kSectorSize;

    for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
      g_data_buf[i] = static_cast<uint8_t>(0xDD + (i & 0x0F));
    }

    auto write_result = dev.Write(
        std::span<const uint8_t>(g_data_buf, sizeof(g_data_buf)), kOffset);
    EXPECT_TRUE(write_result.has_value(),
                "Write(offset=sector95) byte-level succeeds");
    if (write_result.has_value()) {
      EXPECT_EQ(static_cast<uint64_t>(sizeof(g_data_buf)),
                static_cast<uint64_t>(*write_result),
                "Write returned correct byte count");
    }
  }

  // === 测试 11: 字节级 Read(offset) -> 块桥接 ===
  {
    constexpr size_t kOffset = 95 * device_framework::virtio::blk::kSectorSize;

    Memzero(g_data_buf, sizeof(g_data_buf));

    auto read_result =
        dev.Read(std::span<uint8_t>(g_data_buf, sizeof(g_data_buf)), kOffset);
    EXPECT_TRUE(read_result.has_value(),
                "Read(offset=sector95) byte-level succeeds");

    if (read_result.has_value()) {
      bool match = true;
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        if (g_data_buf[i] != static_cast<uint8_t>(0xDD + (i & 0x0F))) {
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
        dev.Read(std::span<uint8_t>(g_data_buf, sizeof(g_data_buf)),
                 100);  // 非 512 对齐
    EXPECT_FALSE(read_result.has_value(), "Read(offset=100 non-aligned) fails");
    if (!read_result.has_value()) {
      EXPECT_EQ(static_cast<uint32_t>(
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
    auto read_result =
        dev.ReadBlock(0, std::span<uint8_t>(g_data_buf, sizeof(g_data_buf)));
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

  // ======== HandleInterrupt 通过 BlockDevice ops 层测试 ========

  // 需要重新创建设备实例（前一个已被同步 Read/Write 使用过）
  Memzero(g_dma_buf, kRequiredDmaSize);
  auto dev2_result = DeviceType::Create(blk_base, g_dma_buf);
  EXPECT_TRUE(dev2_result.has_value(),
              "VirtioBlkDevice::Create() for interrupt tests");
  if (!dev2_result.has_value()) {
    LOG("Cannot create device for interrupt tests, skipping");
    TEST_SUITE_END();
    return;
  }
  auto& dev2 = *dev2_result;

  // === 测试 16: HandleInterrupt 简化版（无回调） ===
  {
    // 无待处理请求时调用应安全返回
    dev2.HandleInterrupt();
    EXPECT_TRUE(true,
                "HandleInterrupt() without pending requests does not crash");
  }

  // === 测试 17: HandleInterrupt 带回调 - 无待处理请求 ===
  {
    uint32_t callback_count = 0;
    dev2.HandleInterrupt(
        [&callback_count](void* /*token*/,
                          device_framework::ErrorCode /*status*/) {
          ++callback_count;
        });
    EXPECT_EQ(static_cast<uint32_t>(0), callback_count,
              "HandleInterrupt callback not invoked when no pending IO");
  }

  // === 测试 18: HandleInterrupt 带回调 - 异步写入完成 ===
  {
    auto open_result = dev2.OpenReadWrite();
    EXPECT_TRUE(open_result.has_value(), "Open for async HandleInterrupt test");

    if (open_result.has_value()) {
      // 准备数据
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        g_data_buf[i] = static_cast<uint8_t>(0xCC + (i & 0x0F));
      }

      // 通过底层驱动的异步接口提交请求
      auto& driver = dev2.GetDriver();
      device_framework::virtio::IoVec iov{
          RiscvTraits::VirtToPhys(g_data_buf),
          device_framework::virtio::blk::kSectorSize};
      auto enq = driver.EnqueueWrite(0, 85, &iov, 1, nullptr);
      EXPECT_TRUE(enq.has_value(), "Async enqueue for ops HandleInterrupt");

      if (enq.has_value()) {
        driver.Kick(0);

        // 通过 ops 层的 HandleInterrupt 处理完成
        bool cb_invoked = false;
        device_framework::ErrorCode cb_status =
            device_framework::ErrorCode::kTimeout;

        for (uint32_t spin = 0; spin < 100000000 && !cb_invoked; ++spin) {
          RiscvTraits::Rmb();
          dev2.HandleInterrupt(
              [&cb_invoked, &cb_status](void* /*token*/,
                                        device_framework::ErrorCode status) {
                cb_invoked = true;
                cb_status = status;
              });
        }

        EXPECT_TRUE(cb_invoked,
                    "Ops-layer HandleInterrupt: write callback invoked");
        EXPECT_EQ(static_cast<uint32_t>(device_framework::ErrorCode::kSuccess),
                  static_cast<uint32_t>(cb_status),
                  "Ops-layer HandleInterrupt: write status is kSuccess");
      }

      (void)dev2.Release();
    }
  }

  // === 测试 19: HandleInterrupt 带回调 - 异步读取 + 数据验证 ===
  {
    auto open_result = dev2.OpenReadWrite();
    EXPECT_TRUE(open_result.has_value(),
                "Open for async read HandleInterrupt test");

    if (open_result.has_value()) {
      Memzero(g_data_buf, sizeof(g_data_buf));

      auto& driver = dev2.GetDriver();
      device_framework::virtio::IoVec iov{
          RiscvTraits::VirtToPhys(g_data_buf),
          device_framework::virtio::blk::kSectorSize};
      auto enq = driver.EnqueueRead(0, 85, &iov, 1, nullptr);
      EXPECT_TRUE(enq.has_value(),
                  "Async read enqueue for ops HandleInterrupt");

      if (enq.has_value()) {
        driver.Kick(0);

        bool cb_invoked = false;
        device_framework::ErrorCode cb_status =
            device_framework::ErrorCode::kTimeout;

        for (uint32_t spin = 0; spin < 100000000 && !cb_invoked; ++spin) {
          RiscvTraits::Rmb();
          dev2.HandleInterrupt(
              [&cb_invoked, &cb_status](void* /*token*/,
                                        device_framework::ErrorCode status) {
                cb_invoked = true;
                cb_status = status;
              });
        }

        EXPECT_TRUE(cb_invoked,
                    "Ops-layer HandleInterrupt: read callback invoked");
        EXPECT_EQ(static_cast<uint32_t>(device_framework::ErrorCode::kSuccess),
                  static_cast<uint32_t>(cb_status),
                  "Ops-layer HandleInterrupt: read status is kSuccess");

        if (cb_invoked && cb_status == device_framework::ErrorCode::kSuccess) {
          bool data_ok = true;
          for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize;
               ++i) {
            auto expected = static_cast<uint8_t>(0xCC + (i & 0x0F));
            if (g_data_buf[i] != expected) {
              data_ok = false;
              LOG_HEX("Ops HandleInterrupt read mismatch at byte", i);
              break;
            }
          }
          EXPECT_TRUE(data_ok,
                      "Ops-layer HandleInterrupt: read data matches "
                      "previously written");
        }
      }

      (void)dev2.Release();
    }
  }

  // === 测试 20: HandleInterrupt 幂等性 - ops 层 ===
  {
    auto open_result = dev2.OpenReadWrite();
    EXPECT_TRUE(open_result.has_value(),
                "Open for idempotent HandleInterrupt test");

    if (open_result.has_value()) {
      for (size_t i = 0; i < device_framework::virtio::blk::kSectorSize; ++i) {
        g_data_buf[i] = static_cast<uint8_t>(0xEE);
      }

      auto& driver = dev2.GetDriver();
      device_framework::virtio::IoVec iov{
          RiscvTraits::VirtToPhys(g_data_buf),
          device_framework::virtio::blk::kSectorSize};
      auto enq = driver.EnqueueWrite(0, 86, &iov, 1, nullptr);
      EXPECT_TRUE(enq.has_value(), "Idempotent test: enqueue succeeds");

      if (enq.has_value()) {
        driver.Kick(0);

        // 第一次调用应触发回调
        uint32_t first_cb = 0;
        for (uint32_t spin = 0; spin < 100000000 && first_cb == 0; ++spin) {
          RiscvTraits::Rmb();
          dev2.HandleInterrupt(
              [&first_cb](void* /*token*/,
                          device_framework::ErrorCode /*status*/) {
                ++first_cb;
              });
        }
        EXPECT_EQ(static_cast<uint32_t>(1), first_cb,
                  "Ops HandleInterrupt idempotent: first round once");

        // 第二次调用不应再触发回调
        uint32_t second_cb = 0;
        dev2.HandleInterrupt(
            [&second_cb](void* /*token*/,
                         device_framework::ErrorCode /*status*/) {
              ++second_cb;
            });
        EXPECT_EQ(static_cast<uint32_t>(0), second_cb,
                  "Ops HandleInterrupt idempotent: second round zero");
      }

      (void)dev2.Release();
    }
  }

  TEST_SUITE_END();
}
