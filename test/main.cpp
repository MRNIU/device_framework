/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include <cstdint>

#include "virtio_driver/device/virtio_blk.hpp"
#include "plic.h"
#include "test.h"
#include "uart.h"
// #include "virtio_blk_test_oo.hpp"  // TODO: 修复 blk 测试后启用

/**
 * @brief 主测试入口
 */
void test_main(uint32_t hart_id, uint8_t* dtb) {
  // 打印欢迎信息
  uart_puts("\n╔════════════════════════════════════════╗\n");
  uart_puts("║   VirtIO Driver Test Environment      ║\n");
  uart_puts("╚════════════════════════════════════════╝\n");
  uart_puts("Hart ID: ");
  uart_put_hex(hart_id);
  uart_puts("\nDTB Address: ");
  uart_put_hex(reinterpret_cast<uint64_t>(dtb));
  uart_puts("\n");

  // 初始化 UART（包含中断支持）
  uart_puts("\n[INIT] Initializing UART...\n");
  uart_init();
  uart_puts("[INIT] UART initialized with interrupt support\n");

  // 初始化 PLIC (平台级中断控制器)
  uart_puts("\n[INIT] Initializing PLIC...\n");
  plic_init();
  uart_puts("[INIT] PLIC initialized\n");

  // 使能机器模式全局中断（已在 boot.S 中设置）
  uart_puts("[INIT] Interrupts enabled\n");

  // 扫描 VirtIO MMIO 设备
  // scan_virtio_devices();

  uart_puts("\nTest: Hello from C++!\n");
  uart_puts("Test: UART is working!\n");
  uart_puts("Test: Interrupt system ready!\n");

  uart_puts("\n[SUCCESS] All initialization completed!\n");
  uart_puts("========================================\n\n");

  // 测试 VirtIO MMIO 设备状态读取
  test_virtio_mmio_device_status();

  // 测试 VirtIO 块设备
  test_virtio_blk();

  uart_puts("[INFO] System ready. Try typing on the console...\n");
}

extern "C" void _start(uint32_t hart_id, uint8_t* dtb) {
  // 调用测试函数
  test_main(hart_id, dtb);

  // 进入死循环
  uart_puts("[INFO] Entering infinite loop...\n");
  while (true) {
    asm volatile("wfi");
  }
}
