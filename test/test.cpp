/**
 * @file test.cpp
 * @brief 测试框架实现：全局统计、横幅与摘要
 * @copyright Copyright The device_framework Contributors
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

TestStats g_suite_stats = {0, 0, 0};
TestStats g_global_stats = {0, 0, 0};

VirtioIrqHandler g_virtio_irq_handlers[8] = {};

void test_print_banner() {
  uart_puts("\n");
  uart_puts(
      "================================================================\n");
  uart_puts("  device_framework Test Suite\n");
  uart_puts(
      "================================================================\n");
}

void test_print_summary() {
  uart_puts("\n");
  uart_puts(
      "================================================================\n");
  uart_puts("  Total: ");
  uart_put_dec(g_global_stats.passed);
  uart_putc('/');
  uart_put_dec(g_global_stats.total);
  uart_puts(" passed");

  if (g_global_stats.failed == 0) {
    uart_puts(" - ALL TESTS PASSED\n");
  } else {
    uart_puts(" - ");
    uart_put_dec(g_global_stats.failed);
    uart_puts(" FAILED\n");
  }

  uart_puts(
      "================================================================\n");
}
