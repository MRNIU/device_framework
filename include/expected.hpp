/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_SRC_INCLUDE_EXPECTED_HPP_
#define VIRTIO_DRIVER_SRC_INCLUDE_EXPECTED_HPP_

#include <cstddef>
#include <cstdint>
#include <expected>

namespace virtio_driver {

/// virtio 驱动错误码
enum class ErrorCode : uint32_t {
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
};

/**
 * @brief 获取错误码的描述字符串
 * @param code 错误码
 * @return 错误描述字符串
 */
constexpr auto GetErrorMessage(ErrorCode code) -> const char* {
  switch (code) {
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
    default:
      return "Unknown error";
  }
}

/**
 * @brief virtio 驱动错误类型
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
   * @brief 隐式转换为 ErrorCode，方便比较
   */
  explicit constexpr operator ErrorCode() const { return code; }
};

/// std::expected 别名模板
template <typename T>
using Expected = std::expected<T, Error>;

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_SRC_INCLUDE_EXPECTED_HPP_ */
