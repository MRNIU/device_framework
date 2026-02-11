/**
 * @file virtio_blk.hpp
 * @brief Virtio 块设备驱动
 * @copyright Copyright The virtio_driver Contributors
 * @see virtio-v1.2#5.2 Block Device
 */

#ifndef VIRTIO_DRIVER_DEVICE_VIRTIO_BLK_HPP_
#define VIRTIO_DRIVER_DEVICE_VIRTIO_BLK_HPP_

#include <atomic>
#include <utility>

#include "virtio_driver/defs.h"
#include "virtio_driver/device/device_initializer.hpp"
#include "virtio_driver/expected.hpp"
#include "virtio_driver/platform.h"
#include "virtio_driver/transport/mmio.hpp"
#include "virtio_driver/virt_queue/split.hpp"

namespace virtio_driver::blk {

/**
 * @brief 块设备特性位定义
 * @see virtio-v1.2#5.2.3 Feature bits
 *
 * 特性位用于在设备初始化期间协商设备功能。
 * 驱动程序通过读取设备特性位来确定设备支持哪些功能，
 * 并通过写入驱动程序特性位来确认要使用的功能。
 */
enum class BlkFeatureBit : uint64_t {
  /// 设备配置空间中 size_max 字段有效 (VIRTIO_BLK_F_SIZE_MAX)
  kSizeMax = 1ULL << 1,
  /// 设备配置空间中 seg_max 字段有效 (VIRTIO_BLK_F_SEG_MAX)
  kSegMax = 1ULL << 2,
  /// 设备配置空间中 geometry 字段有效 (VIRTIO_BLK_F_GEOMETRY)
  kGeometry = 1ULL << 4,
  /// 设备为只读设备 (VIRTIO_BLK_F_RO)
  kRo = 1ULL << 5,
  /// 设备配置空间中 blk_size 字段有效 (VIRTIO_BLK_F_BLK_SIZE)
  kBlkSize = 1ULL << 6,
  /// 设备支持缓存刷新命令 (VIRTIO_BLK_F_FLUSH)
  kFlush = 1ULL << 9,
  /// 设备配置空间中 topology 字段有效 (VIRTIO_BLK_F_TOPOLOGY)
  kTopology = 1ULL << 10,
  /// 设备可在回写和直写缓存模式间切换 (VIRTIO_BLK_F_CONFIG_WCE)
  kConfigWce = 1ULL << 11,
  /// 设备支持多队列 (VIRTIO_BLK_F_MQ)
  kMq = 1ULL << 12,
  /// 设备支持 discard 命令 (VIRTIO_BLK_F_DISCARD)
  kDiscard = 1ULL << 13,
  /// 设备支持 write zeroes 命令 (VIRTIO_BLK_F_WRITE_ZEROES)
  kWriteZeroes = 1ULL << 14,
  /// 设备支持提供存储生命周期信息 (VIRTIO_BLK_F_LIFETIME)
  kLifetime = 1ULL << 15,
  /// 设备支持 secure erase 命令 (VIRTIO_BLK_F_SECURE_ERASE)
  kSecureErase = 1ULL << 16,
};

/**
 * @brief 块设备配置空间布局
 * @see virtio-v1.2#5.2.4 Device configuration layout
 *
 * 设备配置空间包含设备的静态配置信息，如容量、最大段大小、
 * 几何信息、拓扑信息等。驱动程序通过传输层读取这些信息。
 *
 * @note 配置空间使用小端格式
 * @note 多字节字段需要使用 generation counter 机制确保读取一致性
 */
struct BlkConfig {
  /// 设备容量（以 512 字节扇区为单位）
  uint64_t capacity;
  /// 任意单个段的最大字节数（如果 VIRTIO_BLK_F_SIZE_MAX 被协商）
  uint32_t size_max;
  /// 单个请求中的最大段数（如果 VIRTIO_BLK_F_SEG_MAX 被协商）
  uint32_t seg_max;

