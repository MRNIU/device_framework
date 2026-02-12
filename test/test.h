/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef VIRTIO_DRIVER_TESTS_TEST_FRAMEWORK_H_
#define VIRTIO_DRIVER_TESTS_TEST_FRAMEWORK_H_

#include <cstdint>

#include "uart.h"

/**
 * @brief 测试统计信息
 */
struct TestStats {
  int total;
  int passed;
  int failed;
};

// 全局测试统计
extern TestStats g_test_stats;

/**
 * @brief EXPECT_TRUE 宏 - 验证条件为真
 * @param condition 要检查的条件
 * @param msg 失败时的消息
 */
#define EXPECT_TRUE(condition, msg) \
  do {                              \
    g_test_stats.total++;           \
    if (condition) {                \
      g_test_stats.passed++;        \
      uart_puts("[PASS] ");         \
      uart_puts(msg);               \
      uart_puts("\n");              \
    } else {                        \
      g_test_stats.failed++;        \
      uart_puts("[FAIL] ");         \
      uart_puts(msg);               \
      uart_puts(" at ");            \
      uart_puts(__FILE__);          \
      uart_puts(":");               \
      uart_put_hex(__LINE__);       \
      uart_puts("\n");              \
    }                               \
  } while (0)

/**
 * @brief EXPECT_FALSE 宏 - 验证条件为假
 * @param condition 要检查的条件
 * @param msg 失败时的消息
 */
#define EXPECT_FALSE(condition, msg) EXPECT_TRUE(!(condition), msg)

/**
 * @brief EXPECT_EQ 宏 - 验证两个值相等
 * @param expected 期望值
 * @param actual 实际值
 * @param msg 失败时的消息
 */
#define EXPECT_EQ(expected, actual, msg)            \
  do {                                              \
    g_test_stats.total++;                           \
    auto exp_val = (expected);                      \
    auto act_val = (actual);                        \
    if (exp_val == act_val) {                       \
      g_test_stats.passed++;                        \
      uart_puts("[PASS] ");                         \
      uart_puts(msg);                               \
      uart_puts("\n");                              \
    } else {                                        \
      g_test_stats.failed++;                        \
      uart_puts("[FAIL] ");                         \
      uart_puts(msg);                               \
      uart_puts(" - Expected: 0x");                 \
      uart_put_hex(static_cast<uint64_t>(exp_val)); \
      uart_puts(", Got: 0x");                       \
      uart_put_hex(static_cast<uint64_t>(act_val)); \
      uart_puts(" at ");                            \
      uart_puts(__FILE__);                          \
      uart_puts(":");                               \
      uart_put_hex(__LINE__);                       \
      uart_puts("\n");                              \
    }                                               \
  } while (0)

/**
 * @brief EXPECT_NE 宏 - 验证两个值不相等
 * @param not_expected 不期望的值
 * @param actual 实际值
 * @param msg 失败时的消息
 */
#define EXPECT_NE(not_expected, actual, msg)            \
  do {                                                  \
    g_test_stats.total++;                               \
    auto not_exp_val = (not_expected);                  \
    auto act_val = (actual);                            \
    if (not_exp_val != act_val) {                       \
      g_test_stats.passed++;                            \
      uart_puts("[PASS] ");                             \
      uart_puts(msg);                                   \
      uart_puts("\n");                                  \
    } else {                                            \
      g_test_stats.failed++;                            \
      uart_puts("[FAIL] ");                             \
      uart_puts(msg);                                   \
      uart_puts(" - Value should not be: 0x");          \
      uart_put_hex(static_cast<uint64_t>(not_exp_val)); \
      uart_puts(" at ");                                \
      uart_puts(__FILE__);                              \
      uart_puts(":");                                   \
      uart_put_hex(__LINE__);                           \
      uart_puts("\n");                                  \
    }                                                   \
  } while (0)

/**
 * @brief LOG 宏 - 输出日志信息
 * @param msg 日志消息
 */
#define LOG(msg)         \
  do {                   \
    uart_puts("[LOG] "); \
    uart_puts(msg);      \
    uart_puts("\n");     \
  } while (0)

/**
 * @brief LOG_HEX 宏 - 输出十六进制值
 * @param msg 消息前缀
 * @param value 要输出的值
 */
#define LOG_HEX(msg, value)                     \
  do {                                          \
    uart_puts("[LOG] ");                        \
    uart_puts(msg);                             \
    uart_puts(": 0x");                          \
    uart_put_hex(static_cast<uint64_t>(value)); \
    uart_puts("\n");                            \
  } while (0)

/// @name VirtIO 中断回调机制
/// @{

/// VirtIO 设备中断回调函数类型
using VirtioIrqHandler = void (*)();

/// VirtIO 设备中断回调表（索引 0-7 对应 8 个 MMIO 设备）
extern VirtioIrqHandler g_virtio_irq_handlers[8];

/// @}

/**
 * @brief 初始化测试框架
 */
void test_framework_init();

/**
 * @brief 打印测试结果摘要
 */
void test_framework_print_summary();

/**
 * @brief 测试 VirtIO MMIO 设备状态读取
 */
void test_virtio_mmio_device_status();

/**
 * @brief 测试 VirtIO 块设备功能
 */
void test_virtio_blk();

/**
 * @brief 测试 VirtIO 块设备统一接口
 */
void test_virtio_blk_device();

/**
 * @brief 测试 NS16550A 字符设备统一接口
 */
void test_ns16550a();

#endif /* VIRTIO_DRIVER_TESTS_TEST_FRAMEWORK_H_ */
