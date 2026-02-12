/**
 * @copyright Copyright The device_framework Contributors
 *
 * @brief PL011 UART 设备公开接口
 *
 * 用户应通过此头文件使用 PL011 设备，而非直接包含 detail/ 中的实现文件。
 *
 * @code
 * #include "device_framework/pl011.hpp"
 *
 * device_framework::pl011::Pl011Device uart(0x0900'0000);
 * uart.OpenReadWrite();
 * uart.PutChar('H');
 * @endcode
 */

#ifndef DEVICE_FRAMEWORK_PL011_HPP_
#define DEVICE_FRAMEWORK_PL011_HPP_

#include "device_framework/detail/pl011/pl011_device.hpp"

#endif /* DEVICE_FRAMEWORK_PL011_HPP_ */
