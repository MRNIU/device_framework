/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_DETAIL_NS16550A_NS16550A_DEVICE_HPP_
#define DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_DETAIL_NS16550A_NS16550A_DEVICE_HPP_

#include <cstdint>

#include "device_framework/detail/ns16550a/ns16550a.hpp"
#include "device_framework/detail/uart_device.hpp"

namespace device_framework::detail::ns16550a {

/**
 * @brief NS16550A 字符设备
 *
 * 将底层 Ns16550a 驱动适配到统一的 CharDevice 接口。
 * 支持 Open/Release/Read/Write/Poll 操作。
 */
class Ns16550aDevice : public UartDevice<Ns16550aDevice, Ns16550a> {
 public:
  Ns16550aDevice() = default;
  explicit Ns16550aDevice(uint64_t base_addr) { driver_ = Ns16550a(base_addr); }
};

}  // namespace device_framework::detail::ns16550a

#endif /* DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_DETAIL_NS16550A_NS16550A_DEVICE_HPP_ \
        */
