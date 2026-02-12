/**
 * @copyright Copyright The device_framework Contributors
 *
 * @brief NS16550A UART 设备公开接口
 *
 * 用户应通过此头文件使用 NS16550A 设备，而非直接包含 detail/ 中的实现文件。
 *
 * @code
 * #include "device_framework/ns16550a.hpp"
 *
 * device_framework::ns16550a::Ns16550aDevice uart(0x1000'0000);
 * uart.OpenReadWrite();
 * uart.PutChar('H');
 * @endcode
 */

#ifndef DEVICE_FRAMEWORK_NS16550A_HPP_
#define DEVICE_FRAMEWORK_NS16550A_HPP_

#include "device_framework/detail/ns16550a/ns16550a_device.hpp"

#endif /* DEVICE_FRAMEWORK_NS16550A_HPP_ */
