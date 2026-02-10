/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_SRC_INCLUDE_TRANSPORT_TRANSPORT_HPP_
#define VIRTIO_DRIVER_SRC_INCLUDE_TRANSPORT_TRANSPORT_HPP_

#include "defs.h"
#include "expected.hpp"

namespace virtio_driver {

/**
 * @brief Virtio 传输层抽象基类
 *
 * 该类提供了 virtio 设备传输层的统一接口，屏蔽底层传输机制的差异（MMIO、PCI
 * 等）。 传输层负责与设备进行通信，包括读写设备寄存器、配置
 * virtqueue、处理中断等。
 *
 * 主要职责：
 * - 设备标识和状态管理
 * - 特性协商
 * - Virtqueue 配置
 * - 设备通知和中断处理
 * - 配置空间访问
 *
 * @note 所有子类必须实现所有纯虚函数
 * @see virtio-v1.2#4 Virtio Transport Options
 */
template <class LogFunc = std::nullptr_t>
class Transport : public Logger<LogFunc> {
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
   * @brief 析构函数
   */
  virtual ~Transport() = default;

  /**
   * @brief 获取 Virtio Subsystem Device ID
   *
   * Device ID 用于标识设备类型（例如：1 = 网络设备，2 = 块设备）。
   *
   * @return 设备类型 ID
   * @see virtio-v1.2#5 Device Types
   */
  [[nodiscard]] virtual auto GetDeviceId() const -> uint32_t = 0;

  /**
   * @brief 获取 Virtio Subsystem Vendor ID
   *
   * Vendor ID 用于标识设备供应商（PCI Vendor ID）。
   *
   * @return 供应商 ID
   * @see virtio-v1.2#4.1.2 PCI Device Discovery
   */
  [[nodiscard]] virtual auto GetVendorId() const -> uint32_t = 0;

  /**
   * @brief 读取设备状态寄存器
   *
   * 状态寄存器反映设备当前的初始化状态和运行状态。
   *
   * @return 当前设备状态（DeviceStatus 位的组合）
   * @see virtio-v1.2#2.1 Device Status Field
   */
  [[nodiscard]] virtual auto GetStatus() const -> uint32_t = 0;

  /**
   * @brief 写入设备状态寄存器
   *
   * 驱动程序通过写入状态位来推进设备初始化流程。
   * 状态位应该累加设置（例如：先写 ACKNOWLEDGE，再写 DRIVER）。
   *
   * @param status 要设置的状态位（DeviceStatus 位的组合）
   * @see virtio-v1.2#2.1 Device Status Field
   */
  virtual auto SetStatus(uint32_t status) -> void = 0;

  /**
   * @brief 重置设备
   *
   * 将状态寄存器写 0，使设备回到初始状态。
   * 重置后所有队列配置和特性协商都将失效。
   *
   * @see virtio-v1.2#2.1 Device Status Field
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  auto Reset() -> void { SetStatus(kReset); }

  /**
   * @brief 读取设备支持的 64 位特性位
   *
   * 设备特性位表示设备支持的可选功能。
   * 驱动程序应读取此值，与自己支持的特性取交集后写回。
   *
   * @note 部分传输层（如 MMIO）需要写入选择寄存器，因此不能声明为 const
   *
   * @return 设备支持的特性位（64 位掩码）
   * @see virtio-v1.2#2.2 Feature Bits
   * @see virtio-v1.2#6 Reserved Feature Bits
   */
  [[nodiscard]] virtual auto GetDeviceFeatures() -> uint64_t = 0;

  /**
   * @brief 写入驱动程序接受的 64 位特性位
   *
   * 驱动程序将协商后的特性位写入此寄存器，表示同意使用这些功能。
   * 必须在设置 FEATURES_OK 状态位之前完成。
   *
   * @param features 驱动程序接受的特性位（应为设备特性的子集）
   * @see virtio-v1.2#2.2 Feature Bits
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  virtual auto SetDriverFeatures(uint64_t features) -> void = 0;

  /**
   * @brief 获取指定队列支持的最大队列大小
   *
   * 设备报告每个 virtqueue 支持的最大 queue_size（描述符数量）。
   * 返回 0 表示该队列索引无效或不可用。
   *
   * @note 部分传输层（如 MMIO）需要写入选择寄存器，因此不能声明为 const
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @return 最大队列大小（queue_num_max），0 表示队列不可用
   * @see virtio-v1.2#2.6 Split Virtqueues
   */
  [[nodiscard]] virtual auto GetQueueNumMax(uint32_t queue_idx) -> uint32_t = 0;

