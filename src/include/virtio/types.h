/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_TYPES_H
#define VIRTIO_TYPES_H

#include <cstddef>
#include <cstdint>
#include <expected>

namespace virtio {

/// 物理地址类型
using PhysAddr = uint64_t;

/// virtio 驱动错误类型
enum class Error : uint32_t {
  /// 无效的 MMIO 魔数
  kInvalidMagic = 1,
  /// 无效的版本号
  kInvalidVersion,
  /// 无效的设备 ID（设备不存在）
  kInvalidDeviceId,
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

/// 带返回值的结果类型
template <typename T>
using Result = std::expected<T, Error>;

/// 无返回值的结果类型
using VoidResult = std::expected<void, Error>;

/**
 * @brief 将值向上对齐到指定边界
 * @param value 要对齐的值
 * @param align 对齐边界（必须为 2 的幂）
 * @return 对齐后的值
 */
constexpr auto align_up(size_t value, size_t align) -> size_t {
  return (value + align - 1) & ~(align - 1);
}

/**
 * @brief 检查值是否为 2 的幂
 * @param value 要检查的值
 * @return 是否为 2 的幂
 */
constexpr auto is_power_of_two(size_t value) -> bool {
  return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace virtio

#endif /* VIRTIO_TYPES_H */
