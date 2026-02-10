/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_SRC_INCLUDE_TRANSPORT_MMIO_HPP_
#define VIRTIO_DRIVER_SRC_INCLUDE_TRANSPORT_MMIO_HPP_

#include "transport.hpp"

namespace virtio_driver {

/**
 * @brief MMIO 中断状态位
 * @see virtio-v1.2#4.2.2 Table 4.1: MMIO Device Register Layout
 */
enum class InterruptStatus : uint32_t {
  /// 设备在至少一个活动虚拟队列中使用了缓冲区 Used Buffer Notification
  kUsedBuffer = 0x01,
  /// 设备配置已更改 Configuration Change Notification
  kConfigChange = 0x02,
};

/**
 * @brief MMIO 魔数: little-endian "virt" = 0x74726976
 * @see virtio-v1.2#4.2.2
 */
static constexpr uint32_t kMmioMagicValue = 0x74726976;

/**
 * @brief MMIO 版本号（v2 for virtio 1.0+, legacy 使用 v1）
 * @see virtio-v1.2#4.2.2
 */
static constexpr uint32_t kMmioVersion = 0x02;

/**
 * @brief Virtio MMIO 传输层
 *
 * MMIO virtio 设备通过一组内存映射的控制寄存器和设备特定配置空间进行访问。
 * 所有寄存器值采用小端格式组织。
 *
 * 寄存器布局包括：
 * - 魔数（MagicValue）: 0x74726976
 * - 版本号（Version）: 0x2（virtio 1.0+）
 * - 设备/供应商 ID
 * - 特性位配置
 * - 队列配置
 * - 中断状态与确认
 * - 设备状态
 * - 共享内存区域（可选，需要相应特性支持）
 * - 队列重置（可选，需要 VIRTIO_F_RING_RESET 特性）
 * - 设备特定配置空间（从 0x100 开始）
 *
 * @see virtio-v1.2#4.2 Virtio Over MMIO
 */
class MmioTransport final : public Transport {
 public:
  /**
   * @brief MMIO 寄存器偏移量
   * @see virtio-v1.2#4.2.2 MMIO Device Register Layout
   */
  enum MmioReg : size_t {
    kMagicValue = 0x000,
    kVersion = 0x004,
    kDeviceId = 0x008,
    kVendorId = 0x00C,
    kDeviceFeatures = 0x010,
    kDeviceFeaturesSel = 0x014,
    // 0x018 ~ 0x01F: reserved
    kDriverFeatures = 0x020,
    kDriverFeaturesSel = 0x024,
    // 0x028 ~ 0x02F: reserved
    kQueueSel = 0x030,
    kQueueNumMax = 0x034,
    kQueueNum = 0x038,
    // 0x03C ~ 0x043: reserved
    kQueueReady = 0x044,
    // 0x048 ~ 0x04F: reserved
    kQueueNotify = 0x050,
    // 0x054 ~ 0x05F: reserved
    kInterruptStatus = 0x060,
    kInterruptAck = 0x064,
    // 0x068 ~ 0x06F: reserved
    kStatus = 0x070,
    // 0x074 ~ 0x07F: reserved
    kQueueDescLow = 0x080,
    kQueueDescHigh = 0x084,
    // 0x088 ~ 0x08F: reserved
    kQueueDriverLow = 0x090,
    kQueueDriverHigh = 0x094,
    // 0x098 ~ 0x09F: reserved
    kQueueDeviceLow = 0x0A0,
    kQueueDeviceHigh = 0x0A4,
    // 0x0A8 ~ 0x0AB: reserved
    kShmSel = 0x0AC,
    kShmLenLow = 0x0B0,
    kShmLenHigh = 0x0B4,
    kShmBaseLow = 0x0B8,
    kShmBaseHigh = 0x0BC,
    kQueueReset = 0x0C0,
    // 0x0C4 ~ 0x0FB: reserved
    kConfigGeneration = 0x0FC,
    kConfig = 0x100,
  };

