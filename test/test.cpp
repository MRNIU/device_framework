/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include "test.h"

#include "uart.h"

// 全局测试统计
TestStats g_test_stats = {0, 0, 0};

void test_framework_init() {
  g_test_stats.total = 0;
  g_test_stats.passed = 0;
  g_test_stats.failed = 0;
}

void test_framework_print_summary() {
  uart_puts("\n");
  uart_puts("========================================\n");
  uart_puts("         Test Summary\n");
  uart_puts("========================================\n");
  uart_puts("Total tests: ");
  uart_put_hex(g_test_stats.total);
  uart_puts("\n");
  uart_puts("Passed: ");
  uart_put_hex(g_test_stats.passed);
  uart_puts("\n");
  uart_puts("Failed: ");
  uart_put_hex(g_test_stats.failed);
  uart_puts("\n");

  if (g_test_stats.failed == 0) {
    uart_puts("========================================\n");
    uart_puts("    ✓ ALL TESTS PASSED!\n");
    uart_puts("========================================\n");
  } else {
    uart_puts("========================================\n");
    uart_puts("    ✗ SOME TESTS FAILED\n");
    uart_puts("========================================\n");
  }
}
