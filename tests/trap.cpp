/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include <cstdint>

#include "uart.h"

/**
 * @brief Trap handler - 处理异常和中断
 * @param cause 异常/中断原因
 * @param epc 异常发生时的 PC
 * @param tval 异常相关的值
 */
void trap_handler(uint64_t cause, uint64_t epc, uint64_t tval) {
  uart_puts("\n[TRAP] Unexpected trap!\n");
  uart_puts("  cause: ");
  uart_put_hex(cause);
  uart_puts("\n  epc: ");
  uart_put_hex(epc);
  uart_puts("\n  tval: ");
  uart_put_hex(tval);
  uart_puts("\n");

  // 死循环
  while (true) {
    asm volatile("wfi");
  }
}
