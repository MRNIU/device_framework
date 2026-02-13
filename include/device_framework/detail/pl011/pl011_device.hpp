/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_DETAIL_PL011_PL011_DEVICE_HPP_
#define DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_DETAIL_PL011_PL011_DEVICE_HPP_

#include <cstdint>

#include "device_framework/detail/pl011/pl011.hpp"
#include "device_framework/detail/uart_device.hpp"

namespace device_framework::detail::pl011 {

/**
 * @brief PL011 字符设备
 *
 * 将底层 Pl011 驱动适配到统一的 CharDevice 接口。
 * 支持 Open/Release/Read/Write/Poll 操作。
 */
class Pl011Device : public UartDevice<Pl011Device, Pl011> {
 public:
  Pl011Device() = default;
  explicit Pl011Device(uint64_t base_addr) { driver_ = Pl011(base_addr); }
  Pl011Device(uint64_t base_addr, uint64_t clock, uint64_t baud_rate) {
    driver_ = Pl011(base_addr, clock, baud_rate);
  }
};

}  // namespace device_framework::detail::pl011

#endif /* DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_DETAIL_PL011_PL011_DEVICE_HPP_ \
        */