  /**
   * @brief 根据 MMIO 寄存器基地址创建传输层实例
   *
   * 会验证魔数和版本号
   * @param base MMIO 寄存器基地址（虚拟地址，需已映射）
   * @return 成功返回 MmioTransport 实例，失败返回错误
   */
  [[nodiscard]] static auto create(uintptr_t base_addr)
      -> Result<MmioTransport> {
    MmioTransport transport(base_addr);

    // 验证魔数 0x74726976 ("virt")
    // @see virtio-v1.2#4.2.2.2
    uint32_t magic = transport.Read<uint32_t>(MmioReg::kMagicValue);
    if (magic != kMmioMagicValue) {
      return ErrorCode::kInvalidMagic;
    }

    // 验证版本号（必须为 2，即 virtio modern）
    // @see virtio-v1.2#4.2.2.2
    uint32_t version = transport.Read<uint32_t>(MmioReg::kVersion);
    if (version != kMmioVersion) {
      return ErrorCode::kInvalidVersion;
    }

    // 设备 ID 为 0 表示不存在设备
    uint32_t device_id = transport.Read<uint32_t>(MmioReg::kDeviceId);
    if (device_id == 0) {
      return ErrorCode::kInvalidDeviceId;
    }

    return transport;
  }

  // ========================================================================
  // Transport 接口实现
  // ========================================================================

  [[nodiscard]] auto GetDeviceId() const -> uint32_t override {
    return Read<uint32_t>(MmioReg::kDeviceId);
  }

  [[nodiscard]] auto GetVendorId() const -> uint32_t override {
    return Read<uint32_t>(MmioReg::kVendorId);
  }

  [[nodiscard]] auto GetStatus() const -> uint32_t override {
    return Read<uint32_t>(MmioReg::kStatus);
  }

  auto SetStatus(uint32_t status) -> void override {
    Write<uint32_t>(MmioReg::kStatus, status);
  }

  /**
   * @brief 读取 64 位设备特性
   *
   * 需要分两次 32 位读取（低 32 位和高 32 位）
   * 
   * @note 该操作需要写入 DeviceFeaturesSel 寄存器来选择读取哪 32 位，
   *       虽然不改变 C++ 对象状态，但涉及硬件寄存器写入，因此不能声明为 const
   *
   * @return 设备支持的 64 位特性位
   * @see virtio-v1.2#4.2.2.1
   */
  [[nodiscard]] auto GetDeviceFeatures() -> uint64_t override {
    // 选择特性位 [31:0]
    Write<uint32_t>(MmioReg::kDeviceFeaturesSel, 0);
    uint64_t lo = Read<uint32_t>(MmioReg::kDeviceFeatures);

    // 选择特性位 [63:32]
    Write<uint32_t>(MmioReg::kDeviceFeaturesSel, 1);
    uint64_t hi = Read<uint32_t>(MmioReg::kDeviceFeatures);

    return (hi << 32) | lo;
  }

  /**
   * @brief 写入 64 位驱动特性
   *
   * 需要分两次 32 位写入（低 32 位和高 32 位）
   *
   * @param features 驱动程序接受的特性位
   * @see virtio-v1.2#4.2.2.1
   */
  auto SetDriverFeatures(uint64_t features) -> void override {
    Write<uint32_t>(MmioReg::kDriverFeaturesSel, 0);
    Write<uint32_t>(MmioReg::kDriverFeatures, static_cast<uint32_t>(features));

    Write<uint32_t>(MmioReg::kDriverFeaturesSel, 1);
    Write<uint32_t>(MmioReg::kDriverFeatures,
                    static_cast<uint32_t>(features >> 32));
  }

