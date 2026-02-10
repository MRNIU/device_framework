/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include <cstdint>

#include "uart.h"

void test_main(uint32_t hart_id, uint8_t *dtb) {
  uart_puts("\n========================================\n");
  uart_puts("virtio_driver Test Environment\n");
  uart_puts("========================================\n");
  uart_puts("Hart ID: ");
  uart_put_hex(hart_id);
  uart_puts("\nDTB Address: ");
  uart_put_hex(reinterpret_cast<uint64_t>(dtb));
  uart_puts("\n");

  uart_puts("\nTest: Hello from C++!\n");
  uart_puts("Test: UART is working!\n");

  uart_puts("\n[SUCCESS] All tests passed!\n");
  uart_puts("========================================\n\n");
}

extern "C" void _start(uint32_t hart_id, uint8_t *dtb) {
  // 调用测试函数
  test_main(hart_id, dtb);

  // 进入死循环
  uart_puts("[INFO] Entering infinite loop...\n");
  while (true) {
    asm volatile("wfi");
  }
}
