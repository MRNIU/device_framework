/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_DEFS_H_
#define DEVICE_FRAMEWORK_DEFS_H_

#include <cstdint>

namespace device_framework {

/// @brief 设备类型枚举
enum class DeviceType : uint32_t {
  /// 未知设备类型
  kUnknown = 0,
  /// 字符设备
  kChar,
  /// 块设备
  kBlock,
  /// 网络设备
  kNetwork,
};

}  // namespace device_framework

#endif /* DEVICE_FRAMEWORK_DEFS_H_ */
