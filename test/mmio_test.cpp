/**
 * @file mmio_test.cpp
 * @brief VirtIO MMIO 传输层设备状态测试
 * @copyright Copyright The device_framework Contributors
 */

#include "device_framework/detail/virtio/transport/mmio.hpp"

#include <cstdint>

#include "device_framework/detail/virtio/device/device_initializer.hpp"
#include "device_framework/detail/virtio/traits.hpp"
#include "test.h"
#include "test_env.h"

void test_virtio_mmio_device_status() {
  TEST_SUITE_BEGIN("VirtIO MMIO Device Status");

  LOG("Scanning VirtIO MMIO devices...");

  int devices_found = 0;

  for (int i = 0; i < kMaxVirtioDevices; ++i) {
    uint64_t base = kVirtioMmioBase + i * kVirtioMmioSize;

    LOG_HEX("Probing device at address", base);

    auto magic = *reinterpret_cast<volatile uint32_t*>(base + 0x000);

    if (magic != device_framework::virtio::kMmioMagicValue) {
      LOG("  No device found (invalid magic)");
      continue;
    }

    auto device_id = *reinterpret_cast<volatile uint32_t*>(
        base + device_framework::virtio::MmioTransport<>::MmioReg::kDeviceId);
    if (device_id == 0) {
      LOG("  Empty VirtIO MMIO slot (Device ID = 0)");
      continue;
    }

    LOG("  Found valid VirtIO device!");
    devices_found++;

    device_framework::virtio::MmioTransport<RiscvTraits> transport(base);

    // 测试 0: 验证传输层初始化成功
    {
      bool is_valid = transport.IsValid();
      EXPECT_TRUE(is_valid, "Transport should be valid after construction");
      if (!is_valid) {
        LOG("  Transport initialization failed, skipping device");
        continue;
      }
    }

    // 测试 1: 验证魔数正确
    {
      auto magic_value = *reinterpret_cast<volatile uint32_t*>(
          base + transport.MmioReg::kMagicValue);
      EXPECT_EQ(device_framework::virtio::kMmioMagicValue, magic_value,
                "Magic value should be 0x74726976");
    }

    // 测试 2: 验证版本号
    {
      auto version = *reinterpret_cast<volatile uint32_t*>(
          base + transport.MmioReg::kVersion);
      EXPECT_EQ(device_framework::virtio::kMmioVersionModern, version,
                "Version should be 2 (modern)");
      LOG_HEX("  Version", version);
    }

    // 测试 3: 读取设备 ID 并识别设备类型
    {
      auto dev_id = transport.GetDeviceId();
      LOG_HEX("  Device ID", dev_id);

      switch (dev_id) {
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

      transport.SetStatus(0x01);
      auto status_after_ack = transport.GetStatus();
      EXPECT_EQ(0x01U, status_after_ack,
                "Status should be 0x01 after ACKNOWLEDGE");

      transport.SetStatus(0x01 | 0x02);
      auto status_after_driver = transport.GetStatus();
      EXPECT_EQ(0x03U, status_after_driver,
                "Status should be 0x03 after DRIVER");

      transport.SetStatus(0x00);
      auto status_after_reset = transport.GetStatus();
      EXPECT_EQ(0x00U, status_after_reset, "Status should be 0 after reset");
    }

    // 测试 7: 读取设备特性（64 位）
    {
      auto features = transport.GetDeviceFeatures();
      LOG_HEX("  Device features (low 32)", static_cast<uint32_t>(features));
      LOG_HEX("  Device features (high 32)",
              static_cast<uint32_t>(features >> 32));
    }

    // 测试 8: 获取队列最大容量
    {
      auto queue_max = transport.GetQueueNumMax(0);
      LOG_HEX("  Queue 0 max size", queue_max);
    }
  }

  EXPECT_TRUE(devices_found > 0, "Should find at least one VirtIO MMIO device");
  LOG_HEX("Total devices found", devices_found);

  TEST_SUITE_END();
}
