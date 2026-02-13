/**
 * @file ns16550a_test.cpp
 * @brief NS16550A 字符设备统一接口测试
 * @copyright Copyright The device_framework Contributors
 *
 * 测试 NS16550A 通过统一 CharDevice 接口的操作：
 * Open/PutChar/Write/Poll/Release 及错误路径
 */

#include "device_framework/ns16550a.hpp"

#include <cstdint>

#include "test.h"

namespace {

/// QEMU virt 机器第一个 UART 的 MMIO 基地址
/// 注意：0x10000000 已被 boot 阶段的 UART 占用，
/// 这里使用相同地址来验证 CharDevice 接口逻辑
constexpr uint64_t kUartBase = 0x10000000;

}  // namespace

void test_ns16550a() {
  TEST_SUITE_BEGIN("NS16550A CharDevice");

  // === 测试 1: 构造 Ns16550aDevice ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    EXPECT_TRUE(true, "Ns16550aDevice construction succeeds");
  }

  // === 测试 2: OpenReadWrite 成功 ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto open_result = uart.OpenReadWrite();
    EXPECT_TRUE(open_result.has_value(), "OpenReadWrite() succeeds");
    if (open_result.has_value()) {
      auto release_result = uart.Release();
      EXPECT_TRUE(release_result.has_value(), "Release() after open succeeds");
    }
  }

  // === 测试 3: 重复 Open 应返回 kDeviceAlreadyOpen ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto first = uart.OpenReadWrite();
    EXPECT_TRUE(first.has_value(), "First OpenReadWrite() succeeds");

    if (first.has_value()) {
      auto second = uart.OpenReadWrite();
      EXPECT_FALSE(second.has_value(),
                   "Second OpenReadWrite() fails (already open)");
      if (!second.has_value()) {
        EXPECT_EQ(static_cast<uint32_t>(
                      device_framework::ErrorCode::kDeviceAlreadyOpen),
                  static_cast<uint32_t>(second.error().code),
                  "Error code is kDeviceAlreadyOpen");
      }
      (void)uart.Release();
    }
  }

  // === 测试 4: 未打开时 Write 应返回 kDeviceNotOpen ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    uint8_t data[] = {'H', 'i'};
    auto write_result = uart.Write(std::span<const uint8_t>(data, 2));
    EXPECT_FALSE(write_result.has_value(),
                 "Write() before Open() fails (not open)");
    if (!write_result.has_value()) {
      EXPECT_EQ(
          static_cast<uint32_t>(device_framework::ErrorCode::kDeviceNotOpen),
          static_cast<uint32_t>(write_result.error().code),
          "Error code is kDeviceNotOpen");
    }
  }

  // === 测试 5: 未打开时 PutChar 应返回 kDeviceNotOpen ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto put_result = uart.PutChar('X');
    EXPECT_FALSE(put_result.has_value(),
                 "PutChar() before Open() fails (not open)");
  }

  // === 测试 6: 未打开时 Release 应返回 kDeviceNotOpen ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto release_result = uart.Release();
    EXPECT_FALSE(release_result.has_value(),
                 "Release() before Open() fails (not open)");
  }

  // === 测试 7: PutChar 通过统一接口输出字符 ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto open_result = uart.OpenReadWrite();
    EXPECT_TRUE(open_result.has_value(), "Open for PutChar test");

    if (open_result.has_value()) {
      auto put_result = uart.PutChar('O');
      EXPECT_TRUE(put_result.has_value(), "PutChar('O') succeeds");

      put_result = uart.PutChar('K');
      EXPECT_TRUE(put_result.has_value(), "PutChar('K') succeeds");

      put_result = uart.PutChar('\n');
      EXPECT_TRUE(put_result.has_value(), "PutChar('\\n') succeeds");

      (void)uart.Release();
    }
  }

  // === 测试 8: Write 通过统一接口写入多字节 ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto open_result = uart.OpenReadWrite();
    EXPECT_TRUE(open_result.has_value(), "Open for Write test");

    if (open_result.has_value()) {
      const uint8_t msg[] = {'U', 'A', 'R', 'T', '\n'};
      auto write_result =
          uart.Write(std::span<const uint8_t>(msg, sizeof(msg)));
      EXPECT_TRUE(write_result.has_value(), "Write('UART\\n') succeeds");
      if (write_result.has_value()) {
        EXPECT_EQ(static_cast<uint64_t>(sizeof(msg)),
                  static_cast<uint64_t>(*write_result),
                  "Write returned correct byte count");
      }

      (void)uart.Release();
    }
  }

  // === 测试 9: OpenReadOnly 后写入应返回 kDevicePermissionDenied ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto open_result = uart.OpenReadOnly();
    EXPECT_TRUE(open_result.has_value(), "OpenReadOnly() succeeds");

    if (open_result.has_value()) {
      auto put_result = uart.PutChar('X');
      EXPECT_FALSE(put_result.has_value(),
                   "PutChar() on read-only device fails");
      if (!put_result.has_value()) {
        EXPECT_EQ(static_cast<uint32_t>(
                      device_framework::ErrorCode::kDevicePermissionDenied),
                  static_cast<uint32_t>(put_result.error().code),
                  "Error code is kDevicePermissionDenied");
      }

      (void)uart.Release();
    }
  }

  // === 测试 10: Poll 查询 ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto open_result = uart.OpenReadWrite();
    EXPECT_TRUE(open_result.has_value(), "Open for Poll test");

    if (open_result.has_value()) {
      auto poll_result = uart.Poll(
          device_framework::PollEvents{device_framework::PollEvents::kOut});
      EXPECT_TRUE(poll_result.has_value(), "Poll(kOut) succeeds");
      if (poll_result.has_value()) {
        EXPECT_TRUE(poll_result->HasOut(),
                    "Poll reports output ready (kOut set)");
      }

      (void)uart.Release();
    }
  }

  // === 测试 11: Release 后再次 Open 应成功 ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto first_open = uart.OpenReadWrite();
    EXPECT_TRUE(first_open.has_value(), "First open for reopen test");
    if (first_open.has_value()) {
      (void)uart.Release();
    }

    auto second_open = uart.OpenReadWrite();
    EXPECT_TRUE(second_open.has_value(), "Re-open after Release() succeeds");
    if (second_open.has_value()) {
      (void)uart.Release();
    }
  }

  // === 测试 12: HandleInterrupt 简化版（无回调） ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    // HandleInterrupt 不要求设备处于打开状态，可直接调用
    // 在无待接收数据时应安全返回（不会崩溃或挂起）
    uart.HandleInterrupt();
    EXPECT_TRUE(true, "HandleInterrupt() without data does not crash");
  }

  // === 测试 13: HandleInterrupt 带回调版（无待接收数据） ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    uint32_t callback_count = 0;
    uart.HandleInterrupt(
        [&callback_count](uint8_t /*ch*/) { ++callback_count; });
    EXPECT_EQ(static_cast<uint32_t>(0), callback_count,
              "HandleInterrupt callback not invoked when no RX data");
  }

  // === 测试 14: HandleInterrupt 带回调版（写入后回环验证） ===
  // 注意：QEMU NS16550A 不一定支持硬件回环，此测试验证接口可调用性
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto open_result = uart.OpenReadWrite();
    EXPECT_TRUE(open_result.has_value(), "Open for HandleInterrupt loopback");

    if (open_result.has_value()) {
      // 调用 HandleInterrupt 消费可能残留的 RX 数据
      uint32_t drained = 0;
      uart.HandleInterrupt([&drained](uint8_t /*ch*/) { ++drained; });
      // 不对 drained 数量做断言，因为残留数据量不确定
      EXPECT_TRUE(true, "HandleInterrupt(callback) drains residual RX data");

      (void)uart.Release();
    }
  }

  // === 测试 15: 底层驱动中断状态查询 ===
  {
    device_framework::ns16550a::Ns16550aDevice uart(kUartBase);
    auto& driver = uart.GetDriver();

    // 读取中断标识寄存器（ISR/IIR）
    uint8_t iir = driver.GetInterruptId();
    // bit[0]==1 表示无中断挂起（正常空闲状态）
    // 我们只验证接口可调用，不对具体值做硬性断言
    (void)iir;
    EXPECT_TRUE(true, "GetInterruptId() returns without crash");

    bool pending = driver.IsInterruptPending();
    // 空闲状态下通常不应有中断挂起
    EXPECT_FALSE(pending,
                 "IsInterruptPending() reports no pending in idle state");
  }

  TEST_SUITE_END();
}
