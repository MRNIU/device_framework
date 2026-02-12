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
static constexpr uint32_t kUartRegRbr = 0;  // Receiver Buffer Register (读)
static constexpr uint32_t kUartRegThr = 0;  // Transmitter Holding Register (写)
static constexpr uint32_t kUartRegIer = 1;  // Interrupt Enable Register
static constexpr uint32_t kUartRegFcr = 2;  // FIFO Control Register (写)
static constexpr uint32_t kUartRegIsr = 2;  // Interrupt Status Register (读)
static constexpr uint32_t kUartRegLcr = 3;  // Line Control Register
static constexpr uint32_t kUartRegMcr = 4;  // Modem Control Register
static constexpr uint32_t kUartRegLsr = 5;  // Line Status Register
static constexpr uint32_t kUartRegMsr = 6;  // Modem Status Register
static constexpr uint32_t kUartRegScr = 7;  // Scratch Register

/**
 * @brief Interrupt Enable Register 位定义
 */
static constexpr uint8_t kUartIerRda = (1 << 0);  // Received Data Available
static constexpr uint8_t kUartIerThre =
    (1 << 1);  // Transmitter Holding Register Empty

/**
 * @brief Line Status Register 位定义
 */
static constexpr uint8_t kUartLsrDr = (1 << 0);  // Data Ready
static constexpr uint8_t kUartLsrThre =
    (1 << 5);  // Transmitter Holding Register Empty

/**
 * @brief 读 UART 寄存器
 */
static inline auto uart_read_reg(uint32_t reg) -> uint8_t {
  volatile auto *uart = reinterpret_cast<volatile uint8_t *>(kUart0Base);
  return uart[reg];
}

/**
 * @brief 写 UART 寄存器
 */
static inline void uart_write_reg(uint32_t reg, uint8_t val) {
  volatile auto *uart = reinterpret_cast<volatile uint8_t *>(kUart0Base);
  uart[reg] = val;
}

void uart_putc(char c) {
  while ((uart_read_reg(kUartRegLsr) & kUartLsrThre) == 0) {
  }

  uart_write_reg(kUartRegThr, static_cast<uint8_t>(c));

  if (c == '\n') {
    while ((uart_read_reg(kUartRegLsr) & kUartLsrThre) == 0) {
    }
    uart_write_reg(kUartRegThr, '\r');
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

void uart_init() { uart_write_reg(kUartRegIer, kUartIerRda); }

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
  if ((uart_read_reg(kUartRegLsr) & kUartLsrDr) != 0) {
    return uart_read_reg(kUartRegRbr);
  }
  return -1;
}

void uart_handle_interrupt() {
  uint8_t isr = uart_read_reg(kUartRegIsr);

  if ((isr & 0x01) == 0) {
    int c;
    while ((c = uart_getc()) != -1) {
      uart_putc(static_cast<char>(c));
      if (c == '\r') {
        uart_putc('\n');
      }
    }
  }
}