  /// 磁盘几何信息（如果 VIRTIO_BLK_F_GEOMETRY 被协商）
  struct {
    /// 柱面数
    uint16_t cylinders;
    /// 磁头数
    uint8_t heads;
    /// 每磁道扇区数
    uint8_t sectors;
  } __attribute__((packed)) geometry;

  /// 块大小（字节），用于性能优化（如果 VIRTIO_BLK_F_BLK_SIZE 被协商）
  uint32_t blk_size;

  /// I/O 拓扑信息（如果 VIRTIO_BLK_F_TOPOLOGY 被协商）
  struct {
    /// 每个物理块包含的逻辑块数 (log2)
    uint8_t physical_block_exp;
    /// 第一个对齐逻辑块的偏移
    uint8_t alignment_offset;
    /// 建议的最小 I/O 大小（块数）
    uint16_t min_io_size;
    /// 建议的最优 I/O 大小（块数）
    uint32_t opt_io_size;
  } __attribute__((packed)) topology;

  /// 缓存模式：0=直写(writethrough)，1=回写(writeback)
  /// （如果 VIRTIO_BLK_F_CONFIG_WCE 被协商）
  uint8_t writeback;
  /// 保留字段，用于填充对齐
  uint8_t unused0[3];

  /// discard 命令的最大扇区数（如果 VIRTIO_BLK_F_DISCARD 被协商）
  uint32_t max_discard_sectors;
  /// discard 命令的最大段数（如果 VIRTIO_BLK_F_DISCARD 被协商）
  uint32_t max_discard_seg;
  /// discard 扇区对齐要求（如果 VIRTIO_BLK_F_DISCARD 被协商）
  uint32_t discard_sector_alignment;

  /// write zeroes 命令的最大扇区数（如果 VIRTIO_BLK_F_WRITE_ZEROES 被协商）
  uint32_t max_write_zeroes_sectors;
  /// write zeroes 命令的最大段数（如果 VIRTIO_BLK_F_WRITE_ZEROES 被协商）
  uint32_t max_write_zeroes_seg;
  /// write zeroes 是否可能导致 unmap（如果 VIRTIO_BLK_F_WRITE_ZEROES 被协商）
  uint8_t write_zeroes_may_unmap;
  /// 保留字段，用于填充对齐
  uint8_t unused1[3];

  /// secure erase 命令的最大扇区数（如果 VIRTIO_BLK_F_SECURE_ERASE 被协商）
  uint32_t max_secure_erase_sectors;
  /// secure erase 命令的最大段数（如果 VIRTIO_BLK_F_SECURE_ERASE 被协商）
  uint32_t max_secure_erase_seg;
  /// secure erase 扇区对齐要求（如果 VIRTIO_BLK_F_SECURE_ERASE 被协商）
  uint32_t secure_erase_sector_alignment;

