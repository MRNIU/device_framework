/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_TRANSPORT_TRANSPORT_HPP_
#define VIRTIO_DRIVER_TRANSPORT_TRANSPORT_HPP_

#include "virtio_driver/defs.h"
#include "virtio_driver/expected.hpp"
#include "virtio_driver/traits.hpp"

namespace virtio_driver {

/**
 * @brief Virtio 传输层 CRTP 基类（零虚表开销）
 *
 * 通过 CRTP（Curiously Recurring Template Pattern）实现编译期多态，
 * 消除虚表指针开销，实现零开销抽象。子类（MmioTransport、PciTransport）
 * 继承此基类并提供具体的寄存器访问实现。
 *
 * 基类仅提供通用逻辑方法（Reset、NeedsReset、IsActive、AcknowledgeInterrupt），
 * 通过 CRTP 静态分发调用子类的具体实现。
 *
 * 子类应提供以下方法（隐式接口，不再通过纯虚函数声明）：
 * - IsValid() const -> bool
 * - GetDeviceId() const -> uint32_t
 * - GetVendorId() const -> uint32_t
 * - GetStatus() const -> uint32_t
 * - SetStatus(uint32_t) -> void
 * - GetDeviceFeatures() -> uint64_t
 * - SetDriverFeatures(uint64_t) -> void
 * - GetQueueNumMax(uint32_t) -> uint32_t
 * - SetQueueNum(uint32_t, uint32_t) -> void
 * - SetQueueDesc(uint32_t, uint64_t) -> void
 * - SetQueueAvail(uint32_t, uint64_t) -> void
 * - SetQueueUsed(uint32_t, uint64_t) -> void
 * - GetQueueReady(uint32_t) -> bool
 * - SetQueueReady(uint32_t, bool) -> void
 * - NotifyQueue(uint32_t) -> void
 * - GetInterruptStatus() const -> uint32_t
 * - AckInterrupt(uint32_t) -> void
 * - ReadConfigU8(uint32_t) const -> uint8_t
 * - ReadConfigU16(uint32_t) const -> uint16_t
 * - ReadConfigU32(uint32_t) const -> uint32_t
 * - ReadConfigU64(uint32_t) const -> uint64_t
 * - GetConfigGeneration() const -> uint32_t
 *
 * @tparam Traits 平台环境特征类型
 * @tparam Derived CRTP 派生类类型
 * @see virtio-v1.2#4 Virtio Transport Options
 */
template <VirtioEnvironmentTraits Traits, typename Derived>
class Transport {
 public:
  /**
   * @brief 设备状态位定义
   * @see virtio-v1.2#2.1 Device Status Field
   */
  enum DeviceStatus : uint32_t {
    /// 重置状态，驱动程序将此写入以重置设备
    kReset = 0,
    /// 表示客户操作系统已找到设备并识别为有效的 virtio 设备
    kAcknowledge = 1,
    /// 表示客户操作系统知道如何驱动该设备
    kDriver = 2,
    /// 表示驱动程序已准备好驱动设备（特性协商完成）
    kDriverOk = 4,
    /// 表示驱动程序已确认设备提供的所有功能
    kFeaturesOk = 8,
    /// 表示设备需要重置
    kDeviceNeedsReset = 64,
    /// 表示在客户机中出现问题，已放弃该设备
    kFailed = 128,
  };

  /**
   * @brief 重置设备
   *
   * 将状态寄存器写 0，使设备回到初始状态。
   * 重置后所有队列配置和特性协商都将失效。
   *
   * @see virtio-v1.2#2.1 Device Status Field
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  auto Reset() -> void { derived().SetStatus(kReset); }

  /**
   * @brief 检查设备是否需要重置
   *
   * 如果设备遇到严重错误，会设置 DEVICE_NEEDS_RESET 状态位。
   * 驱动程序应该检测到此状态后重新初始化设备。
   *
   * @return true 表示设备需要重置，false 表示设备正常
   * @see virtio-v1.2#2.1 Device Status Field
   */
  [[nodiscard]] auto NeedsReset() const -> bool {
    return (derived().GetStatus() & kDeviceNeedsReset) != 0;
  }

  /**
   * @brief 检查设备是否已激活（DRIVER_OK 已设置）
   *
   * @return true 表示设备已激活，false 表示未激活
   */
  [[nodiscard]] auto IsActive() const -> bool {
    return (derived().GetStatus() & kDriverOk) != 0;
  }

  /**
   * @brief 确认并清除设备中断
   *
   * 读取中断状态，如果有中断则确认清除。
   * 通用逻辑通过 CRTP 分发到子类的 GetInterruptStatus() 和 AckInterrupt()。
   *
   * @see virtio-v1.2#2.3 Notifications
   */
  auto AcknowledgeInterrupt() -> void {
    auto status = derived().GetInterruptStatus();
    if (status != 0) {
      derived().AckInterrupt(status);
    }
  }

 protected:
  /// @name 构造/析构函数（仅允许派生类使用）
  /// @{
  Transport() = default;
  ~Transport() = default;
  Transport(Transport&&) noexcept = default;
  auto operator=(Transport&&) noexcept -> Transport& = default;
  Transport(const Transport&) = delete;
  auto operator=(const Transport&) -> Transport& = delete;
  /// @}

 private:
  /// @brief CRTP 向下转型（非 const 版本）
  [[nodiscard]] auto derived() -> Derived& {
    return *static_cast<Derived*>(this);
  }
  /// @brief CRTP 向下转型（const 版本）
  [[nodiscard]] auto derived() const -> const Derived& {
    return *static_cast<const Derived*>(this);
  }
};

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_TRANSPORT_TRANSPORT_HPP_ */
