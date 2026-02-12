/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_DRIVER_NS16550A_NS16550A_DEVICE_HPP_
#define DEVICE_FRAMEWORK_DRIVER_NS16550A_NS16550A_DEVICE_HPP_

#include <cstdint>
#include <span>

#include "device_framework/driver/ns16550a/ns16550a.hpp"
#include "device_framework/expected.hpp"
#include "device_framework/ops/char_device.hpp"

namespace device_framework::ns16550a {

/**
 * @brief NS16550A 字符设备
 *
 * 将底层 Ns16550a 驱动适配到统一的 CharDevice 接口。
 * 支持 Open/Release/Read/Write/Poll 操作。
 */
class Ns16550aDevice : public CharDevice<Ns16550aDevice> {
 public:
  Ns16550aDevice() = default;
  explicit Ns16550aDevice(uint64_t base_addr) : driver_(base_addr) {}

  /// @brief 直接访问底层驱动（用于中断处理等需要绕过 Device 框架的场景）
  auto GetDriver() -> Ns16550a& { return driver_; }

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

 private:
  /// @brief CRTP 基类需要访问 DoXxx 方法
  template <class>
  friend class DeviceOperationsBase;
  template <class>
  friend class CharDevice;

  Ns16550a driver_;
  OpenFlags flags_{0};
};

}  // namespace device_framework::ns16550a

#endif /* DEVICE_FRAMEWORK_DRIVER_NS16550A_NS16550A_DEVICE_HPP_ */