  /// 请求队列数（如果 VIRTIO_BLK_F_MQ 被协商）
  uint16_t num_queues;
  /// 保留字段，用于未来扩展
  uint8_t unused2[6];
} __attribute__((packed));

/**
 * @brief 块设备配置空间字段偏移量
 * @see virtio-v1.2#5.2.4
 *
 * 这些常量定义了各个配置字段在配置空间中的字节偏移量，
 * 用于通过传输层 ReadConfigU* 系列函数访问配置空间。
 */
enum class BlkConfigOffset : uint32_t {
  kCapacity = 0,
  kSizeMax = 8,
  kSegMax = 12,
  kGeometryCylinders = 16,
  kGeometryHeads = 18,
  kGeometrySectors = 19,
  kBlkSize = 20,
  kTopologyPhysBlockExp = 24,
  kTopologyAlignOffset = 25,
  kTopologyMinIoSize = 26,
  kTopologyOptIoSize = 28,
  kWriteback = 32,
  kMaxDiscardSectors = 36,
  kMaxDiscardSeg = 40,
  kDiscardSectorAlignment = 44,
  kMaxWriteZeroesSectors = 48,
  kMaxWriteZeroesSeg = 52,
  kWriteZeroesMayUnmap = 56,
  kMaxSecureEraseSectors = 60,
  kMaxSecureEraseSeg = 64,
  kSecureEraseSectorAlignment = 68,
  kNumQueues = 72,
};

/**
 * @brief 块设备请求类型
 * @see virtio-v1.2#5.2.6 Device Operation
 *
 * 定义了块设备支持的各种请求操作类型。
 * 请求类型存储在请求头的 type 字段中。
 */
enum class ReqType : uint32_t {
  /// 读取 (VIRTIO_BLK_T_IN)
  kIn = 0,
  /// 写入 (VIRTIO_BLK_T_OUT)
  kOut = 1,
  /// 刷新缓存 (VIRTIO_BLK_T_FLUSH)
  kFlush = 4,
  /// 获取设备 ID (VIRTIO_BLK_T_GET_ID)
  kGetId = 8,
  /// 获取设备生命周期信息 (VIRTIO_BLK_T_GET_LIFETIME)
  kGetLifetime = 10,
  /// 丢弃扇区 (VIRTIO_BLK_T_DISCARD)
  kDiscard = 11,
  /// 写零 (VIRTIO_BLK_T_WRITE_ZEROES)
  kWriteZeroes = 13,
  /// 安全擦除 (VIRTIO_BLK_T_SECURE_ERASE)
  kSecureErase = 14,
};

/**
 * @brief 块设备请求状态
 * @see virtio-v1.2#5.2.6
 *
 * 设备在请求完成后，在响应中写入状态字节。
 */
enum class BlkStatus : uint8_t {
  /// 操作成功 (VIRTIO_BLK_S_OK)
  kOk = 0,
  /// IO 错误 (VIRTIO_BLK_S_IOERR)
  kIoErr = 1,
  /// 不支持的操作 (VIRTIO_BLK_S_UNSUPP)
  kUnsupp = 2,
};

/**
 * @brief 块设备请求头
 * @see virtio-v1.2#5.2.6 Device Operation
 *
 * 所有块设备请求都以此结构开头，位于第一个描述符中（设备只读）。
 *
 * @note 协议中所有字段采用小端格式
 * @note 请求头后跟数据缓冲区（可选），最后是状态字节（设备只写）
 */
struct BlkReqHeader {
  /// 请求类型 (ReqType)
  uint32_t type;
  /// 保留字段，必须为 0
  uint32_t reserved;
  /// 起始扇区号（仅对读/写请求有效，其他类型应设为 0）
  uint64_t sector;
} __attribute__((packed));

/**
 * @brief Discard/Write Zeroes/Secure Erase 请求段
 * @see virtio-v1.2#5.2.6
 *
 * VIRTIO_BLK_T_DISCARD、VIRTIO_BLK_T_WRITE_ZEROES 和
 * VIRTIO_BLK_T_SECURE_ERASE 请求的数据部分由一个或多个此结构的实例组成。
 */
struct BlkDiscardWriteZeroes {
  /// 起始扇区（以 512 字节为单位）
  uint64_t sector;
  /// 扇区数（以 512 字节为单位）
  uint32_t num_sectors;
  /// 标志位
  struct {
    /// 对于 write zeroes: 允许设备 unmap（取消映射）指定范围
    /// 对于 discard/secure erase: 保留，必须为 0
    uint32_t unmap : 1;
    /// 保留位，必须为 0
    uint32_t reserved : 31;
  } flags;
} __attribute__((packed));

/**
 * @brief 设备生命周期信息
 * @see virtio-v1.2#5.2.6
 *
 * VIRTIO_BLK_T_GET_LIFETIME 请求的响应数据。
 * 用于 eMMC/UFS 等存储设备报告磨损程度。
 */
struct BlkLifetime {
  /**
   * @brief Pre-EOL 信息常量
   */
  enum class PreEolInfo : uint16_t {
    /// 0: 值未定义 (VIRTIO_BLK_PRE_EOL_INFO_UNDEFINED)
    kUndefined = 0,
    /// 1: 正常，< 80% 保留块已消耗 (VIRTIO_BLK_PRE_EOL_INFO_NORMAL)
    kNormal = 1,
    /// 2: 警告，80% 保留块已消耗 (VIRTIO_BLK_PRE_EOL_INFO_WARNING)
    kWarning = 2,
    /// 3: 紧急，90% 保留块已消耗 (VIRTIO_BLK_PRE_EOL_INFO_URGENT)
    kUrgent = 3,
  };

