/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_EXPECTED_HPP_
#define DEVICE_FRAMEWORK_EXPECTED_HPP_

#include <cstddef>
#include <cstdint>
#include <expected>

namespace device_framework {

/// @brief 设备框架错误码
enum class ErrorCode : uint32_t {
  /// 操作成功
  kSuccess = 0,

  /// @name VirtIO 传输层错误
  /// @{
  /// 无效的 MMIO 魔数
  kInvalidMagic = 1,
  /// 无效的版本号
  kInvalidVersion,
  /// 无效的设备 ID（设备不存在）
  kInvalidDeviceId,
  /// 传输层未正确初始化
  kTransportNotInitialized,
  /// 特性协商失败
  kFeatureNegotiationFailed,
  /// 队列不可用（queue_num_max == 0）
  kQueueNotAvailable,
  /// 队列已被使用
  kQueueAlreadyUsed,
  /// 请求的队列大小超过设备支持的最大值
  kQueueTooLarge,
  /// 提供的内存不足
  kOutOfMemory,
  /// 没有空闲描述符
  kNoFreeDescriptors,
  /// 无效的描述符索引
  kInvalidDescriptor,
  /// 没有已使用的缓冲区可回收
  kNoUsedBuffers,
  /// 设备报告错误
  kDeviceError,
  /// IO 操作错误
  kIoError,
  /// 不支持的操作
  kNotSupported,
  /// 无效的参数
  kInvalidArgument,
  /// 操作超时
  kTimeout,
  /// @}

  /// @name 设备操作层错误
  /// @{
  /// 设备已处于打开状态
  kDeviceAlreadyOpen,
  /// 设备未打开
  kDeviceNotOpen,
  /// 设备不支持此操作
  kDeviceNotSupported,
  /// 权限不足
  kDevicePermissionDenied,
  /// 块访问未对齐
  kDeviceBlockUnaligned,
  /// 块号超出范围
  kDeviceBlockOutOfRange,
  /// 读取失败
  kDeviceReadFailed,
  /// @}
};

/**
 * @brief 获取错误码的描述字符串
 * @param code 错误码
 * @return 错误描述字符串
 */
constexpr auto GetErrorMessage(ErrorCode code) -> const char* {
  switch (code) {
    case ErrorCode::kSuccess:
      return "Success";
    case ErrorCode::kInvalidMagic:
      return "Invalid MMIO magic value";
    case ErrorCode::kInvalidVersion:
      return "Unsupported virtio version";
    case ErrorCode::kInvalidDeviceId:
      return "Invalid device ID (device does not exist)";
    case ErrorCode::kTransportNotInitialized:
      return "Transport layer not initialized";
    case ErrorCode::kFeatureNegotiationFailed:
      return "Feature negotiation failed";
    case ErrorCode::kQueueNotAvailable:
      return "Queue not available (queue_num_max == 0)";
    case ErrorCode::kQueueAlreadyUsed:
      return "Queue already used";
    case ErrorCode::kQueueTooLarge:
      return "Requested queue size exceeds maximum";
    case ErrorCode::kOutOfMemory:
      return "Out of memory";
    case ErrorCode::kNoFreeDescriptors:
      return "No free descriptors available";
    case ErrorCode::kInvalidDescriptor:
      return "Invalid descriptor index";
    case ErrorCode::kNoUsedBuffers:
      return "No used buffers to reclaim";
    case ErrorCode::kDeviceError:
      return "Device reported an error";
    case ErrorCode::kIoError:
      return "I/O operation failed";
    case ErrorCode::kNotSupported:
      return "Operation not supported";
    case ErrorCode::kInvalidArgument:
      return "Invalid argument";
    case ErrorCode::kTimeout:
      return "Operation timed out";
    case ErrorCode::kDeviceAlreadyOpen:
      return "Device already open";
    case ErrorCode::kDeviceNotOpen:
      return "Device not open";
    case ErrorCode::kDeviceNotSupported:
      return "Device does not support this operation";
    case ErrorCode::kDevicePermissionDenied:
      return "Permission denied";
    case ErrorCode::kDeviceBlockUnaligned:
      return "Block access not aligned";
    case ErrorCode::kDeviceBlockOutOfRange:
      return "Block number out of range";
    case ErrorCode::kDeviceReadFailed:
      return "Device read failed";
    default:
      return "Unknown error";
  }
}

/**
 * @brief 设备框架错误类型
 */
struct Error {
  ErrorCode code;

  constexpr Error(ErrorCode c) : code(c) {}

  /**
   * @brief 获取错误描述消息
   * @return 错误描述字符串
   */
  [[nodiscard]] constexpr auto message() const -> const char* {
    return GetErrorMessage(code);
  }

  /**
   * @brief 显式转换为 ErrorCode
   */
  explicit constexpr operator ErrorCode() const { return code; }

  /// @name 比较运算符
  /// @{
  [[nodiscard]] constexpr auto operator==(const Error& other) const -> bool {
    return code == other.code;
  }
  [[nodiscard]] constexpr auto operator==(ErrorCode other) const -> bool {
    return code == other;
  }
  /// @}
};

/// @brief std::expected 别名模板
template <typename T>
using Expected = std::expected<T, Error>;

}  // namespace device_framework

#endif /* DEVICE_FRAMEWORK_EXPECTED_HPP_ */
