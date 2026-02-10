/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_SRC_INCLUDE_VIRT_QUEUE_MISC_HPP_
#define VIRTIO_DRIVER_SRC_INCLUDE_VIRT_QUEUE_MISC_HPP_

#include <cstddef>
#include <cstdint>
#include <expected>

namespace virtio_driver {

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

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_SRC_INCLUDE_VIRT_QUEUE_MISC_HPP_ */
