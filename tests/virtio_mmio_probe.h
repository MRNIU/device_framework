/**
 * @file virtio_mmio_probe.h
 * @brief VirtIO MMIO 设备探测和检测
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_TESTS_VIRTIO_MMIO_PROBE_H_
#define VIRTIO_DRIVER_TESTS_VIRTIO_MMIO_PROBE_H_

#include <cstdint>

// QEMU virt 机器的 VirtIO MMIO 设备地址范围
constexpr uint64_t VIRTIO_MMIO_BASE = 0x10001000;
constexpr uint64_t VIRTIO_MMIO_SIZE = 0x1000;  // 每个设备 4KB
constexpr uint32_t VIRTIO_MMIO_MAX_DEVICES = 8;

// VirtIO MMIO 寄存器偏移
constexpr uint32_t MMIO_MAGIC_VALUE = 0x000;
constexpr uint32_t MMIO_VERSION = 0x004;
constexpr uint32_t MMIO_DEVICE_ID = 0x008;
constexpr uint32_t MMIO_VENDOR_ID = 0x00C;

// VirtIO MMIO 魔数: "virt" = 0x74726976
constexpr uint32_t VIRTIO_MMIO_MAGIC = 0x74726976;

/**
 * @brief VirtIO 设备信息
 */
struct VirtioDeviceInfo {
  uint64_t base_addr;  // 设备基地址
  uint32_t device_id;  // 设备 ID
  uint32_t vendor_id;  // 供应商 ID
  uint32_t version;    // 版本
  uint32_t irq;        // 中断号
};

/**
 * @brief 读取 MMIO 寄存器
 */
inline auto mmio_read32(uint64_t base, uint32_t offset) -> uint32_t {
  volatile uint32_t* addr = reinterpret_cast<uint32_t*>(base + offset);
  return *addr;
}

/**
 * @brief 探测单个 VirtIO MMIO 设备
 */
inline auto probe_virtio_device(uint64_t base_addr, VirtioDeviceInfo* info)
    -> bool {
  // 读取魔数
  uint32_t magic = mmio_read32(base_addr, MMIO_MAGIC_VALUE);
  if (magic != VIRTIO_MMIO_MAGIC) {
    return false;
  }

  // 读取版本
  uint32_t version = mmio_read32(base_addr, MMIO_VERSION);
  if (version == 0) {
    return false;  // 版本 0 表示没有设备
  }

  // 读取设备 ID
  uint32_t device_id = mmio_read32(base_addr, MMIO_DEVICE_ID);
  if (device_id == 0) {
    return false;  // 设备 ID 0 表示无效设备
  }

  // 读取供应商 ID
  uint32_t vendor_id = mmio_read32(base_addr, MMIO_VENDOR_ID);

  // 填充设备信息
  if (info != nullptr) {
    info->base_addr = base_addr;
    info->device_id = device_id;
    info->vendor_id = vendor_id;
    info->version = version;
    // IRQ 号 = 1 + 设备索引 (在 QEMU virt 中)
    info->irq = 1 + ((base_addr - VIRTIO_MMIO_BASE) / VIRTIO_MMIO_SIZE);
  }

  return true;
}

/**
 * @brief 获取设备类型名称
 */
inline auto get_device_type_name(uint32_t device_id) -> const char* {
  switch (device_id) {
    case 1:
      return "Network";
    case 2:
      return "Block";
    case 3:
      return "Console";
    case 4:
      return "Entropy";
    case 5:
      return "Memory Balloon (Traditional)";
    case 16:
      return "GPU";
    case 18:
      return "Input";
    default:
      return "Unknown";
  }
}

/**
 * @brief 扫描所有 VirtIO MMIO 设备
 */
void scan_virtio_devices();

#endif  // VIRTIO_DRIVER_TESTS_VIRTIO_MMIO_PROBE_H_
