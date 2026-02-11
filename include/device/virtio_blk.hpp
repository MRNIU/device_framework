/**
 * @file virtio_blk.hpp
 * @brief Virtio 块设备驱动
 * @copyright Copyright The virtio_driver Contributors
 * @see virtio-v1.2#5.2 Block Device
 */

#ifndef VIRTIO_DRIVER_SRC_INCLUDE_VIRTIO_BLK_HPP_
#define VIRTIO_DRIVER_SRC_INCLUDE_VIRTIO_BLK_HPP_

#include "defs.h"
#include "device/device_initializer.hpp"
#include "expected.hpp"
#include "platform.h"
#include "transport/transport.hpp"
#include "virt_queue/split.hpp"

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
 * 主要特性：
 * - 读写操作（512 字节扇区）
 * - 缓存刷新 (FLUSH)
 * - Discard/Write Zeroes/Secure Erase（可选）
 * - 多队列（可选，需要 VIRTIO_BLK_F_MQ）
 * - 设备生命周期信息（可选，需要 VIRTIO_BLK_F_LIFETIME）
 *
 * 请求格式（Split Virtqueue）：
 * 1. 请求头（设备只读）: BlkReqHeader
 * 2. 数据缓冲区（读=设备只写，写=设备只读）
 * 3. 状态字节（设备只写）: BlkStatus
 *
 * @tparam LogFunc 日志函数类型（可选）
 * @see virtio-v1.2#5.2 Block Device
 */
template <class LogFunc = std::nullptr_t>
class VirtioBlk : public Logger<LogFunc> {
 public:
  /// Transport 类型别名（确保类型一致）
  using TransportType = Transport<LogFunc>;

  /**
   * @brief 创建并初始化块设备
   *
   * 使用 DeviceInitializer 执行标准的 virtio 设备初始化序列。
   *
   * @param transport 传输层引用（类型必须为 Transport<LogFunc>&）
   * @param vq SplitVirtqueue 引用（单队列）
   * @param platform 平台操作接口（提供物理地址转换和内存屏障）
   * @param driver_features 驱动希望启用的额外特性位（默认仅 VERSION_1）
   * @return 成功返回 VirtioBlk 实例，失败返回错误
   * @see virtio-v1.2#3.1 Device Initialization
   */
  [[nodiscard]] static auto Create(TransportType& transport, SplitVirtqueue& vq,
                                   const PlatformOps& platform,
                                   uint64_t driver_features = 0)
      -> Expected<VirtioBlk> {
    // 创建设备初始化器
    DeviceInitializer<LogFunc> initializer(transport);

    // 1. 执行设备初始化并协商特性（优先协商 VERSION_1，兼容 legacy 设备）
    uint64_t wanted_features =
        static_cast<uint64_t>(ReservedFeature::kVersion1) | driver_features;
    auto negotiated_result = initializer.Init(wanted_features);
    if (!negotiated_result) {
      return std::unexpected(negotiated_result.error());
    }
    uint64_t negotiated = *negotiated_result;

    // VERSION_1 为可选：modern 设备协商成功，legacy 设备跳过

    // 2. 配置 virtqueue 0（块设备仅使用一个队列）
    const uint32_t queue_idx = 0;
    auto setup_result = initializer.SetupQueue(
        queue_idx, vq.DescPhys(), vq.AvailPhys(), vq.UsedPhys(), vq.Size());
    if (!setup_result) {
      return std::unexpected(setup_result.error());
    }

    // 3. 激活设备
    auto activate_result = initializer.Activate();
    if (!activate_result) {
      return std::unexpected(activate_result.error());
    }

    return VirtioBlk(transport, vq, platform, negotiated);
  }

