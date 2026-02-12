/**
 * @file ns16550a_test.cpp
 * @brief NS16550A 字符设备统一接口测试
 * @copyright Copyright The device_framework Contributors
 *
 * 测试 NS16550A 通过统一 CharDevice 接口的操作：
 * Open/PutChar/Write/Poll/Release 及错误路径
 */

#include "device_framework/driver/ns16550a/ns16550a_device.hpp"

#include <cstdint>

#include "test.h"
#include "uart.h"

namespace {

/// QEMU virt 机器第一个 UART 的 MMIO 基地址
/// 注意：0x10000000 已被 boot 阶段的 UART 占用，
/// 这里使用相同地址来验证 CharDevice 接口逻辑
constexpr uint64_t kUartBase = 0x10000000;

}  // namespace

void test_ns16550a() {
  uart_puts("\n");
  uart_puts("╔════════════════════════════════════════╗\n");
  uart_puts("║   NS16550A CharDevice Interface Test   ║\n");
  uart_puts("╚════════════════════════════════════════╝\n");

  test_framework_init();

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
        EXPECT_EQ(
            static_cast<uint32_t>(
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
        EXPECT_EQ(
            static_cast<uint32_t>(
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
      auto poll_result =
          uart.Poll(device_framework::PollEvents{
              device_framework::PollEvents::kOut});
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
    EXPECT_TRUE(second_open.has_value(),
                "Re-open after Release() succeeds");
    if (second_open.has_value()) {
      (void)uart.Release();
    }
  }

  test_framework_print_summary();
}
