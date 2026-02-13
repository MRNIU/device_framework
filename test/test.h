/**
 * @file test.h
 * @brief 轻量级测试框架：宏、统计与套件管理
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_TEST_TEST_H_
#define DEVICE_FRAMEWORK_TEST_TEST_H_

#include <cstdint>

#include "uart.h"

/**
 * @brief 测试统计信息
 */
struct TestStats {
  uint32_t total;
  uint32_t passed;
  uint32_t failed;
};

/// 当前测试套件统计
extern TestStats g_suite_stats;
/// 全局累积统计
extern TestStats g_global_stats;

/**
 * @brief 开始一个测试套件，重置套件统计并输出标题
 * @param name 套件名称（字符串字面量）
 */
#define TEST_SUITE_BEGIN(name)   \
  do {                           \
    g_suite_stats = {0, 0, 0};   \
    uart_puts("\n[" name "]\n"); \
  } while (0)

/**
 * @brief 结束一个测试套件，输出该套件的统计摘要
 */
#define TEST_SUITE_END()                \
  do {                                  \
    uart_puts("  --- ");                \
    uart_put_dec(g_suite_stats.passed); \
    uart_putc('/');                     \
    uart_put_dec(g_suite_stats.total);  \
    uart_puts(" passed ---\n");         \
  } while (0)

/**
 * @brief EXPECT_TRUE 宏 - 验证条件为真
 * @param condition 要检查的条件
 * @param msg 描述信息
 */
#define EXPECT_TRUE(condition, msg) \
  do {                              \
    g_suite_stats.total++;          \
    g_global_stats.total++;         \
    if (condition) {                \
      g_suite_stats.passed++;       \
      g_global_stats.passed++;      \
      uart_puts("  [PASS] ");       \
      uart_puts(msg);               \
      uart_puts("\n");              \
    } else {                        \
      g_suite_stats.failed++;       \
      g_global_stats.failed++;      \
      uart_puts("  [FAIL] ");       \
      uart_puts(msg);               \
      uart_puts(" (");              \
      uart_puts(__FILE__);          \
      uart_putc(':');               \
      uart_put_dec(__LINE__);       \
      uart_puts(")\n");             \
    }                               \
  } while (0)

/**
 * @brief EXPECT_FALSE 宏 - 验证条件为假
 * @param condition 要检查的条件
 * @param msg 描述信息
 */
#define EXPECT_FALSE(condition, msg) EXPECT_TRUE(!(condition), msg)

/**
 * @brief EXPECT_EQ 宏 - 验证两个值相等
 * @param expected 期望值
 * @param actual 实际值
 * @param msg 描述信息
 */
#define EXPECT_EQ(expected, actual, msg)             \
  do {                                               \
    g_suite_stats.total++;                           \
    g_global_stats.total++;                          \
    auto exp_val_ = (expected);                      \
    auto act_val_ = (actual);                        \
    if (exp_val_ == act_val_) {                      \
      g_suite_stats.passed++;                        \
      g_global_stats.passed++;                       \
      uart_puts("  [PASS] ");                        \
      uart_puts(msg);                                \
      uart_puts("\n");                               \
    } else {                                         \
      g_suite_stats.failed++;                        \
      g_global_stats.failed++;                       \
      uart_puts("  [FAIL] ");                        \
      uart_puts(msg);                                \
      uart_puts(" (expected: 0x");                   \
      uart_put_hex(static_cast<uint64_t>(exp_val_)); \
      uart_puts(", got: 0x");                        \
      uart_put_hex(static_cast<uint64_t>(act_val_)); \
      uart_puts(") (");                              \
      uart_puts(__FILE__);                           \
      uart_putc(':');                                \
      uart_put_dec(__LINE__);                        \
      uart_puts(")\n");                              \
    }                                                \
  } while (0)

/**
 * @brief EXPECT_NE 宏 - 验证两个值不相等
 * @param not_expected 不期望的值
 * @param actual 实际值
 * @param msg 描述信息
 */
#define EXPECT_NE(not_expected, actual, msg)             \
  do {                                                   \
    g_suite_stats.total++;                               \
    g_global_stats.total++;                              \
    auto not_exp_val_ = (not_expected);                  \
    auto act_val_ = (actual);                            \
    if (not_exp_val_ != act_val_) {                      \
      g_suite_stats.passed++;                            \
      g_global_stats.passed++;                           \
      uart_puts("  [PASS] ");                            \
      uart_puts(msg);                                    \
      uart_puts("\n");                                   \
    } else {                                             \
      g_suite_stats.failed++;                            \
      g_global_stats.failed++;                           \
      uart_puts("  [FAIL] ");                            \
      uart_puts(msg);                                    \
      uart_puts(" (should not be: 0x");                  \
      uart_put_hex(static_cast<uint64_t>(not_exp_val_)); \
      uart_puts(") (");                                  \
      uart_puts(__FILE__);                               \
      uart_putc(':');                                    \
      uart_put_dec(__LINE__);                            \
      uart_puts(")\n");                                  \
    }                                                    \
  } while (0)

/**
 * @brief LOG 宏 - 输出测试过程信息
 * @param msg 消息
 */
#define LOG(msg)            \
  do {                      \
    uart_puts("  [INFO] "); \
    uart_puts(msg);         \
    uart_puts("\n");        \
  } while (0)

/**
 * @brief LOG_HEX 宏 - 输出带十六进制值的信息
 * @param msg 消息前缀
 * @param value 要输出的值
 */
#define LOG_HEX(msg, value)                     \
  do {                                          \
    uart_puts("  [INFO] ");                     \
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
 * @brief 输出全局测试横幅
 */
void test_print_banner();

/**
 * @brief 输出全局测试摘要
 */
void test_print_summary();

/// @name 测试套件声明
/// @{

void test_ns16550a();
void test_virtio_mmio_device_status();
void test_virtio_blk();
void test_virtio_blk_device();

/// @}

#endif /* DEVICE_FRAMEWORK_TEST_TEST_H_ */
