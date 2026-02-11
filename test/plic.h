/**
 * @file plic.h
 * @brief PLIC (Platform-Level Interrupt Controller) 支持
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_TESTS_PLIC_H_
#define VIRTIO_DRIVER_TESTS_PLIC_H_

#include <cstdint>

// QEMU virt 机器的 PLIC 基地址
constexpr uint64_t PLIC_BASE = 0x0C000000;

// PLIC 寄存器偏移
constexpr uint64_t PLIC_PRIORITY_BASE = 0x0;    // 中断优先级基址
constexpr uint64_t PLIC_PENDING_BASE = 0x1000;  // 中断挂起基址
constexpr uint64_t PLIC_ENABLE_BASE = 0x2000;  // 中断使能基址 (contexr 0)
constexpr uint64_t PLIC_THRESHOLD = 0x200000;  // 优先级阈值 (context 0)
constexpr uint64_t PLIC_CLAIM = 0x200004;  // 中断声明/完成 (context 0)

// VirtIO 设备的 IRQ 编号 (QEMU virt 机器)
// VirtIO MMIO 设备的 IRQ 从 1 开始，共 8 个设备 (1-8)
constexpr uint32_t VIRTIO0_IRQ = 1;  // 第一个 VirtIO 设备
constexpr uint32_t VIRTIO1_IRQ = 2;
constexpr uint32_t VIRTIO2_IRQ = 3;
constexpr uint32_t VIRTIO3_IRQ = 4;
constexpr uint32_t VIRTIO4_IRQ = 5;
constexpr uint32_t VIRTIO5_IRQ = 6;
constexpr uint32_t VIRTIO6_IRQ = 7;
constexpr uint32_t VIRTIO7_IRQ = 8;

// UART IRQ
constexpr uint32_t UART0_IRQ = 10;

/**
 * @brief 初始化 PLIC
 */
inline void plic_init() {
  // 设置所有 VirtIO 设备的中断优先级为 1
  for (uint32_t irq = VIRTIO0_IRQ; irq <= VIRTIO7_IRQ; irq++) {
    volatile uint32_t* priority =
        reinterpret_cast<uint32_t*>(PLIC_BASE + PLIC_PRIORITY_BASE + irq * 4);
    *priority = 1;
  }

  // 设置 UART 中断优先级为 1
  volatile uint32_t* uart_priority = reinterpret_cast<uint32_t*>(
      PLIC_BASE + PLIC_PRIORITY_BASE + UART0_IRQ * 4);
  *uart_priority = 1;

  // 使能 VirtIO 设备和 UART 的中断 (context 1 = S-mode hart 0)
  // Context 0 是 M-mode，Context 1 是 S-mode
  // 每个 bit 代表一个中断源
  volatile uint32_t* enable =
      reinterpret_cast<uint32_t*>(PLIC_BASE + PLIC_ENABLE_BASE + 0x80);
  // 使能 IRQ 1-8 (VirtIO 设备) 和 IRQ 10 (UART)
  *enable = (0xFF << 1) | (1 << 10);  // bit 1-8, 10

  // 设置优先级阈值为 0 (接受所有优先级 > 0 的中断) - S-mode context
  volatile uint32_t* threshold =
      reinterpret_cast<uint32_t*>(PLIC_BASE + PLIC_THRESHOLD + 0x1000);
  *threshold = 0;
}

/**
 * @brief 声明并获取挂起的中断
 * @return 中断号，0 表示没有中断
 */
inline uint32_t plic_claim() {
  // S-mode context (context 1)
  volatile uint32_t* claim =
      reinterpret_cast<uint32_t*>(PLIC_BASE + PLIC_CLAIM + 0x1000);
  return *claim;
}

/**
 * @brief 完成中断处理
 * @param irq 中断号
 */
inline void plic_complete(uint32_t irq) {
  // S-mode context (context 1)
  volatile uint32_t* claim =
      reinterpret_cast<uint32_t*>(PLIC_BASE + PLIC_CLAIM + 0x1000);
  *claim = irq;
}

/**
 * @brief 处理 PLIC 外部中断
 */
inline void plic_handle_interrupt() {
  uint32_t irq = plic_claim();
  if (irq == 0) {
    return;  // 没有中断
  }

  // TODO: 根据 IRQ 号调用对应的设备中断处理函数
  // 这里可以添加一个中断处理函数表

  // 完成中断处理
  plic_complete(irq);
}

#endif  // VIRTIO_DRIVER_TESTS_PLIC_H_
