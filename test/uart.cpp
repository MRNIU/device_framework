/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include "uart.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

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
  constexpr char hex_chars[] = "0123456789abcdef";
  char buf[19];  // "0x" + 16 hex digits + null

  buf[0] = '0';
  buf[1] = 'x';

  for (int i = 15; i >= 0; i--) {
    buf[2 + i] = hex_chars[num & 0xF];
    num >>= 4;
  }

  buf[18] = '\0';
  uart_puts(buf);
}

void uart_init() {
  // 使能接收数据中断
  uart_write_reg(UART_REG_IER, UART_IER_RDA);
}

void uart_put_dec(uint64_t num) {
  if (num == 0) {
    uart_putc('0');
    return;
  }

  char buf[21];  // uint64_t 最大 20 位十进制 + null
  int pos = 0;

  while (num > 0) {
    buf[pos++] = static_cast<char>('0' + (num % 10));
    num /= 10;
  }

  // 反转输出
  for (int i = pos - 1; i >= 0; --i) {
    uart_putc(buf[i]);
  }
}

/// 内部辅助：输出零填充的十六进制数（无前缀）
static void uart_put_hex_padded(uint64_t num, int width) {
  constexpr char hex_chars[] = "0123456789abcdef";
  char buf[17];  // 最多 16 位 hex
  if (width > 16) {
    width = 16;
  }

  for (int i = width - 1; i >= 0; --i) {
    buf[i] = hex_chars[num & 0xF];
    num >>= 4;
  }
  buf[width] = '\0';
  uart_puts(buf);
}

auto uart_printf(const char *format, ...) -> int {
  va_list args;
  va_start(args, format);
  int count = uart_vprintf(format, args);
  va_end(args);
  return count;
}

auto uart_vprintf(const char *format, va_list args) -> int {
  if (format == nullptr) {
    return 0;
  }

  int count = 0;
  const char *p = format;

  while (*p != '\0') {
    if (*p != '%') {
      uart_putc(*p);
      ++count;
      ++p;
      continue;
    }

    ++p;  // skip '%'

    // 解析宽度和零填充
    bool zero_pad = false;
    int width = 0;

    if (*p == '%') {
      uart_putc('%');
      ++count;
      ++p;
      continue;
    }

    if (*p == '0') {
      zero_pad = true;
      ++p;
    }

    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      ++p;
    }

    // 支持 'l' 和 'll' 长度修饰符
    int long_count = 0;
    while (*p == 'l') {
      ++long_count;
      ++p;
    }

    switch (*p) {
      case 's': {
        const char *s = va_arg(args, const char *);
        if (s == nullptr) {
          s = "(null)";
        }
        uart_puts(s);
        // 简化计数
        while (*s != '\0') {
          ++count;
          ++s;
        }
        break;
      }
      case 'd': {
        int64_t val;
        if (long_count >= 2) {
          val = va_arg(args, int64_t);
        } else if (long_count == 1) {
          val = va_arg(args, long);
        } else {
          val = va_arg(args, int);
        }
        if (val < 0) {
          uart_putc('-');
          ++count;
          val = -val;
        }
        uart_put_dec(static_cast<uint64_t>(val));
        break;
      }
      case 'u': {
        uint64_t val;
        if (long_count >= 2) {
          val = va_arg(args, uint64_t);
        } else if (long_count == 1) {
          val = va_arg(args, unsigned long);
        } else {
          val = va_arg(args, unsigned int);
        }
        uart_put_dec(val);
        break;
      }
      case 'x': {
        uint64_t val;
        if (long_count >= 2) {
          val = va_arg(args, uint64_t);
        } else if (long_count == 1) {
          val = va_arg(args, unsigned long);
        } else {
          val = va_arg(args, unsigned int);
        }
        if (zero_pad && width > 0) {
          uart_put_hex_padded(val, width);
        } else {
          // 无填充：找到最高非零 nibble
          if (val == 0) {
            uart_putc('0');
            ++count;
          } else {
            uart_put_hex_padded(val, 16);
          }
        }
        break;
      }
      case 'p': {
        auto val = reinterpret_cast<uint64_t>(va_arg(args, void *));
        uart_puts("0x");
        count += 2;
        uart_put_hex_padded(val, 16);
        break;
      }
      case 'c': {
        char c = static_cast<char>(va_arg(args, int));
        uart_putc(c);
        ++count;
        break;
      }
      default:
        // 未知格式，原样输出
        uart_putc('%');
        uart_putc(*p);
        count += 2;
        break;
    }

    ++p;
  }

  return count;
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
