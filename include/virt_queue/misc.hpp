/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_INCLUDE_VIRT_QUEUE_MISC_HPP_
#define VIRTIO_DRIVER_INCLUDE_VIRT_QUEUE_MISC_HPP_

#include <cstddef>
#include <cstdint>

namespace virtio_driver {

/**
 * @brief 将值向上对齐到指定边界
 *
 * 该函数将给定的值向上舍入到最接近的对齐边界的倍数。
 * 常用于计算 DMA 缓冲区布局、描述符表和队列环的内存对齐要求。
 *
 * @param value 要对齐的值（字节数）
 * @param align 对齐边界（字节数，必须为 2 的幂）
 * @return 对齐后的值（>= value，且为 align 的整数倍）
 *
 * @note align 必须为 2 的幂（如 2, 4, 8, 16 等）
 * @note 该函数为 constexpr，可在编译期求值
 *
 * @see virtio-v1.2#2.7.5 The Virtqueue Descriptor Table
 * @see virtio-v1.2#2.7.6 The Virtqueue Available Ring
 * @see virtio-v1.2#2.7.8 The Virtqueue Used Ring
 */
[[nodiscard]] constexpr auto AlignUp(size_t value, size_t align) -> size_t {
  return (value + align - 1) & ~(align - 1);
}

/**
 * @brief 检查值是否为 2 的幂
 *
 * 用于验证对齐参数、队列大小等必须为 2 的幂的参数。
 * VirtIO 规范要求许多参数（如 queue_size）必须为 2 的幂。
 *
 * @param value 要检查的值
 * @return true 表示 value 是 2 的幂（如 1, 2, 4, 8, 16...），false 表示不是
 *
 * @note value 为 0 时返回 false
 * @note 该函数为 constexpr，可在编译期求值
 *
 * @see virtio-v1.2#2.6 Split Virtqueues
 */
[[nodiscard]] constexpr auto IsPowerOfTwo(size_t value) -> bool {
  return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_INCLUDE_VIRT_QUEUE_MISC_HPP_ */
