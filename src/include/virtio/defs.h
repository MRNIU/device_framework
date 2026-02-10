/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DEFS_H
#define VIRTIO_DEFS_H

#include "virtio/types.h"

namespace virtio {

/**
 * @brief 设备状态位定义
 * @see virtio-v1.2#2.1
 */
enum class DeviceStatus : uint8_t {
  /// 表示 OS 已识别设备 (ACKNOWLEDGE)
  kAcknowledge = 1,
  /// 表示 OS 已匹配到驱动 (DRIVER)
  kDriver = 2,
  /// 驱动准备就绪，设备可用 (DRIVER_OK)
  kDriverOk = 4,
  /// 设备特性协商完成 (FEATURES_OK)
  kFeaturesOk = 8,
  /// 设备需要重置 (DEVICE_NEEDS_RESET)
  kDeviceNeedsReset = 64,
  /// 设备出现不可恢复的错误 (FAILED)
  kFailed = 128,
};

/**
 * @brief Virtio 设备 ID 定义
 * @see virtio-v1.2#5
 */
enum class DeviceId : uint32_t {
  kReserved = 0,
  kNetwork = 1,
  kBlock = 2,
  kConsole = 3,
  kEntropy = 4,
  kMemoryBalloonTraditional = 5,
  kIoMemory = 6,
  kRpmsg = 7,
  kScsiHost = 8,
  kNinepTransport = 9,
  kMac80211Wlan = 10,
  kRprocSerial = 11,
  kVirtioCaif = 12,
  kMemoryBalloon = 13,
  // 14-15 保留
  kGpu = 16,
  kTimerClock = 17,
  kInput = 18,
  kSocket = 19,
  kCrypto = 20,
  kSignalDist = 21,
  kPstore = 22,
  kIommu = 23,
  kMemory = 24,
  kSound = 25,
  kFilesystem = 26,
  kPmem = 27,
  kRpmb = 28,
  // 29-31 保留
  kScmi = 32,
  // 33 保留
  kI2cAdapter = 34,
  // 35 保留
  kCan = 36,
  // 37 保留
  kParameterServer = 38,
  kAudioPolicy = 39,
  kBluetooth = 40,
  kGpio = 41,
  kRdma = 42,
};

/**
 * @brief 保留特性位定义 (Reserved Feature Bits)
 * @see virtio-v1.2#6
 *
 * 特性位分配:
 * - 0-23: 设备特定特性位
 * - 24-40: 队列和特性协商机制扩展保留位
 * - 41-49: 为未来扩展保留
 * - 50-127: 设备特定特性位
 * - 128及以上: 为未来扩展保留
 */
enum class ReservedFeature : uint64_t {
  /// 设备支持间接描述符 (VIRTIO_F_INDIRECT_DESC)
  kIndirectDesc = 1ULL << 28,
  /// 设备支持 avail_event 和 used_event 字段 (VIRTIO_F_EVENT_IDX)
  kEventIdx = 1ULL << 29,
  /// 设备符合 virtio 1.0+ 规范 (VIRTIO_F_VERSION_1)
  kVersion1 = 1ULL << 32,
  /// 设备可被 IOMMU 限定的平台访问 (VIRTIO_F_ACCESS_PLATFORM)
  kAccessPlatform = 1ULL << 33,
  /// 支持 Packed Virtqueue 布局 (VIRTIO_F_RING_PACKED)
  kRingPacked = 1ULL << 34,
  /// 按顺序使用缓冲区 (VIRTIO_F_IN_ORDER)
  kInOrder = 1ULL << 35,
  /// 平台提供内存排序保证 (VIRTIO_F_ORDER_PLATFORM)
  kOrderPlatform = 1ULL << 36,
  /// 支持 Single Root I/O Virtualization (VIRTIO_F_SR_IOV)
  kSrIov = 1ULL << 37,
  /// 驱动在通知中传递额外数据 (VIRTIO_F_NOTIFICATION_DATA)
  kNotificationData = 1ULL << 38,
  /// 驱动使用设备提供的数据作为可用缓冲区通知的 virtqueue 标识符
  /// (VIRTIO_F_NOTIF_CONFIG_DATA)
  kNotifConfigData = 1ULL << 39,
  /// 驱动可以单独重置队列 (VIRTIO_F_RING_RESET)
  kRingReset = 1ULL << 40,
};

}  // namespace virtio

#endif /* VIRTIO_DEFS_H */
