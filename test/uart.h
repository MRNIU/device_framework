/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_TEST_UART_H_
#define DEVICE_FRAMEWORK_TEST_UART_H_

#include <cstdarg>
#include <cstdint>

/**
 * @brief UART 基地址 (QEMU virt machine)
 */
constexpr uint64_t kUart0Base = 0x10000000UL;

/**
 * @brief 初始化 UART
 */
void uart_init();

/**
 * @brief 输出一个字符到 UART
 * @param c 要输出的字符
 */
void uart_putc(char c);

/**
 * @brief 输出字符串到 UART
 * @param str 要输出的字符串
 */
void uart_puts(const char *str);

/**
 * @brief 输出十六进制数到 UART
 * @param num 要输出的数字
 */
void uart_put_hex(uint64_t num);

/**
 * @brief 输出十进制无符号整数到 UART
 * @param num 要输出的数字
 */
void uart_put_dec(uint64_t num);

/**
 * @brief 简易格式化输出到 UART
 *
 * 支持的格式说明符：
 * - %s: 字符串
 * - %d: 有符号十进制整数
 * - %u: 无符号十进制整数
 * - %x: 十六进制（无前缀）
 * - %p: 指针（0x 前缀十六进制）
 * - %08x: 8 位零填充十六进制
 * - %%: 字面 %
 *
 * @param format 格式化字符串
 * @param ... 可变参数
 * @return 输出的字符数
 */
auto uart_printf(const char *format, ...) -> int;

/**
 * @brief 简易格式化输出到 UART（va_list 版本）
 *
 * 与 uart_printf 相同，但接受 va_list 参数。
 * 用于实现自定义的可变参数日志函数。
 *
 * @param format 格式化字符串
 * @param args va_list 参数
 * @return 输出的字符数
 */
auto uart_vprintf(const char *format, va_list args) -> int;

/**
 * @brief 尝试从 UART 读取一个字符
 * @return 读取的字符，如果没有可用字符则返回 -1
 */
auto uart_getc() -> int;

/**
 * @brief UART 中断处理函数
 */
void uart_handle_interrupt();

#endif /* DEVICE_FRAMEWORK_TEST_UART_H_ */
