/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_DRIVER_VIRTIO_TRAITS_HPP_
#define DEVICE_FRAMEWORK_DRIVER_VIRTIO_TRAITS_HPP_

#include "device_framework/traits.hpp"

namespace device_framework::virtio {

/**
 * @brief VirtIO 驱动专用 Traits 约束
 *
 * 在 EnvironmentTraits 基础上，额外要求内存屏障和 DMA 地址转换能力。
 * VirtIO 驱动的模板参数应约束为此 concept。
 *
 * @see virtio-v1.2#2.4 Virtqueues
 */
template <typename T>
concept VirtioTraits = EnvironmentTraits<T> && BarrierTraits<T> && DmaTraits<T>;

/**
 * @brief VirtIO 零开销默认 Traits
 *
 * 满足 VirtioTraits concept 的默认实现，所有方法在编译期消除。
 * 继承自 device_framework::NullTraits。
 */
using NullVirtioTraits = device_framework::NullTraits;

}  // namespace device_framework::virtio

#endif /* DEVICE_FRAMEWORK_DRIVER_VIRTIO_TRAITS_HPP_ */
