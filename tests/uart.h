/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_TESTS_UART_H_
#define VIRTIO_DRIVER_TESTS_UART_H_

#include <cstdint>

/**
 * @brief UART 基地址 (QEMU virt machine)
 */
#define UART0_BASE 0x10000000UL

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

#endif  // VIRTIO_DRIVER_TESTS_UART_H_