  /**
   * @brief 设置指定队列的队列大小
   *
   * 队列大小决定了描述符表、Available Ring 和 Used Ring 的条目数量。
   * 必须在设置队列地址之前调用。
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @param num 队列大小（必须 <= get_queue_num_max()，通常为 2 的幂）
   * @see virtio-v1.2#2.6 Split Virtqueues
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  virtual auto SetQueueNum(uint32_t queue_idx, uint32_t num) -> void = 0;

  /**
   * @brief 设置描述符表的物理地址
   *
   * 配置 virtqueue 描述符表在客户机物理内存中的位置。
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @param addr 描述符表的 64 位客户机物理地址（必须 16 字节对齐）
   * @see virtio-v1.2#2.6.2 The Virtqueue Descriptor Table
   */
  virtual auto SetQueueDesc(uint32_t queue_idx, uint64_t addr) -> void = 0;

  /**
   * @brief 设置 Available Ring 的物理地址
   *
   * 配置 Available Ring（Driver Area）在客户机物理内存中的位置。
   * 驱动程序通过此区域向设备提供可用缓冲区。
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @param addr Available Ring 的 64 位客户机物理地址（必须 2 字节对齐）
   * @see virtio-v1.2#2.6.3 The Virtqueue Available Ring
   */
  virtual auto SetQueueAvail(uint32_t queue_idx, uint64_t addr) -> void = 0;

  /**
   * @brief 设置 Used Ring 的物理地址
   *
   * 配置 Used Ring（Device Area）在客户机物理内存中的位置。
   * 设备通过此区域向驱动程序返回已处理的缓冲区。
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @param addr Used Ring 的 64 位客户机物理地址（必须 4 字节对齐）
   * @see virtio-v1.2#2.6.4 The Virtqueue Used Ring
   */
  virtual auto SetQueueUsed(uint32_t queue_idx, uint64_t addr) -> void = 0;

  /**
   * @brief 读取队列就绪状态
   *
   * 检查指定队列是否已配置完成并处于就绪状态。
   *
   * @note 部分传输层（如 MMIO）需要写入选择寄存器，因此不能声明为 const
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @return true 表示队列已就绪，false 表示未就绪
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] virtual auto GetQueueReady(uint32_t queue_idx) -> bool = 0;

  /**
   * @brief 设置队列就绪状态
   *
   * 在配置完队列大小和地址后，驱动程序必须将队列标记为就绪。
   * 设备只能使用已就绪的队列。
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @param ready true 表示队列已配置完成，false 表示禁用队列
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  virtual auto SetQueueReady(uint32_t queue_idx, bool ready) -> void = 0;

  /**
   * @brief 通知设备指定队列有新的可用缓冲区
   *
   * 驱动程序将缓冲区添加到 Available Ring 后，需要通过此方法通知设备。
   * 如果启用了中断抑制特性，可能不需要每次都通知。
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @see virtio-v1.2#2.6.8 Supplying Buffers to The Device
   * @see virtio-v1.2#2.7.21 Driver notifications
   */
  virtual auto NotifyQueue(uint32_t queue_idx) -> void = 0;

  /**
   * @brief 读取中断状态寄存器
   *
   * 获取设备当前的中断原因（Used Buffer 或 Configuration Change）。
   * 读取后应使用 ack_interrupt() 清除相应的状态位。
   *
   * @return 中断状态位（位 0：Used Buffer，位 1：Configuration Change）
   * @see virtio-v1.2#2.6.7 Used Buffer Notification
   * @see virtio-v1.2#4.2.2 MMIO Device Register Layout (InterruptStatus)
   */
  [[nodiscard]] virtual auto GetInterruptStatus() const -> uint32_t = 0;

  /**
   * @brief 确认并清除中断状态位
   *
   * 驱动程序处理完中断后，应写入相应的位来清除中断状态。
   *
   * @param ack_bits 要确认的中断位（位 0：Used Buffer，位 1：Configuration
   * Change）
   * @see virtio-v1.2#4.2.2 MMIO Device Register Layout (InterruptACK)
   */
  virtual auto AckInterrupt(uint32_t ack_bits) -> void = 0;

  // ========================================================================
  // 配置空间访问
  // @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
  // @see virtio-v1.2#4.2.2 MMIO Device Register Layout (Config)
  // ========================================================================

