/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_INCLUDE_DEVICE_DEVICE_INITIALIZER_HPP_
#define VIRTIO_DRIVER_INCLUDE_DEVICE_DEVICE_INITIALIZER_HPP_

#include "expected.hpp"
#include "transport/transport.hpp"

namespace virtio_driver {

/**
 * @brief Virtio 设备初始化器
 *
 * 负责编排 virtio 设备的初始化流程，独立于底层传输机制（MMIO、PCI 等）。
 * 实现 virtio 规范定义的标准初始化序列。
 *
 * 主要职责：
 * - 执行完整的设备初始化序列（特性协商、状态设置等）
 * - 配置和激活 virtqueue
 * - 提供设备初始化流程的统一抽象
 *
 * 使用示例：
 * @code
 * MmioTransport<> transport(base_addr);
 * DeviceInitializer<> initializer(transport);
 *
 * // 初始化设备并协商特性
 * auto features_result = initializer.Init(my_driver_features);
 * if (!features_result) { handle_error(); }
 *
 * // 配置队列
 * initializer.SetupQueue(0, desc_phys, avail_phys, used_phys, queue_size);
 *
 * // 激活设备
 * initializer.Activate();
 * @endcode
 *
 * @tparam LogFunc 日志函数类型（可选）
 * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
 */
template <class LogFunc = std::nullptr_t>
class DeviceInitializer : public Logger<LogFunc> {
 public:
  /**
   * @brief 构造函数
   *
   * @param transport 传输层引用（必须在 DeviceInitializer 生命周期内保持有效）
   * @pre transport.IsValid() == true
   */
  explicit DeviceInitializer(Transport<LogFunc>& transport)
      : transport_(transport) {}

