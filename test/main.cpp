/**
 * @file main.cpp
 * @brief 测试主入口
 * @copyright Copyright The device_framework Contributors
 */

#include <cstdint>

#include "plic.h"
#include "test.h"
#include "uart.h"

/**
 * @brief 主测试入口
 */
void test_main(uint32_t hart_id, uint8_t* dtb) {
  (void)hart_id;
  (void)dtb;

  uart_init();
  plic_init();

  test_print_banner();

  test_ns16550a();
  test_virtio_mmio_device_status();
  test_virtio_blk();
  test_virtio_blk_device();

  test_print_summary();
}

extern "C" void _start(uint32_t hart_id, uint8_t* dtb) {
  test_main(hart_id, dtb);

  while (true) {
    asm volatile("wfi");
  }
}