  /// 预 EOL (End-Of-Life) 信息
  uint16_t pre_eol_info;
  /// 设备生命周期估计 A（SLC 单元磨损）
  /// 0x01-0x0a: 使用了 x*10% 生命周期
  /// 0x0b: 超出预估生命周期
  uint16_t device_lifetime_est_typ_a;
  /// 设备生命周期估计 B（MLC 单元磨损）
  /// 含义同 device_lifetime_est_typ_a
  uint16_t device_lifetime_est_typ_b;
} __attribute__((packed));

/// 标准扇区大小（字节）
static constexpr size_t kSectorSize = 512;

/// GET_ID 请求返回的设备 ID 字符串最大长度（字节）
/// @note 如果字符串长度为 20 字节，则没有 NUL 终止符
static constexpr size_t kDeviceIdMaxLen = 20;

/**
 * @brief Virtio 块设备驱动
 *
 * virtio 块设备是一个简单的虚拟块设备（即磁盘）。
 * 读写请求（以及其他特殊请求）被放置在请求队列中，由设备服务（可能乱序）。
 *
 * 该类封装了 VirtIO 块设备的完整生命周期：
 * - 传输层（MmioTransport）的创建和管理
 * - Split Virtqueue 的创建和管理
 * - 设备初始化序列（特性协商、队列配置、设备激活）
 * - 同步读写操作（请求构建、轮询、完成处理）
 *
 * 用户只需提供 MMIO 基地址、DMA 缓冲区和平台操作接口，
 * 即可通过 Read() / Write() 进行块设备操作。
 *
 * @tparam LogFunc 日志函数类型（可选）
 * @see virtio-v1.2#5.2 Block Device
 */
template <class LogFunc = std::nullptr_t>
class VirtioBlk : public Logger<LogFunc> {
 public:
  /**
   * @brief 计算 Virtqueue DMA 缓冲区所需的字节数
   *
   * 调用者应根据此值预分配页对齐、已清零的 DMA 内存。
   *
   * @param queue_size 队列大小（2 的幂，默认 128）
   * @return 所需的 DMA 内存字节数
   */
  [[nodiscard]] static constexpr auto CalcDmaSize(uint16_t queue_size = 128)
      -> size_t {
    return SplitVirtqueue::CalcSize(queue_size, false);
  }

