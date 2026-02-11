/**
 * @file platform.h
 * @brief 平台抽象接口
 *
 * 使用者需要实现此结构中的函数指针，以适配自己的内核/平台环境。
 * 本驱动库不直接调用任何 OS API，所有平台相关操作均通过此接口完成。
 *
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_PLATFORM_H_
#define VIRTIO_DRIVER_PLATFORM_H_

#include <cstddef>

namespace virtio_driver {

/**
 * @brief 平台操作函数集合
 * @note 使用者必须在创建 virtio 设备前初始化所有函数指针
 */
struct PlatformOps {
  /**
   * @brief 虚拟地址转换为物理地址
   * @param vaddr 虚拟地址
   * @return 对应的物理地址
   */
  uint64_t (*virt_to_phys)(void* vaddr);
};

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_PLATFORM_H_ */