  /**
   * @brief 获取队列最大容量
   *
   * 写入 QueueSel 选择队列后读取 QueueNumMax
   * 
   * @note 该操作需要写入 QueueSel 寄存器，因此不能声明为 const
   *
   * @param queue_idx 队列索引
   * @return 队列最大大小
   * @see virtio-v1.2#4.2.3.2
   */
  [[nodiscard]] auto GetQueueNumMax(uint32_t queue_idx)
      -> uint32_t override {
    Write<uint32_t>(MmioReg::kQueueSel, queue_idx);
    return Read<uint32_t>(MmioReg::kQueueNumMax);
  }

  auto SetQueueNum(uint32_t queue_idx, uint32_t num) -> void override {
    Write<uint32_t>(MmioReg::kQueueSel, queue_idx);
    Write<uint32_t>(MmioReg::kQueueNum, num);
  }

  /**
   * @brief 设置描述符表物理地址
   *
   * MMIO 寄存器为 32 位，需要分高低两次写入
   *
   * @param queue_idx 队列索引
   * @param addr 描述符表的 64 位物理地址
   */
  auto SetQueueDesc(uint32_t queue_idx, uint64_t addr) -> void override {
    Write<uint32_t>(MmioReg::kQueueSel, queue_idx);
    Write<uint32_t>(MmioReg::kQueueDescLow, static_cast<uint32_t>(addr));
    Write<uint32_t>(MmioReg::kQueueDescHigh, static_cast<uint32_t>(addr >> 32));
  }

  /**
   * @brief 设置 Available Ring 物理地址
   *
   * @param queue_idx 队列索引
   * @param addr Available Ring 的 64 位物理地址
   */
  auto SetQueueAvail(uint32_t queue_idx, uint64_t addr) -> void override {
    Write<uint32_t>(MmioReg::kQueueSel, queue_idx);
    Write<uint32_t>(MmioReg::kQueueDriverLow, static_cast<uint32_t>(addr));
    Write<uint32_t>(MmioReg::kQueueDriverHigh,
                    static_cast<uint32_t>(addr >> 32));
  }

  /**
   * @brief 设置 Used Ring 物理地址
   *
   * @param queue_idx 队列索引
   * @param addr Used Ring 的 64 位物理地址
   */
  auto SetQueueUsed(uint32_t queue_idx, uint64_t addr) -> void override {
    Write<uint32_t>(MmioReg::kQueueSel, queue_idx);
    Write<uint32_t>(MmioReg::kQueueDeviceLow, static_cast<uint32_t>(addr));
    Write<uint32_t>(MmioReg::kQueueDeviceHigh,
                    static_cast<uint32_t>(addr >> 32));
  }

  [[nodiscard]] auto GetQueueReady(uint32_t queue_idx) -> bool override {
    Write<uint32_t>(MmioReg::kQueueSel, queue_idx);
    return Read<uint32_t>(MmioReg::kQueueReady) != 0;
  }

  auto SetQueueReady(uint32_t queue_idx, bool ready) -> void override {
    Write<uint32_t>(MmioReg::kQueueSel, queue_idx);
    Write<uint32_t>(MmioReg::kQueueReady, ready ? 1 : 0);
  }

  /**
   * @brief 通知设备有新的可用缓冲区
   *
   * @param queue_idx 队列索引
   * @see virtio-v1.2#4.2.3.3
   */
  auto NotifyQueue(uint32_t queue_idx) -> void override {
    Write<uint32_t>(MmioReg::kQueueNotify, queue_idx);
  }

  [[nodiscard]] auto GetInterruptStatus() const -> uint32_t override {
    return Read<uint32_t>(MmioReg::kInterruptStatus);
  }

  auto AckInterrupt(uint32_t ack_bits) -> void override {
    Write<uint32_t>(MmioReg::kInterruptAck, ack_bits);
  }

  /**
   * @brief 读取配置空间 8 位值
   *
   * @param offset 相对于配置空间起始的偏移量
   * @return 8 位配置值
   * @see virtio-v1.2#4.2.2.2 (Config[] at offset 0x100+)
   */
  [[nodiscard]] auto ReadConfigU8(uint32_t offset) const -> uint8_t override {
    return Read<uint8_t>(MmioReg::kConfig + offset);
  }