  /**
   * @brief 创建并初始化块设备
   *
   * 内部自动完成：
   * 1. MmioTransport 初始化和验证
   * 2. SplitVirtqueue 创建
   * 3. VirtIO 设备初始化序列（重置、特性协商、队列配置、设备激活）
   *
   * @param mmio_base MMIO 设备基地址
   * @param vq_dma_buf 预分配的 DMA 缓冲区虚拟地址
   *        （页对齐，已清零，大小 >= CalcDmaSize(queue_size)）
   * @param platform 平台操作接口（提供虚拟地址到物理地址转换）
   * @param queue_size 队列大小（2 的幂，默认 128）
   * @param driver_features 额外的驱动特性位（VERSION_1 自动包含）
   * @return 成功返回 VirtioBlk 实例，失败返回错误
   * @see virtio-v1.2#3.1.1 Driver Requirements: Device Initialization
   */
  [[nodiscard]] static auto Create(uint64_t mmio_base, void* vq_dma_buf,
                                   const PlatformOps& platform,
                                   uint16_t queue_size = 128,
                                   uint64_t driver_features = 0)
      -> Expected<VirtioBlk> {
    // 1. 创建传输层
    MmioTransport<LogFunc> transport(mmio_base);
    if (!transport.IsValid()) {
      return std::unexpected(Error{ErrorCode::kTransportNotInitialized});
    }

    // 2. 创建 Virtqueue
    uint64_t dma_phys = platform.virt_to_phys(vq_dma_buf);
    SplitVirtqueue vq(vq_dma_buf, dma_phys, queue_size, false);
    if (!vq.IsValid()) {
      return std::unexpected(Error{ErrorCode::kInvalidArgument});
    }

    // 3. 设备初始化序列
    DeviceInitializer<LogFunc> initializer(transport);

    uint64_t wanted_features =
        static_cast<uint64_t>(ReservedFeature::kVersion1) | driver_features;
    auto negotiated_result = initializer.Init(wanted_features);
    if (!negotiated_result) {
      return std::unexpected(negotiated_result.error());
    }
    uint64_t negotiated = *negotiated_result;

    // 验证 VERSION_1 已协商成功
    if ((negotiated & static_cast<uint64_t>(ReservedFeature::kVersion1)) == 0) {
      initializer.Log("Device does not support VERSION_1 (modern mode)");
      return std::unexpected(Error{ErrorCode::kFeatureNegotiationFailed});
    }

    // 4. 配置队列
    const uint32_t queue_idx = 0;
    auto setup_result = initializer.SetupQueue(
        queue_idx, vq.DescPhys(), vq.AvailPhys(), vq.UsedPhys(), vq.Size());
    if (!setup_result) {
      return std::unexpected(setup_result.error());
    }

    // 5. 激活设备
    auto activate_result = initializer.Activate();
    if (!activate_result) {
      return std::unexpected(activate_result.error());
    }

    return VirtioBlk(std::move(transport), std::move(vq), platform, negotiated);
  }

  /**
   * @brief 同步读取一个扇区
   *
   * 提交读请求到设备，等待完成，处理结果并返回。
   * 所有内部细节（请求头、状态字节、轮询、描述符管理）均自动处理。
   *
   * @param sector 起始扇区号（以 512 字节为单位）
   * @param data 数据缓冲区（至少 kSectorSize 字节，必须位于 DMA 可访问内存）
   * @return 成功或失败
   * @note 缓冲区在函数返回前不得释放或修改
   * @see virtio-v1.2#5.2.6 Device Operation
   */
  [[nodiscard]] auto Read(uint64_t sector, uint8_t* data) -> Expected<void> {
    return DoSyncRequest(ReqType::kIn, sector, data);
  }

  /**
   * @brief 同步写入一个扇区
   *
   * 提交写请求到设备，等待完成，处理结果并返回。
   *
   * @param sector 起始扇区号（以 512 字节为单位）
   * @param data 数据缓冲区（至少 kSectorSize 字节，必须位于 DMA 可访问内存）
   * @return 成功或失败
   * @note 如果设备协商了 VIRTIO_BLK_F_RO，写请求将返回错误
   * @see virtio-v1.2#5.2.6 Device Operation
   */
  [[nodiscard]] auto Write(uint64_t sector, const uint8_t* data)
      -> Expected<void> {
    return DoSyncRequest(ReqType::kOut, sector, const_cast<uint8_t*>(data));
  }