  /**
   * @brief 执行 virtio 设备初始化序列
   *
   * 完整执行设备初始化流程（步骤 1-6）：
   * 1. 重置设备（写入 0 到 status 寄存器）
   * 2. 设置 ACKNOWLEDGE 状态位（识别为 virtio 设备）
   * 3. 设置 DRIVER 状态位（驱动程序知道如何驱动）
   * 4. 读取设备特性，与 driver_features 取交集后写回
   * 5. 设置 FEATURES_OK 状态位
   * 6. 重新读取验证 FEATURES_OK 是否仍被设置（设备可能拒绝某些特性组合）
   *
   * @param driver_features 驱动程序希望启用的特性位（将与设备特性取交集）
   * @return 成功时返回实际协商后的特性位；失败返回错误
   * @pre transport_.IsValid() == true（传输层已成功初始化）
   * @post 初始化成功后，调用者还需配置队列并调用 Activate() 完成激活
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] auto Init(uint64_t driver_features) -> Expected<uint64_t> {
    // 检查传输层是否有效
    if (!transport_.IsValid()) {
      Logger<LogFunc>::Log("Transport layer not initialized");
      return std::unexpected(Error{ErrorCode::kTransportNotInitialized});
    }

    Logger<LogFunc>::Log("Starting device initialization sequence");

    // 步骤 1: 重置设备
    transport_.Reset();

    // 步骤 2: 设置 ACKNOWLEDGE 状态位
    transport_.SetStatus(Transport<LogFunc>::kAcknowledge);
    Logger<LogFunc>::Log("Set ACKNOWLEDGE status");

    // 步骤 3: 设置 DRIVER 状态位
    transport_.SetStatus(Transport<LogFunc>::kAcknowledge |
                         Transport<LogFunc>::kDriver);
    Logger<LogFunc>::Log("Set DRIVER status");

    // 步骤 4: 特性协商
    uint64_t device_features = transport_.GetDeviceFeatures();
    uint64_t negotiated_features = device_features & driver_features;
    Logger<LogFunc>::Log(
        "Feature negotiation: device=0x%016llx, driver=0x%016llx, "
        "negotiated=0x%016llx",
        static_cast<unsigned long long>(device_features),
        static_cast<unsigned long long>(driver_features),
        static_cast<unsigned long long>(negotiated_features));

    transport_.SetDriverFeatures(negotiated_features);

    // 步骤 5: 设置 FEATURES_OK 状态位
    transport_.SetStatus(Transport<LogFunc>::kAcknowledge |
                         Transport<LogFunc>::kDriver |
                         Transport<LogFunc>::kFeaturesOk);
    Logger<LogFunc>::Log("Set FEATURES_OK status");

    // 步骤 6: 验证 FEATURES_OK
    uint32_t status = transport_.GetStatus();
    if ((status & Transport<LogFunc>::kFeaturesOk) == 0) {
      // 设备拒绝了特性组合
      Logger<LogFunc>::Log("Device rejected feature negotiation");
      transport_.SetStatus(status | Transport<LogFunc>::kFailed);
      return std::unexpected(Error{ErrorCode::kFeatureNegotiationFailed});
    }

    Logger<LogFunc>::Log("Device initialization sequence completed");
    return negotiated_features;
  }

  /**
   * @brief 配置并激活指定的 virtqueue
   *
   * 设置 virtqueue 的物理地址和大小，然后标记为就绪（步骤 7 的一部分）。
   * 必须在调用 Init() 之后、Activate() 之前完成。
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @param desc_phys 描述符表的客户机物理地址（必须 16 字节对齐）
   * @param avail_phys Available Ring 的客户机物理地址（必须 2 字节对齐）
   * @param used_phys Used Ring 的客户机物理地址（必须 4 字节对齐）
   * @param queue_size 队列大小（必须 <= transport_.GetQueueNumMax()）
   * @return 成功或失败
   * @pre transport_.IsValid() == true
   * @pre Init() 已成功调用
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] auto SetupQueue(uint32_t queue_idx, uint64_t desc_phys,
                                uint64_t avail_phys, uint64_t used_phys,
                                uint32_t queue_size) -> Expected<void> {
    // 检查传输层是否有效
    if (!transport_.IsValid()) {
      Logger<LogFunc>::Log("Transport layer not initialized");
      return std::unexpected(Error{ErrorCode::kTransportNotInitialized});
    }

    Logger<LogFunc>::Log("Setting up queue %u (size=%u)", queue_idx,
                         queue_size);

    // 检查队列大小是否有效
    uint32_t max_size = transport_.GetQueueNumMax(queue_idx);
    if (max_size == 0) {
      Logger<LogFunc>::Log("Queue %u not available", queue_idx);
      return std::unexpected(Error{ErrorCode::kQueueNotAvailable});
    }
    if (queue_size > max_size) {
      Logger<LogFunc>::Log("Queue %u size %u exceeds max %u", queue_idx,
                           queue_size, max_size);
      return std::unexpected(Error{ErrorCode::kQueueTooLarge});
    }

    // 配置队列
    transport_.SetQueueNum(queue_idx, queue_size);
    transport_.SetQueueDesc(queue_idx, desc_phys);
    transport_.SetQueueAvail(queue_idx, avail_phys);
    transport_.SetQueueUsed(queue_idx, used_phys);

    // 标记队列就绪
    transport_.SetQueueReady(queue_idx, true);

    Logger<LogFunc>::Log(
        "Queue %u configured: desc=0x%016llx, avail=0x%016llx, used=0x%016llx",
        queue_idx, static_cast<unsigned long long>(desc_phys),
        static_cast<unsigned long long>(avail_phys),
        static_cast<unsigned long long>(used_phys));

    return {};
  }

  /**
   * @brief 激活设备，开始正常运行
   *
   * 设置 DRIVER_OK 状态位，完成设备初始化流程（步骤 8）。
   * 必须在所有队列配置完成后调用。
   * 调用后设备开始正常运行，可以处理队列中的请求。
   *
   * @return 成功或失败
   * @pre transport_.IsValid() == true
   * @pre Init() 已成功调用
   * @pre 所有需要的队列已通过 SetupQueue() 配置
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] auto Activate() -> Expected<void> {
    // 检查传输层是否有效
    if (!transport_.IsValid()) {
      Logger<LogFunc>::Log("Transport layer not initialized");
      return std::unexpected(Error{ErrorCode::kTransportNotInitialized});
    }

    Logger<LogFunc>::Log("Activating device");

    uint32_t current_status = transport_.GetStatus();
    transport_.SetStatus(current_status | Transport<LogFunc>::kDriverOk);

    // 验证设备是否正常激活
    uint32_t new_status = transport_.GetStatus();
    if ((new_status & Transport<LogFunc>::kDeviceNeedsReset) != 0) {
      Logger<LogFunc>::Log("Device activation failed: device needs reset");
      return std::unexpected(Error{ErrorCode::kDeviceError});
    }

    Logger<LogFunc>::Log("Device activated successfully");
    return {};
  }

  /**
   * @brief 获取底层传输层引用
   *
   * 允许访问底层传输层的其他功能（如读取配置空间、处理中断等）。
   *
   * @return 传输层的引用
   */
  [[nodiscard]] auto transport() -> Transport<LogFunc>& { return transport_; }

  [[nodiscard]] auto transport() const -> const Transport<LogFunc>& {
    return transport_;
  }

 private:
  /// 底层传输层引用
  Transport<LogFunc>& transport_;
};

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_INCLUDE_DEVICE_DEVICE_INITIALIZER_HPP_ */
