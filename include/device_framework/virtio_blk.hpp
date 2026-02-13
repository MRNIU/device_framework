/**
 * @copyright Copyright The device_framework Contributors
 *
 * @brief VirtIO 块设备公开接口
 *
 * 用户应通过此头文件使用 VirtIO 块设备，而非直接包含 detail/ 中的实现文件。
 * 此头文件同时导出 VirtioTraits concept，供用户定义自己的平台 Traits。
 *
 * @code
 * #include "device_framework/virtio_blk.hpp"
 *
 * // 定义满足 VirtioTraits 的平台 Traits
 * struct MyTraits { ... };
 *
 * device_framework::virtio::blk::VirtioBlkDevice<MyTraits> blk(mmio_base);
 * blk.OpenReadWrite();
 * blk.ReadBlock(0, buffer);
 * @endcode
 */

#ifndef DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_VIRTIO_BLK_HPP_
#define DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_VIRTIO_BLK_HPP_

#include "device_framework/detail/virtio/device/virtio_blk_device.hpp"
#include "device_framework/detail/virtio/traits.hpp"

namespace device_framework::virtio {
using namespace detail::virtio;  // NOLINT(google-build-using-namespace)
}  // namespace device_framework::virtio

namespace device_framework::virtio::blk {
using namespace detail::virtio::blk;  // NOLINT(google-build-using-namespace)
}  // namespace device_framework::virtio::blk

#endif /* DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_VIRTIO_BLK_HPP_ */
