/**
 * @file test_env.h
 * @brief 测试环境：平台 Traits、共享常量与工具函数
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_TEST_TEST_ENV_H_
#define DEVICE_FRAMEWORK_TEST_TEST_ENV_H_

#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "uart.h"

/// @name 平台常量
/// @{

/// QEMU virt 机器 VirtIO MMIO 基地址
constexpr uint64_t kVirtioMmioBase = 0x10001000;
/// 每个 VirtIO MMIO 设备的地址间隔
constexpr uint64_t kVirtioMmioSize = 0x1000;
/// 最大扫描设备数
constexpr int kMaxVirtioDevices = 8;
/// VirtIO 块设备 Device ID
constexpr uint32_t kBlockDeviceId = 2;
/// 扇区大小
constexpr size_t kSectorSize = 512;
/// DMA 缓冲区大小
constexpr size_t kDmaBufSize = 32768;
/// 多扇区缓冲区的扇区数
constexpr size_t kMultiBufSectors = 4;

/// @}

/// @name RISC-V 平台 Traits
/// @{

/**
 * @brief RISC-V 平台环境 Traits 实现
 *
 * 满足 EnvironmentTraits + BarrierTraits + DmaTraits（VirtioTraits）
 */
struct RiscvTraits {
  static auto Log(const char* fmt, ...) -> int {
    uart_puts("  ");
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

/// @}

/// @name 共享缓冲区
/// @{

/// DMA 区域（4096 字节对齐）
extern uint8_t g_dma_buf[kDmaBufSize];
/// 单扇区数据缓冲区（16 字节对齐）
extern uint8_t g_data_buf[kSectorSize];
/// 多扇区数据缓冲区（16 字节对齐）
extern uint8_t g_multi_buf[kMultiBufSectors * kSectorSize];

/// @}

/// @name 工具函数
/// @{

/**
 * @brief 扫描 MMIO 设备，查找 VirtIO 块设备
 * @return 块设备 MMIO 基地址，未找到返回 0
 */
auto FindBlkDevice() -> uint64_t;

/**
 * @brief 清零内存区域
 * @param ptr  目标地址
 * @param len  字节数
 */
void Memzero(void* ptr, size_t len);

/// @}

#endif /* DEVICE_FRAMEWORK_TEST_TEST_ENV_H_ */