  /**
   * @brief 发起块设备读请求
   *
   * 提交一个读取请求到设备。请求是异步的，实际完成通过中断通知。
   * 驱动程序应在收到中断后调用 ProcessUsed() 处理完成的请求。
   *
   * @param sector 起始扇区号（以 512 字节为单位）
   * @param data 数据缓冲区（至少 kSectorSize 字节，必须位于 DMA 可访问内存）
   * @param status_out 状态字节输出指针（必须位于 DMA 可访问内存）
   * @param header 请求头缓冲区指针（必须位于 DMA 可访问内存）
   * @return 成功表示请求已提交，失败返回错误
   * @note 所有缓冲区在请求完成前不得释放或修改
   * @see virtio-v1.2#5.2.6 Device Operation
   */
  [[nodiscard]] auto Read(uint64_t sector, uint8_t* data, uint8_t* status_out,
                          BlkReqHeader* header) -> Expected<void> {
    return DoRequest(ReqType::kIn, sector, data, status_out, header);
  }

  /**
   * @brief 发起块设备写请求
   *
   * 提交一个写入请求到设备。请求是异步的，实际完成通过中断通知。
   *
   * @param sector 起始扇区号（以 512 字节为单位）
   * @param data 数据缓冲区（至少 kSectorSize 字节，必须位于 DMA 可访问内存）
   * @param status_out 状态字节输出指针（必须位于 DMA 可访问内存）
   * @param header 请求头缓冲区指针（必须位于 DMA 可访问内存）
   * @return 成功表示请求已提交，失败返回错误
   * @note 如果设备协商了 VIRTIO_BLK_F_RO，所有写请求将失败（状态 IOERR）
   * @note 所有缓冲区在请求完成前不得释放或修改
   * @see virtio-v1.2#5.2.6 Device Operation
   */
  [[nodiscard]] auto Write(uint64_t sector, const uint8_t* data,
                           uint8_t* status_out, BlkReqHeader* header)
      -> Expected<void> {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return DoRequest(ReqType::kOut, sector, const_cast<uint8_t*>(data),
                     status_out, header);
  }

  /**
   * @brief 处理已完成的请求
   *
   * 遍历 Used Ring，释放已完成请求占用的描述符。
   * 通常在接收到设备中断后调用此函数。
   *
   * @return 本次处理的已完成请求数量
   * @note 此函数会自动释放描述符链（header + data + status 三个描述符）
   * @note 调用者需要在调用前确保已执行适当的内存屏障
   * @see virtio-v1.2#2.7.8 The Virtqueue Used Ring
   */
  auto ProcessUsed() -> uint32_t {
    uint32_t count = 0;

    while (vq_.HasUsed()) {
      auto result = vq_.PopUsed();
      if (!result.has_value()) {
        break;
      }

      auto elem = *result;
      uint16_t head = static_cast<uint16_t>(elem.id);

      // 释放描述符链：遍历 kDescFNext 标志释放链上所有描述符
      uint16_t idx = head;
      while (true) {
        uint16_t next = vq_.GetDesc(idx).next;
        bool has_next =
            (vq_.GetDesc(idx).flags & SplitVirtqueue::kDescFNext) != 0;
        vq_.FreeDesc(idx);
        if (!has_next) {
          break;
        }
        idx = next;
      }

      ++count;
    }

    return count;
  }

  /**
   * @brief 确认设备中断
   *
   * 读取中断状态寄存器并写入中断确认寄存器，清除待处理的中断。
   * 通常在中断处理程序开始时调用。
   *
   * @see virtio-v1.2#2.3 Notifications
   */
  auto AckInterrupt() -> void {
    uint32_t status = transport_.GetInterruptStatus();
    transport_.AckInterrupt(status);
  }