  /**
   * @brief 读取配置空间的 uint8_t 值
   *
   * 配置空间存储设备特定的配置信息，不同设备类型的配置空间布局不同。
   *
   * @param offset 相对于配置空间起始的偏移量（字节）
   * @return 读取的 8 位值
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] virtual auto ReadConfigU8(uint32_t offset) const -> uint8_t = 0;

  /**
   * @brief 读取配置空间的 uint16_t 值
   *
   * @param offset 相对于配置空间起始的偏移量（字节，必须 2 字节对齐）
   * @return 读取的 16 位值（little-endian）
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] virtual auto ReadConfigU16(uint32_t offset) const
      -> uint16_t = 0;

  /**
   * @brief 读取配置空间的 uint32_t 值
   *
   * @param offset 相对于配置空间起始的偏移量（字节，必须 4 字节对齐）
   * @return 读取的 32 位值（little-endian）
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] virtual auto ReadConfigU32(uint32_t offset) const
      -> uint32_t = 0;

  /**
   * @brief 读取配置空间的 uint64_t 值
   *
   * @param offset 相对于配置空间起始的偏移量（字节，必须 8 字节对齐）
   * @return 读取的 64 位值（little-endian）
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] virtual auto ReadConfigU64(uint32_t offset) const
      -> uint64_t = 0;

  /**
   * @brief 获取配置空间的 generation 计数
   *
   * 读取配置空间前后需检查 generation 是否一致，以确保读取的数据有效。
   * 如果前后 generation 不同，说明配置在读取过程中被修改，需要重新读取。
   *
   * @return 配置空间的 generation 计数（每次配置变更时递增）
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   * @see virtio-v1.2#4.2.2 MMIO Device Register Layout (ConfigGeneration)
   */
  [[nodiscard]] virtual auto GetConfigGeneration() const -> uint32_t = 0;

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
   * @param driver_features 驱动希望启用的特性位（与设备特性取交集）
   * @return 成功时返回实际协商后的特性位；失败返回错误
   * @note 初始化成功后，调用者还需配置队列并调用 Activate() 完成激活
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] auto Init(uint64_t driver_features) -> Expected<uint64_t> {
    // 重置设备
    Reset();

    // 设置 ACKNOWLEDGE 状态位
    SetStatus(kAcknowledge);

    // 设置 DRIVER 状态位
    SetStatus(kAcknowledge | kDriver);

    // 特性协商
    uint64_t device_features = GetDeviceFeatures();
    uint64_t negotiated_features = device_features & driver_features;
    SetDriverFeatures(negotiated_features);

    // 设置 FEATURES_OK 状态位
    SetStatus(kAcknowledge | kDriver | kFeaturesOk);

    // 验证 FEATURES_OK
    uint32_t status = GetStatus();
    if ((status & kFeaturesOk) == 0) {
      // 设备拒绝了特性组合
      SetStatus(status | kFailed);
      return std::unexpected(Error{ErrorCode::kFeatureNegotiationFailed});
    }

    return negotiated_features;
  }

  /**
   * @brief 配置并激活指定的 virtqueue
   *
   * 设置 virtqueue 的物理地址和大小，然后标记为就绪（步骤 7 的一部分）。
   * 必须在调用 Init() 之后、Activate() 之前完成。
   *
   * @param queue_idx 队列索引（从 0 开始）
   * @param desc_phys 描述符表的客户机物理地址（16 字节对齐）
   * @param avail_phys Available Ring 的客户机物理地址（2 字节对齐）
   * @param used_phys Used Ring 的客户机物理地址（4 字节对齐）
   * @param queue_size 队列大小（必须 <= GetQueueNumMax()）
   * @return 成功或失败
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] auto SetupQueue(uint32_t queue_idx, uint64_t desc_phys,
                                uint64_t avail_phys, uint64_t used_phys,
                                uint32_t queue_size) -> Expected<void> {
    // 检查队列大小是否有效
    uint32_t max_size = GetQueueNumMax(queue_idx);
    if (max_size == 0) {
      return std::unexpected(Error{ErrorCode::kQueueNotAvailable});
    }
    if (queue_size > max_size) {
      return std::unexpected(Error{ErrorCode::kQueueTooLarge});
    }

    // 配置队列
    SetQueueNum(queue_idx, queue_size);
    SetQueueDesc(queue_idx, desc_phys);
    SetQueueAvail(queue_idx, avail_phys);
    SetQueueUsed(queue_idx, used_phys);

    // 标记队列就绪
    SetQueueReady(queue_idx, true);

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
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] auto Activate() -> Expected<void> {
    uint32_t current_status = GetStatus();
    SetStatus(current_status | kDriverOk);

    // 验证设备是否正常激活
    uint32_t new_status = GetStatus();
    if ((new_status & kDeviceNeedsReset) != 0) {
      return std::unexpected(Error{ErrorCode::kDeviceError});
    }

    return {};
  }

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
    return (GetStatus() & kDeviceNeedsReset) != 0;
  }

  /**
   * @brief 检查设备是否已激活（DRIVER_OK 已设置）
   *
   * @return true 表示设备已激活，false 表示未激活
   */
  [[nodiscard]] auto IsActive() const -> bool {
    return (GetStatus() & kDriverOk) != 0;
  }
};

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_SRC_INCLUDE_TRANSPORT_TRANSPORT_HPP_ */
