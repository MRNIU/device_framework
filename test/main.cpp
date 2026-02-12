/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include <cstdint>

#include "plic.h"
#include "test.h"
#include "uart.h"

/**
 * @brief 主测试入口
 */
void test_main(uint32_t hart_id, uint8_t* dtb) {
  uart_puts("\n╔════════════════════════════════════════╗\n");
  uart_puts("║   VirtIO Driver Test Environment      ║\n");
  uart_puts("╚════════════════════════════════════════╝\n");
  uart_puts("Hart ID: ");
  uart_put_hex(hart_id);
  uart_puts("\nDTB Address: ");
  uart_put_hex(reinterpret_cast<uint64_t>(dtb));
  uart_puts("\n");

  uart_puts("\n[INIT] Initializing UART...\n");
  uart_init();
  uart_puts("[INIT] UART initialized with interrupt support\n");

  uart_puts("\n[INIT] Initializing PLIC...\n");
  plic_init();
  uart_puts("[INIT] PLIC initialized\n");

  // 全局中断已在 boot.S 中启用
  uart_puts("[INIT] Interrupts enabled\n");

  uart_puts("\nTest: Hello from C++!\n");
  uart_puts("Test: UART is working!\n");
  uart_puts("Test: Interrupt system ready!\n");

  uart_puts("\n[SUCCESS] All initialization completed!\n");
  uart_puts("========================================\n\n");

  test_virtio_mmio_device_status();
  test_virtio_blk();

  uart_puts("[INFO] System ready. Try typing on the console...\n");
}

extern "C" void _start(uint32_t hart_id, uint8_t* dtb) {
  test_main(hart_id, dtb);

  uart_puts("[INFO] Entering infinite loop...\n");
  while (true) {
    asm volatile("wfi");
  }
}