  /**
   * @brief 读取块设备配置空间
   *
   * 读取设备的配置信息，包括容量、几何信息、拓扑信息等。
   * 配置空间的可用字段取决于协商的特性位。
   *
   * @return 块设备配置结构
   * @note 多字节字段使用 generation counter 机制确保配置一致性
   * @see virtio-v1.2#5.2.4 Device configuration layout
   */
  [[nodiscard]] auto ReadConfig() const -> BlkConfig {
    BlkConfig config{};

    // 读取基本配置（总是可用）
    config.capacity = transport_.ReadConfigU64(
        static_cast<uint32_t>(BlkConfigOffset::kCapacity));
    config.size_max = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kSizeMax));
    config.seg_max = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kSegMax));

    // 几何信息（如果 VIRTIO_BLK_F_GEOMETRY 被协商）
    config.geometry.cylinders = transport_.ReadConfigU16(
        static_cast<uint32_t>(BlkConfigOffset::kGeometryCylinders));
    config.geometry.heads = transport_.ReadConfigU8(
        static_cast<uint32_t>(BlkConfigOffset::kGeometryHeads));
    config.geometry.sectors = transport_.ReadConfigU8(
        static_cast<uint32_t>(BlkConfigOffset::kGeometrySectors));

    config.blk_size = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kBlkSize));

    // 拓扑信息（如果 VIRTIO_BLK_F_TOPOLOGY 被协商）
    config.topology.physical_block_exp = transport_.ReadConfigU8(
        static_cast<uint32_t>(BlkConfigOffset::kTopologyPhysBlockExp));
    config.topology.alignment_offset = transport_.ReadConfigU8(
        static_cast<uint32_t>(BlkConfigOffset::kTopologyAlignOffset));
    config.topology.min_io_size = transport_.ReadConfigU16(
        static_cast<uint32_t>(BlkConfigOffset::kTopologyMinIoSize));
    config.topology.opt_io_size = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kTopologyOptIoSize));

    // 缓存模式（如果 VIRTIO_BLK_F_CONFIG_WCE 被协商）
    config.writeback = transport_.ReadConfigU8(
        static_cast<uint32_t>(BlkConfigOffset::kWriteback));

    // Discard 支持（如果 VIRTIO_BLK_F_DISCARD 被协商）
    config.max_discard_sectors = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kMaxDiscardSectors));
    config.max_discard_seg = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kMaxDiscardSeg));
    config.discard_sector_alignment = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kDiscardSectorAlignment));

    // Write Zeroes 支持（如果 VIRTIO_BLK_F_WRITE_ZEROES 被协商）
    config.max_write_zeroes_sectors = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kMaxWriteZeroesSectors));
    config.max_write_zeroes_seg = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kMaxWriteZeroesSeg));
    config.write_zeroes_may_unmap = transport_.ReadConfigU8(
        static_cast<uint32_t>(BlkConfigOffset::kWriteZeroesMayUnmap));

    // Secure Erase 支持（如果 VIRTIO_BLK_F_SECURE_ERASE 被协商）
    config.max_secure_erase_sectors = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kMaxSecureEraseSectors));
    config.max_secure_erase_seg = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kMaxSecureEraseSeg));
    config.secure_erase_sector_alignment = transport_.ReadConfigU32(
        static_cast<uint32_t>(BlkConfigOffset::kSecureEraseSectorAlignment));

    // 多队列支持（如果 VIRTIO_BLK_F_MQ 被协商）
    config.num_queues = transport_.ReadConfigU16(
        static_cast<uint32_t>(BlkConfigOffset::kNumQueues));

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

  /// 获取传输层引用
  [[nodiscard]] auto GetTransport() -> TransportType& { return transport_; }

  /// 获取 virtqueue 引用
  [[nodiscard]] auto GetVirtqueue() -> SplitVirtqueue& { return vq_; }

 private:
  /**
   * @brief 私有构造函数
   *
   * 只能通过 Create() 静态工厂方法创建实例。
   *
   * @param transport 传输层引用
   * @param vq virtqueue 引用
   * @param platform 平台操作接口引用
   * @param features 协商后的特性位
   */
  VirtioBlk(TransportType& transport, SplitVirtqueue& vq,
            const PlatformOps& platform, uint64_t features)
      : transport_(transport),
        vq_(vq),
        platform_(platform),
        negotiated_features_(features) {}

  /**
   * @brief 内部请求提交函数
   *
   * 分配 3 个描述符并组成描述符链：
   * - 描述符 0: 请求头（设备只读）
   * - 描述符 1: 数据缓冲区（读=设备只写，写=设备只读）
   * - 描述符 2: 状态字节（设备只写）
   *
   * @param type 请求类型（kIn/kOut/kFlush 等）
   * @param sector 起始扇区号
   * @param data 数据缓冲区指针
   * @param status_out 状态字节输出指针
   * @param header 请求头缓冲区指针
   * @return 成功返回 void，失败返回错误
   * @see virtio-v1.2#5.2.6 Device Operation
   */
  [[nodiscard]] auto DoRequest(ReqType type, uint64_t sector, uint8_t* data,
                               uint8_t* status_out, BlkReqHeader* header)
      -> Expected<void> {
    // 分配 3 个描述符：header -> data -> status
    auto desc0_result = vq_.AllocDesc();
    if (!desc0_result.has_value()) {
      return std::unexpected(desc0_result.error());
    }
    uint16_t desc0 = *desc0_result;

    auto desc1_result = vq_.AllocDesc();
    if (!desc1_result.has_value()) {
      vq_.FreeDesc(desc0);
      return std::unexpected(desc1_result.error());
    }
    uint16_t desc1 = *desc1_result;

    auto desc2_result = vq_.AllocDesc();
    if (!desc2_result.has_value()) {
      vq_.FreeDesc(desc0);
      vq_.FreeDesc(desc1);
      return std::unexpected(desc2_result.error());
    }
    uint16_t desc2 = *desc2_result;

    // 填充请求头
    header->type = static_cast<uint32_t>(type);
    header->reserved = 0;
    header->sector = sector;

    // 设置描述符 0：请求头（设备只读）
    vq_.GetDesc(desc0).addr = platform_.virt_to_phys(header);
    vq_.GetDesc(desc0).len = sizeof(BlkReqHeader);
    vq_.GetDesc(desc0).flags = SplitVirtqueue::kDescFNext;
    vq_.GetDesc(desc0).next = desc1;

    // 设置描述符 1：数据缓冲区
    vq_.GetDesc(desc1).addr = platform_.virt_to_phys(data);
    vq_.GetDesc(desc1).len = kSectorSize;
    if (type == ReqType::kIn) {
      // 读取：设备写入数据
      vq_.GetDesc(desc1).flags =
          SplitVirtqueue::kDescFNext | SplitVirtqueue::kDescFWrite;
    } else {
      // 写入：设备读取数据
      vq_.GetDesc(desc1).flags = SplitVirtqueue::kDescFNext;
    }
    vq_.GetDesc(desc1).next = desc2;

    // 设置描述符 2：状态字节（设备只写）
    vq_.GetDesc(desc2).addr = platform_.virt_to_phys(status_out);
    vq_.GetDesc(desc2).len = sizeof(uint8_t);
    vq_.GetDesc(desc2).flags = SplitVirtqueue::kDescFWrite;
    vq_.GetDesc(desc2).next = 0;

    // 内存屏障：确保描述符已写入内存
    if (platform_.wmb) {
      platform_.wmb();
    }

    // 提交到 Available Ring
    vq_.Submit(desc0);

    // 内存屏障：确保 Available Ring 更新对设备可见
    if (platform_.mb) {
      platform_.mb();
    }

    // 通知设备有新的可用缓冲区
    transport_.NotifyQueue(0);

    return {};
  }

  /// 传输层引用
  TransportType& transport_;
  /// Virtqueue 引用
  SplitVirtqueue& vq_;
  /// 平台操作接口引用
  const PlatformOps& platform_;
  /// 协商后的特性位掩码
  uint64_t negotiated_features_;
};

}  // namespace virtio_driver::blk

#endif /* VIRTIO_DRIVER_SRC_INCLUDE_VIRTIO_BLK_HPP_ */