  /**
   * @brief 读取配置空间 16 位值
   *
   * @param offset 相对于配置空间起始的偏移量
   * @return 16 位配置值
   */
  [[nodiscard]] auto ReadConfigU16(uint32_t offset) const -> uint16_t override {
    return Read<uint16_t>(MmioReg::kConfig + offset);
  }

  /**
   * @brief 读取配置空间 32 位值
   *
   * @param offset 相对于配置空间起始的偏移量
   * @return 32 位配置值
   */
  [[nodiscard]] auto ReadConfigU32(uint32_t offset) const -> uint32_t override {
    return Read<uint32_t>(MmioReg::kConfig + offset);
  }

  /**
   * @brief 读取配置空间 64 位值
   *
   * 使用 generation counter 机制保证读取的 64 位配置数据一致性：
   * 1. 读取 ConfigGeneration
   * 2. 读取配置数据
   * 3. 再次读取 ConfigGeneration
   * 4. 如果两次 generation 不同，说明配置在读取过程中被修改，需要重试
   *
   * @param offset 相对于配置空间起始的偏移量
   * @return 64 位配置值（保证一致性）
   * @see virtio-v1.2#2.5.1 Driver Requirements: Device Configuration Space
   * @see virtio-v1.2#4.2.2 MMIO Device Register Layout (ConfigGeneration)
   */
  [[nodiscard]] auto ReadConfigU64(uint32_t offset) const -> uint64_t override {
    uint32_t gen1;
    uint32_t gen2;
    uint64_t value;
    
    // 循环直到读取到一致的配置（generation counter 相同）
    do {
      gen1 = GetConfigGeneration();
      
      auto ptr = reinterpret_cast<volatile uint32_t*>(base_ + MmioReg::kConfig + offset);
      uint64_t lo = ptr[0];
      uint64_t hi = ptr[1];
      value = (hi << 32) | lo;
      
      gen2 = GetConfigGeneration();
    } while (gen1 != gen2);
    
    return value;
  }

  [[nodiscard]] auto GetConfigGeneration() const -> uint32_t override {
    return Read<uint32_t>(MmioReg::kConfigGeneration);
  }

  /// 获取 MMIO 基地址
  [[nodiscard]] auto base() const -> uintptr_t { return base_; }

 private:
  /**
   * @brief 构造函数
   *
   * @param base MMIO 寄存器基地址
   */
  explicit MmioTransport(uintptr_t base) : base_(base) {}

  /**
   * @brief 从 MMIO 寄存器读取指定类型的值
   *
   * @tparam T 要读取的数据类型（uint8_t, uint16_t, uint32_t, uint64_t）
   * @param offset 寄存器偏移量
   * @return 读取的值
   * @note const 指 C++ 对象状态不变，硬件寄存器读取不影响对象状态
   */
  template <typename T>
  [[nodiscard]] auto Read(size_t offset) const -> T {
    return *reinterpret_cast<volatile T*>(base_ + offset);
  }

  /**
   * @brief 向 MMIO 寄存器写入指定类型的值
   *
   * @tparam T 要写入的数据类型（uint8_t, uint16_t, uint32_t, uint64_t）
   * @param offset 寄存器偏移量
   * @param val 要写入的值
   * @note const 因为写入目标是硬件寄存器，不改变 C++ 对象状态。
   *       MMIO 某些读操作需要先写选择寄存器（如 DEVICE_FEATURES_SEL）
   */
  template <typename T>
  auto Write(size_t offset, T val) const -> void {
    *reinterpret_cast<volatile T*>(base_ + offset) = val;
  }

  /// MMIO 寄存器基地址（虚拟地址）
  uintptr_t base_;
};

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_SRC_INCLUDE_TRANSPORT_MMIO_HPP_ */
