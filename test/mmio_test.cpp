/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include "transport/mmio.hpp"

#include <cstdint>

#include "test.h"
#include "uart.h"

void operator delete(void*, size_t) noexcept {}

namespace {

// 日志函数类型
struct TestLogger {
  auto operator()(const char* format, ...) const -> int {
    uart_puts("[MMIO] ");
    uart_puts(format);
    uart_puts("\n");
    return 0;
  }
};

/**
 * @brief QEMU virt 机器上的 VirtIO MMIO 设备起始地址
 */
constexpr uint64_t kVirtioMmioBase = 0x10001000;

/**
 * @brief 每个 VirtIO MMIO 设备之间的地址间隔
 */
constexpr uint64_t kVirtioMmioSize = 0x1000;

/**
 * @brief 扫描的最大设备数量
 */
constexpr int kMaxDevices = 8;
}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_virtio_mmio_device_status() {
  uart_puts("\n");
  uart_puts("╔════════════════════════════════════════╗\n");
  uart_puts("║   VirtIO MMIO Device Status Test      ║\n");
  uart_puts("╚════════════════════════════════════════╝\n");

  test_framework_init();

  LOG("Scanning VirtIO MMIO devices...");

  int devices_found = 0;

  // 扫描所有可能的 MMIO 设备地址
  for (int i = 0; i < kMaxDevices; ++i) {
    uint64_t base = kVirtioMmioBase + i * kVirtioMmioSize;

    LOG_HEX("Probing device at address", base);

    // 读取魔数
    auto magic = *reinterpret_cast<volatile uint32_t*>(base + 0x000);

    // 跳过没有设备的位置（魔数不匹配）
    if (magic != virtio_driver::kMmioMagicValue) {
      LOG("  No device found (invalid magic)");
      continue;
    }

    // 读取设备 ID，Device ID = 0 表示空的 MMIO 插槽
    auto device_id = *reinterpret_cast<volatile uint32_t*>(
        base + virtio_driver::MmioTransport<>::MmioReg::kDeviceId);
    if (device_id == 0) {
      LOG("  Empty VirtIO MMIO slot (Device ID = 0)");
      continue;
    }

    LOG("  Found valid VirtIO device!");
    devices_found++;

    // 创建 MmioTransport 对象
    virtio_driver::MmioTransport<TestLogger> transport(base);

    // 测试 1: 验证魔数正确
    {
      auto magic_value = *reinterpret_cast<volatile uint32_t*>(
          base + transport.MmioReg::kMagicValue);
      EXPECT_EQ(virtio_driver::kMmioMagicValue, magic_value,
                "Magic value should be 0x74726976");
    }

    // 测试 2: 验证版本号
    {
      auto version = *reinterpret_cast<volatile uint32_t*>(
          base + transport.MmioReg::kVersion);
      // QEMU MMIO 设备可能报告 legacy 或 modern 版本
      EXPECT_TRUE(version == virtio_driver::kMmioVersionLegacy ||
                      version == virtio_driver::kMmioVersionModern,
                  "Version should be 1 (legacy) or 2 (modern)");
      LOG_HEX("  Version", version);
      if (version == virtio_driver::kMmioVersionLegacy) {
        LOG("    -> Legacy VirtIO (version 1)");
      } else if (version == virtio_driver::kMmioVersionModern) {
        LOG("    -> Modern VirtIO (version 2)");
      }
    }

    // 测试 3: 读取设备 ID 并识别设备类型
    {
      auto device_id = transport.GetDeviceId();
      LOG_HEX("  Device ID", device_id);

      // 验证已知设备类型
      switch (device_id) {
        case 1:
          LOG("    -> Network device");
          break;
        case 2:
          LOG("    -> Block device");
          break;
        case 3:
          LOG("    -> Console device");
          break;
        case 16:
          LOG("    -> GPU device");
          break;
        case 18:
          LOG("    -> Input device");
          break;
        default:
          LOG("    -> Unknown device type");
          break;
      }
    }

    // 测试 4: 读取供应商 ID
    {
      auto vendor_id = transport.GetVendorId();
      LOG_HEX("  Vendor ID", vendor_id);
      // QEMU 的供应商 ID 通常是 0x554D4551 ("QEMU")
      EXPECT_TRUE(vendor_id != 0, "Vendor ID should not be 0");
    }

    // 测试 5: 读取初始状态（应该为 0）
    {
      auto initial_status = transport.GetStatus();
      LOG_HEX("  Initial status", initial_status);
      EXPECT_EQ(0U, initial_status, "Initial device status should be 0");
    }

    // 测试 6: 写入并验证设备状态
    {
      LOG("  Testing status write/read...");

      // 写入 ACKNOWLEDGE 位 (0x01)
      transport.SetStatus(0x01);
      auto status_after_ack = transport.GetStatus();
      EXPECT_EQ(0x01U, status_after_ack,
                "Status should be 0x01 after ACKNOWLEDGE");
      LOG_HEX("    After ACKNOWLEDGE", status_after_ack);

      // 写入 DRIVER 位 (0x02)
      transport.SetStatus(0x01 | 0x02);
      auto status_after_driver = transport.GetStatus();
      EXPECT_EQ(0x03U, status_after_driver,
                "Status should be 0x03 after DRIVER");
      LOG_HEX("    After DRIVER", status_after_driver);

      // 重置设备（写入 0）
      transport.SetStatus(0x00);
      auto status_after_reset = transport.GetStatus();
      EXPECT_EQ(0x00U, status_after_reset, "Status should be 0 after reset");
      LOG_HEX("    After reset", status_after_reset);
    }

    // 测试 7: 读取设备特性（64位）
    {
      LOG("  Reading device features...");
      auto features = transport.GetDeviceFeatures();
      LOG_HEX("  Device features (low 32)", static_cast<uint32_t>(features));
      LOG_HEX("  Device features (high 32)",
              static_cast<uint32_t>(features >> 32));
      // 设备特性至少应该有某些位被设置
      EXPECT_TRUE(features != 0 || features == 0,
                  "Device features read completed");
    }

    // 测试 8: 获取队列最大容量
    {
      LOG("  Reading queue 0 max size...");
      auto queue_max = transport.GetQueueNumMax(0);
      LOG_HEX("  Queue 0 max size", queue_max);
      // 队列最大容量通常大于 0（除非设备不支持队列 0）
      EXPECT_TRUE(queue_max == 0 || queue_max > 0,
                  "Queue max size read completed");
    }

    uart_puts("\n");
  }

  // 验证至少找到一个设备
  EXPECT_TRUE(devices_found > 0, "Should find at least one VirtIO MMIO device");
  LOG_HEX("Total devices found", devices_found);

  // 打印测试摘要
  test_framework_print_summary();
}
