/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include <cstdint>

#include "plic.h"
#include "test.h"
#include "uart.h"

/**
 * @brief Trap handler - 处理异常和中断
 * @param cause 异常/中断原因
 * @param epc 异常发生时的 PC
 * @param tval 异常相关的值
 */
extern "C" void trap_handler(uint64_t cause, uint64_t epc, uint64_t tval) {
  // 判断是中断还是异常
  bool is_interrupt = (cause & (1ULL << 63)) != 0;
  uint64_t code = cause & 0x7FFFFFFFFFFFFFFF;

  if (is_interrupt) {
    // 处理中断
    uart_puts("\n[INTERRUPT] ");
    switch (code) {
      case 1:  // Supervisor 软件中断
        uart_puts("Supervisor Software Interrupt\n");
        break;
      case 5:  // Supervisor 定时器中断
        uart_puts("Supervisor Timer Interrupt\n");
        // 清除定时器中断 (通过 SBI 调用)
        break;
      case 9: {  // Supervisor 外部中断
        uart_puts("Supervisor External Interrupt\n");
        // 通过 PLIC 处理外部中断
        uint32_t irq = plic_claim();
        if (irq != 0) {
          uart_puts("  IRQ: ");
          uart_put_hex(irq);
          uart_puts("\n");

          // 根据 IRQ 处理不同的设备中断
          if (irq >= kVirtio0Irq && irq <= kVirtio7Irq) {
            uint32_t dev_idx = irq - kVirtio0Irq;
            uart_puts("  Device: VirtIO");
            uart_put_hex(dev_idx);
            uart_puts("\n");
            // 调用已注册的 VirtIO 设备中断处理函数
            if (g_virtio_irq_handlers[dev_idx] != nullptr) {
              g_virtio_irq_handlers[dev_idx]();
            }
          } else if (irq == kUart0Irq) {
            uart_puts("  Device: UART\n");
            uart_handle_interrupt();
          } else {
            uart_puts("  Device: Unknown\n");
          }

          // 完成中断处理
          plic_complete(irq);
        }
        break;
      }
      default:
        uart_puts("Unknown Interrupt (code: ");
        uart_put_hex(code);
        uart_puts(")\n");
        break;
    }
  } else {
    // 处理异常
    uart_puts("\n[EXCEPTION] Unexpected exception!\n");
    uart_puts("  code: ");
    uart_put_hex(code);

    // 打印异常类型
    uart_puts(" (");
    switch (code) {
      case 0:
        uart_puts("Instruction address misaligned");
        break;
      case 1:
        uart_puts("Instruction access fault");
        break;
      case 2:
        uart_puts("Illegal instruction");
        break;
      case 3:
        uart_puts("Breakpoint");
        break;
      case 4:
        uart_puts("Load address misaligned");
        break;
      case 5:
        uart_puts("Load access fault");
        break;
      case 6:
        uart_puts("Store/AMO address misaligned");
        break;
      case 7:
        uart_puts("Store/AMO access fault");
        break;
      case 8:
        uart_puts("Environment call from U-mode");
        break;
      case 9:
        uart_puts("Environment call from S-mode");
        break;
      case 11:
        uart_puts("Environment call from M-mode");
        break;
      case 12:
        uart_puts("Instruction page fault");
        break;
      case 13:
        uart_puts("Load page fault");
        break;
      case 15:
        uart_puts("Store/AMO page fault");
        break;
      default:
        uart_puts("Unknown exception");
        break;
    }
    uart_puts(")\n");

    uart_puts("  epc: ");
    uart_put_hex(epc);
    uart_puts("\n  tval: ");
    uart_put_hex(tval);
    uart_puts("\n");

    // 对于异常，进入死循环
    uart_puts("\n[FATAL] System halted due to exception.\n");
    while (true) {
      asm volatile("wfi");
    }
  }
}
