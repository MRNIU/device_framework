/**
 * @file test_env.cpp
 * @brief 测试环境实现：共享缓冲区、工具函数
 * @copyright Copyright The device_framework Contributors
 */

#include "test_env.h"

#include "device_framework/virtio_blk.hpp"

void operator delete(void*, size_t) noexcept {}

/// @name 共享缓冲区定义
/// @{

alignas(4096) uint8_t g_dma_buf[kDmaBufSize];
alignas(16) uint8_t g_data_buf[kSectorSize];
alignas(16) uint8_t g_multi_buf[kMultiBufSectors * kSectorSize];

/// @}

auto FindBlkDevice() -> uint64_t {
  for (int i = 0; i < kMaxVirtioDevices; ++i) {
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

void Memzero(void* ptr, size_t len) {
  auto* p = static_cast<volatile uint8_t*>(ptr);
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}
