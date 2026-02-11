/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include "test.h"

#include <cstddef>

#include "uart.h"

// Freestanding 环境需要的编译器内建函数
// GCC/Clang 优化器在处理大型结构体拷贝时可能生成 memcpy/memset 调用
extern "C" {
void* memcpy(void* dest, const void* src, size_t n) {
  auto* d = static_cast<uint8_t*>(dest);
  const auto* s = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
  return dest;
}

void* memset(void* dest, int c, size_t n) {
  auto* d = static_cast<uint8_t*>(dest);
  for (size_t i = 0; i < n; ++i) {
    d[i] = static_cast<uint8_t>(c);
  }
  return dest;
}
}

// 全局测试统计
TestStats g_test_stats = {0, 0, 0};

// VirtIO 设备中断回调表
VirtioIrqHandler g_virtio_irq_handlers[8] = {};

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