  /**
   * @brief 读取块设备配置空间
   *
   * 读取设备的配置信息，包括容量、几何信息、拓扑信息等。
   * 配置空间的可用字段取决于协商的特性位。
   *
   * @return 块设备配置结构
   * @see virtio-v1.2#5.2.4 Device configuration layout
   */
  [[nodiscard]] auto ReadConfig() const -> BlkConfig {
    BlkConfig config{};

    config.capacity = transport_.ReadConfigU64(
        static_cast<uint32_t>(BlkConfigOffset::kCapacity));
    config.size_max = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kSizeMax));
    config.seg_max = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kSegMax));

    if ((negotiated_features_ &
         static_cast<uint64_t>(BlkFeatureBit::kGeometry)) != 0) {
      config.geometry.cylinders = transport_.ReadConfigU16(
          static_cast<uint32_t>(BlkConfigOffset::kGeometryCylinders));
      config.geometry.heads = transport_.ReadConfigU8(
          static_cast<uint32_t>(BlkConfigOffset::kGeometryHeads));
      config.geometry.sectors = transport_.ReadConfigU8(
          static_cast<uint32_t>(BlkConfigOffset::kGeometrySectors));
    }

    if ((negotiated_features_ &
         static_cast<uint64_t>(BlkFeatureBit::kBlkSize)) != 0) {
      config.blk_size = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kBlkSize));
    }

    if ((negotiated_features_ &
         static_cast<uint64_t>(BlkFeatureBit::kTopology)) != 0) {
      config.topology.physical_block_exp = transport_.ReadConfigU8(
          static_cast<uint32_t>(BlkConfigOffset::kTopologyPhysBlockExp));
      config.topology.alignment_offset = transport_.ReadConfigU8(
          static_cast<uint32_t>(BlkConfigOffset::kTopologyAlignOffset));
      config.topology.min_io_size = transport_.ReadConfigU16(
          static_cast<uint32_t>(BlkConfigOffset::kTopologyMinIoSize));
      config.topology.opt_io_size = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kTopologyOptIoSize));
    }

    if ((negotiated_features_ &
         static_cast<uint64_t>(BlkFeatureBit::kConfigWce)) != 0) {
      config.writeback = transport_.ReadConfigU8(
          static_cast<uint32_t>(BlkConfigOffset::kWriteback));
    }

    if ((negotiated_features_ &
         static_cast<uint64_t>(BlkFeatureBit::kDiscard)) != 0) {
      config.max_discard_sectors = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kMaxDiscardSectors));
      config.max_discard_seg = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kMaxDiscardSeg));
      config.discard_sector_alignment = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kDiscardSectorAlignment));
    }

    if ((negotiated_features_ &
         static_cast<uint64_t>(BlkFeatureBit::kWriteZeroes)) != 0) {
      config.max_write_zeroes_sectors = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kMaxWriteZeroesSectors));
      config.max_write_zeroes_seg = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kMaxWriteZeroesSeg));
      config.write_zeroes_may_unmap = transport_.ReadConfigU8(
          static_cast<uint32_t>(BlkConfigOffset::kWriteZeroesMayUnmap));
    }

    if ((negotiated_features_ &
         static_cast<uint64_t>(BlkFeatureBit::kSecureErase)) != 0) {
      config.max_secure_erase_sectors = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kMaxSecureEraseSectors));
      config.max_secure_erase_seg = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kMaxSecureEraseSeg));
      config.secure_erase_sector_alignment = transport_.ReadConfigU32(
          static_cast<uint32_t>(BlkConfigOffset::kSecureEraseSectorAlignment));
    }

    if ((negotiated_features_ & static_cast<uint64_t>(BlkFeatureBit::kMq)) !=
        0) {
      config.num_queues = transport_.ReadConfigU16(
          static_cast<uint32_t>(BlkConfigOffset::kNumQueues));
    }

    return config;
  }

  /**
   * @brief 获取设备容量
   *
   * @return 设备容量（以 512 字节扇区为单位）
   * @see virtio-v1.2#5.2.4
   */
  [[nodiscard]] auto GetCapacity() const -> uint64_t {
    return transport_.ReadConfigU64(
        static_cast<uint32_t>(BlkConfigOffset::kCapacity));
  }

  /**
   * @brief 获取协商后的特性位
   *
   * @return 设备和驱动程序都支持的特性位掩码
   */
  [[nodiscard]] auto GetNegotiatedFeatures() const -> uint64_t {
    return negotiated_features_;
  }

  /**
   * @brief 设备中断处理入口
   *
   * 确认设备中断并通知等待中的同步请求。用户应将此方法注册到
   * 对应 VirtIO 设备的硬件中断处理程序中（如通过 PLIC 注册的 ISR）。
   *
   * @note 此方法可在中断上下文中安全调用（ISR-safe）
   * @note 使用原子操作保证多核环境下的正确性
   * @see virtio-v1.2#2.3 Notifications
   */
  auto HandleInterrupt() -> void {
    uint32_t status = transport_.GetInterruptStatus();
    transport_.AckInterrupt(status);
    request_completed_.store(true, std::memory_order_release);
  }

  /// @name 移动/拷贝控制
  /// @{
  VirtioBlk(VirtioBlk&& other) noexcept
      : transport_(std::move(other.transport_)),
        vq_(std::move(other.vq_)),
        platform_(other.platform_),
        negotiated_features_(other.negotiated_features_),
        req_header_(other.req_header_),
        status_(other.status_),
        request_completed_(
            other.request_completed_.load(std::memory_order_relaxed)) {}
  auto operator=(VirtioBlk&& other) noexcept -> VirtioBlk& {
    if (this != &other) {
      transport_ = std::move(other.transport_);
      vq_ = std::move(other.vq_);
      platform_ = other.platform_;
      negotiated_features_ = other.negotiated_features_;
      req_header_ = other.req_header_;
      status_ = other.status_;
      request_completed_.store(
          other.request_completed_.load(std::memory_order_relaxed),
          std::memory_order_relaxed);
    }
    return *this;
  }
  VirtioBlk(const VirtioBlk&) = delete;
  auto operator=(const VirtioBlk&) -> VirtioBlk& = delete;
  ~VirtioBlk() = default;
  /// @}

 private:
  /**
   * @brief 私有构造函数
   *
   * 只能通过 Create() 静态工厂方法创建实例。
   */
  VirtioBlk(MmioTransport<LogFunc> transport, SplitVirtqueue vq,
            PlatformOps platform, uint64_t features)
      : transport_(std::move(transport)),
        vq_(std::move(vq)),
        platform_(platform),
        negotiated_features_(features),
        req_header_{},
        status_(0),
        request_completed_(false) {}

  /**
   * @brief 同步请求的内部实现
   *
   * 完成从请求提交到结果返回的完整流程：
   * 1. 分配描述符并组成描述符链
   * 2. 提交到 Available Ring 并通知设备
   * 3. 自旋等待中断驱动的完成通知（HandleInterrupt 设置原子标志）
   * 4. 回收描述符
   * 5. 检查设备返回的状态
   *
   * @param type 请求类型（kIn/kOut）
   * @param sector 起始扇区号
   * @param data 数据缓冲区指针
   * @return 成功或失败
   * @see virtio-v1.2#5.2.6 Device Operation
   */
  [[nodiscard]] auto DoSyncRequest(ReqType type, uint64_t sector, uint8_t* data)
      -> Expected<void> {
    if (data == nullptr) {
      return std::unexpected(Error{ErrorCode::kInvalidArgument});
    }

    // 填充请求头
    req_header_.type = static_cast<uint32_t>(type);
    req_header_.reserved = 0;
    req_header_.sector = sector;
    status_ = 0xFF;

    // 分配 3 个描述符：header -> data -> status
    auto desc0_result = vq_.AllocDesc();
    if (!desc0_result.has_value()) {
      return std::unexpected(desc0_result.error());
    }
    uint16_t desc0 = *desc0_result;

    auto desc1_result = vq_.AllocDesc();
    if (!desc1_result.has_value()) {
      (void)vq_.FreeDesc(desc0);
      return std::unexpected(desc1_result.error());
    }
    uint16_t desc1 = *desc1_result;

    auto desc2_result = vq_.AllocDesc();
    if (!desc2_result.has_value()) {
      (void)vq_.FreeDesc(desc0);
      (void)vq_.FreeDesc(desc1);
      return std::unexpected(desc2_result.error());
    }
    uint16_t desc2 = *desc2_result;

    // 描述符 0：请求头（设备只读）
    auto* d0 = *vq_.GetDesc(desc0);
    d0->addr = platform_.virt_to_phys(&req_header_);
    d0->len = sizeof(BlkReqHeader);
    d0->flags = SplitVirtqueue::kDescFNext;
    d0->next = desc1;

    // 描述符 1：数据缓冲区
    auto* d1 = *vq_.GetDesc(desc1);
    d1->addr = platform_.virt_to_phys(data);
    d1->len = kSectorSize;
    if (type == ReqType::kIn) {
      d1->flags = SplitVirtqueue::kDescFNext | SplitVirtqueue::kDescFWrite;
    } else {
      d1->flags = SplitVirtqueue::kDescFNext;
    }
    d1->next = desc2;

    // 描述符 2：状态字节（设备只写）
    auto* d2 = *vq_.GetDesc(desc2);
    d2->addr = platform_.virt_to_phys(&status_);
    d2->len = sizeof(uint8_t);
    d2->flags = SplitVirtqueue::kDescFWrite;
    d2->next = 0;

    // 重置完成标志（在提交请求之前）
    request_completed_.store(false, std::memory_order_relaxed);

    // 内存屏障：确保完成标志重置 + 描述符写入对所有核心/设备可见
    std::atomic_thread_fence(std::memory_order_seq_cst);

    // 提交到 Available Ring
    vq_.Submit(desc0);

    // 内存屏障：确保 Available Ring 更新对设备可见
    std::atomic_thread_fence(std::memory_order_seq_cst);

    // 通知设备
    transport_.NotifyQueue(0);

    // 等待中断驱动的完成通知
    // HandleInterrupt() 在硬件中断处理程序中被调用后设置 request_completed_
    static constexpr uint32_t kMaxSpinIterations = 100000000;
    uint32_t spin_count = 0;
    while (!request_completed_.load(std::memory_order_acquire) &&
           spin_count < kMaxSpinIterations) {
      ++spin_count;
    }

    if (!vq_.HasUsed()) {
      (void)vq_.FreeDesc(desc0);
      (void)vq_.FreeDesc(desc1);
      (void)vq_.FreeDesc(desc2);
      return std::unexpected(Error{ErrorCode::kTimeout});
    }

    // 处理完成的请求
    ProcessUsed();

    // 检查设备状态
    if (status_ != static_cast<uint8_t>(BlkStatus::kOk)) {
      if (status_ == static_cast<uint8_t>(BlkStatus::kIoErr)) {
        return std::unexpected(Error{ErrorCode::kIoError});
      }
      if (status_ == static_cast<uint8_t>(BlkStatus::kUnsupp)) {
        return std::unexpected(Error{ErrorCode::kNotSupported});
      }
      return std::unexpected(Error{ErrorCode::kDeviceError});
    }

    return {};
  }

  /**
   * @brief 处理已完成的请求，释放描述符
   */
  auto ProcessUsed() -> void {
    while (vq_.HasUsed()) {
      auto result = vq_.PopUsed();
      if (!result.has_value()) {
        break;
      }

      auto elem = *result;
      uint16_t idx = static_cast<uint16_t>(elem.id);

      // 释放描述符链
      while (true) {
        auto desc_result = vq_.GetDesc(idx);
        if (!desc_result.has_value()) {
          break;
        }
        auto* desc = *desc_result;
        uint16_t next = desc->next;
        bool has_next = (desc->flags & SplitVirtqueue::kDescFNext) != 0;
        (void)vq_.FreeDesc(idx);
        if (!has_next) {
          break;
        }
        idx = next;
      }
    }
  }

  /// 传输层（MMIO）
  MmioTransport<LogFunc> transport_;
  /// Virtqueue（Split）
  SplitVirtqueue vq_;
  /// 平台操作接口
  PlatformOps platform_;
  /// 协商后的特性位掩码
  uint64_t negotiated_features_;
  /// 内部请求头（DMA 可访问）
  alignas(16) BlkReqHeader req_header_;
  /// 内部状态字节（DMA 可访问）
  alignas(16) uint8_t status_;
  /// 请求完成标志（由 HandleInterrupt 在中断上下文中设置，多核安全）
  std::atomic<bool> request_completed_;
};

static_assert(std::atomic<bool>::is_always_lock_free,
              "atomic<bool> must be lock-free for ISR safety");

}  // namespace virtio_driver::blk

#endif /* VIRTIO_DRIVER_DEVICE_VIRTIO_BLK_HPP_ */
