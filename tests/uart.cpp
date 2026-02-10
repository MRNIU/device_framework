/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include "uart.h"

#include <array>

/**
 * @brief UART 寄存器偏移
 */
#define UART_REG_RBR 0  // Receiver Buffer Register (读)
#define UART_REG_THR 0  // Transmitter Holding Register (写)
#define UART_REG_IER 1  // Interrupt Enable Register
#define UART_REG_FCR 2  // FIFO Control Register (写)
#define UART_REG_ISR 2  // Interrupt Status Register (读)
#define UART_REG_LCR 3  // Line Control Register
#define UART_REG_MCR 4  // Modem Control Register
#define UART_REG_LSR 5  // Line Status Register
#define UART_REG_MSR 6  // Modem Status Register
#define UART_REG_SCR 7  // Scratch Register

/**
 * @brief Interrupt Enable Register 位定义
 */
#define UART_IER_RDA (1 << 0)   // Received Data Available
#define UART_IER_THRE (1 << 1)  // Transmitter Holding Register Empty

/**
 * @brief Line Status Register 位定义
 */
#define UART_LSR_DR (1 << 0)    // Data Ready
#define UART_LSR_THRE (1 << 5)  // Transmitter Holding Register Empty

/**
 * @brief 读 UART 寄存器
 */
static inline auto uart_read_reg(uint32_t reg) -> uint8_t {
  volatile auto *uart = reinterpret_cast<volatile uint8_t *>(UART0_BASE);
  return uart[reg];
}

/**
 * @brief 写 UART 寄存器
 */
static inline void uart_write_reg(uint32_t reg, uint8_t val) {
  volatile auto *uart = reinterpret_cast<volatile uint8_t *>(UART0_BASE);
  uart[reg] = val;
}

void uart_putc(char c) {
  // 等待发送缓冲区为空
  while ((uart_read_reg(UART_REG_LSR) & UART_LSR_THRE) == 0) {
    // 忙等待
  }

  // 发送字符
  uart_write_reg(UART_REG_THR, static_cast<uint8_t>(c));

  // 如果是换行符，同时发送回车符
  if (c == '\n') {
    while ((uart_read_reg(UART_REG_LSR) & UART_LSR_THRE) == 0) {
      // 忙等待
    }
    uart_write_reg(UART_REG_THR, '\r');
  }
}

void uart_puts(const char *str) {
  if (str == nullptr) {
    return;
  }

  while (*str != 0U) {
    uart_putc(*str);
    str++;
  }
}

void uart_put_hex(uint64_t num) {
  std::array<char, 17> hex_chars = {"0123456789abcdef"};
  std::array<char, 19> buf;  // "0x" + 16 hex digits + null
  int i;

  buf[0] = '0';
  buf[1] = 'x';

  for (i = 15; i >= 0; i--) {
    buf[2 + i] = hex_chars[num & 0xF];
    num >>= 4;
  }

  buf[18] = '\0';
  uart_puts(buf.data());
}

void uart_init() {
  // 使能接收数据中断
  uart_write_reg(UART_REG_IER, UART_IER_RDA);
}

auto uart_getc() -> int {
  // 检查是否有可用数据
  if ((uart_read_reg(UART_REG_LSR) & UART_LSR_DR) != 0) {
    return uart_read_reg(UART_REG_RBR);
  }
  return -1;
}

void uart_handle_interrupt() {
  // 读取中断状态寄存器
  uint8_t isr = uart_read_reg(UART_REG_ISR);

  // 检查是否是接收数据中断 (ISR bit 0 = 0 表示有中断挂起)
  if ((isr & 0x01) == 0) {
    // 处理接收数据中断
    int c;
    while ((c = uart_getc()) != -1) {
      // 回显字符
      uart_putc(static_cast<char>(c));

      // 可以在这里处理特殊字符
      if (c == '\r') {
        uart_putc('\n');
      }
    }
  }
}
