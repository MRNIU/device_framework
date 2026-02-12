/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_DETAIL_UART_DEVICE_HPP_
#define DEVICE_FRAMEWORK_DETAIL_UART_DEVICE_HPP_

#include <cstdint>
#include <span>

#include "device_framework/expected.hpp"
#include "device_framework/ops/char_device.hpp"

namespace device_framework {

/**
 * @brief UART 字符设备通用 CRTP 中间层
 *
 * 将底层 UART 驱动（NS16550A、PL011 等）适配到统一的 CharDevice 接口。
 * 底层驱动类型 DriverType 需满足以下隐式接口：
 * - `PutChar(uint8_t) -> void`
 * - `TryGetChar() -> std::optional<uint8_t>`
 * - `HasData() -> bool`
 *
 * @tparam Derived 具体设备类型（CRTP）
 * @tparam DriverType 底层 UART 驱动类型
 */
template <class Derived, class DriverType>
class UartDevice : public CharDevice<Derived> {
 public:
  UartDevice() = default;

  /// @brief 直接访问底层驱动（用于中断处理等需要绕过 Device 框架的场景）
  auto GetDriver() -> DriverType& { return driver_; }

 protected:
  auto DoOpen(OpenFlags flags) -> Expected<void> {
    if (!flags.CanRead() && !flags.CanWrite()) {
      return std::unexpected(Error{ErrorCode::kInvalidArgument});
    }
    flags_ = flags;
    return {};
  }

  auto DoCharRead(std::span<uint8_t> buffer) -> Expected<size_t> {
    if (!flags_.CanRead()) {
      return std::unexpected(Error{ErrorCode::kDevicePermissionDenied});
    }
    for (size_t i = 0; i < buffer.size(); ++i) {
      auto ch = driver_.TryGetChar();
      if (!ch) {
        return i;
      }
      buffer[i] = *ch;
    }
    return buffer.size();
  }

  auto DoCharWrite(std::span<const uint8_t> data) -> Expected<size_t> {
    if (!flags_.CanWrite()) {
      return std::unexpected(Error{ErrorCode::kDevicePermissionDenied});
    }
    for (auto byte : data) {
      driver_.PutChar(byte);
    }
    return data.size();
  }

  auto DoPoll(PollEvents requested) -> Expected<PollEvents> {
    uint32_t ready = 0;
    if (requested.HasIn() && driver_.HasData()) {
      ready |= PollEvents::kIn;
    }
    if (requested.HasOut()) {
      ready |= PollEvents::kOut;
    }
    return PollEvents{ready};
  }

  auto DoRelease() -> Expected<void> { return {}; }

  /// @name 构造/析构函数
  /// @{
  ~UartDevice() = default;
  UartDevice(const UartDevice&) = delete;
  auto operator=(const UartDevice&) -> UartDevice& = delete;
  UartDevice(UartDevice&&) noexcept = default;
  auto operator=(UartDevice&&) noexcept -> UartDevice& = default;
  /// @}

  DriverType driver_;
  OpenFlags flags_{0};

 private:
  /// @brief CRTP 基类需要访问 DoXxx 方法
  template <class>
  friend class DeviceOperationsBase;
  template <class>
  friend class CharDevice;
};

}  // namespace device_framework

#endif /* DEVICE_FRAMEWORK_DETAIL_UART_DEVICE_HPP_ */
