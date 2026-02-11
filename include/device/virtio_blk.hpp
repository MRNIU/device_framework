/**
 * @file virtio_blk.hpp
 * @brief Virtio 块设备驱动接口
 * @copyright Copyright The virtio_driver Contributors
 * @see virtio-v1.2#5.2 Block Device
 *
 * 该文件定义了 virtio 块设备的驱动程序接口，包括：
 * - 块设备特性位（blk_feature namespace）
 * - 设备配置空间结构（BlkConfig）
 * - 请求/响应数据结构（BlkReqHeader、BlkDiscardWriteZeroes 等）
 * - 块设备驱动类（VirtioBlk）
 *
 * 支持的功能：
 * - 基本读写操作（512 字节扇区）
 * - 缓存刷新 (FLUSH)
 * - Discard/Write Zeroes/Secure Erase（可选）
 * - 设备生命周期信息查询（可选）
 * - 多队列支持（可选，当前未实现）
 */

#ifndef VIRTIO_DRIVER_SRC_INCLUDE_VIRTIO_BLK_HPP_
#define VIRTIO_DRIVER_SRC_INCLUDE_VIRTIO_BLK_HPP_

#include "defs.h"
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
  } geometry;

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
  } topology;

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
 * @see virtio-v1.2#5.2 Block Device
 *
 * virtio 块设备是一个简单的虚拟块设备（即磁盘）。
 * 读写请求（以及其他特殊请求）被放置在请求队列中，由设备服务（可能乱序）。
 *
 * ### 设备特性
 * - 支持读写操作（512 字节扇区）
 * - 支持缓存刷新 (FLUSH)
 * - 支持 discard/write zeroes/secure erase（需要相应特性）
 * - 支持多队列（需要 VIRTIO_BLK_F_MQ）
 * - 支持设备生命周期信息（需要 VIRTIO_BLK_F_LIFETIME）
 *
 * ### 请求格式
 * 使用 Split Virtqueue，每个请求由 3 个描述符链组成：
 * 1. 请求头（设备只读）: BlkReqHeader
 * 2. 数据缓冲区（读=设备只写，写=设备只读）
 * 3. 状态字节（设备只写）: BlkStatus
 *
 * ### 使用示例
 * ```cpp
 * // 1. 创建传输层
 * auto transport = MmioTransport::create(base_addr).value();
 *
 * // 2. 分配 virtqueue DMA 内存
 * auto queue_max = transport.get_queue_num_max(0);
 * auto mem_size = SplitVirtqueue::calc_size(queue_max);
 * auto* mem = platform.alloc_pages(pages_needed);
 * auto vq = SplitVirtqueue::create(mem, mem_size, queue_max, phys).value();
 *
 * // 3. 创建并初始化块设备
 * auto blk = VirtioBlk::create(transport, vq, platform).value();
 *
 * // 4. 读取配置并发起请求
 * auto config = blk.read_config();
 * BlkReqHeader header{};
 * uint8_t data[512];
 * uint8_t status_byte;
 * blk.read(0, data, &status_byte, &header);
 *
 * // 5. 中断处理
 * blk.process_used();
 * ```
 *
 * @note 当前实现仅支持单队列模式
 * @note 所有 DMA 缓冲区必须位于物理连续内存
 */
// 前向声明
class BlkRequest;

/**
 * @brief Virtio 块设备驱动
 * @see virtio-v1.2#5.2 Block Device
 *
 * virtio 块设备是一个简单的虚拟块设备（即磁盘）。
 * 读写请求（以及其他特殊请求）被放置在请求队列中，由设备服务（可能乱序）。
 *
 * ### 设备特性
 * - 支持读写操作（512 字节扇区）
 * - 支持缓存刷新 (FLUSH)
 * - 支持 discard/write zeroes/secure erase（需要相应特性）
 * - 支持多队列（需要 VIRTIO_BLK_F_MQ）
 * - 支持设备生命周期信息（需要 VIRTIO_BLK_F_LIFETIME）
 *
 * ### 请求格式
 * 使用 Split Virtqueue，每个请求由 3 个描述符链组成：
 * 1. 请求头（设备只读）: BlkReqHeader
 * 2. 数据缓冲区（读=设备只写，写=设备只读）
 * 3. 状态字节（设备只写）: BlkStatus
 *
 * ### 使用示例（传统方式）
 * ```cpp
 * // 1. 创建传输层
 * auto transport = MmioTransport::create(base_addr).value();
 *
 * // 2. 分配 virtqueue DMA 内存
 * auto queue_max = transport.get_queue_num_max(0);
 * auto mem_size = SplitVirtqueue::calc_size(queue_max);
 * auto* mem = platform.alloc_pages(pages_needed);
 * auto vq = SplitVirtqueue::create(mem, mem_size, queue_max, phys).value();
 *
 * // 3. 创建并初始化块设备
 * auto blk = VirtioBlk::create(transport, vq, platform).value();
 *
 * // 4. 读取配置并发起请求
 * auto config = blk.read_config();
 * BlkReqHeader header{};
 * uint8_t data[512];
 * uint8_t status_byte;
 * blk.read(0, data, &status_byte, &header);
 *
 * // 5. 中断处理
 * blk.process_used();
 * ```
 *
 * ### 使用示例（RAII 封装）
 * ```cpp
 * // 使用 BlkRequest 自动管理生命周期
 * #include "blk_request.hpp"
 *
 * auto blk = VirtioBlk::create(...).value();
 * uint8_t data[512];
 *
 * // 同步读取
 * auto req = BlkRequest::read(blk, 0, data).value();
 * auto status = req.wait();
 *
 * // 异步读取
 * auto req = BlkRequest::read(blk, 0, data).value();
 * // ... 执行其他操作 ...
 * if (req.is_complete()) {
 *     auto status = req.status().value();
 * }
 * ```
 *
 * @note 当前实现仅支持单队列模式
 * @note 所有 DMA 缓冲区必须位于物理连续内存
 */
/**
 * @brief VirtioBlk 设备驱动类
 *
 * @tparam LogFunc 日志函数对象类型（必须与 Transport 的 LogFunc 一致）
 *
 * @note 继承自 Logger<LogFunc> 以支持设备层日志
 */
template <class LogFunc = std::nullptr_t>
class VirtioBlk : public Logger<LogFunc> {
  // BlkRequest 需要访问 do_request()
  friend class BlkRequest;

 public:
  /// Transport 类型别名（确保类型一致）
  using TransportType = Transport<LogFunc>;

  /**
   * @brief 创建并初始化块设备
   * @see virtio-v1.2#3.1 Device Initialization
   *
   * 执行完整的 virtio 设备初始化序列：
   * 1. 重置设备
   * 2. 设置 ACKNOWLEDGE 状态位
   * 3. 设置 DRIVER 状态位
   * 4. 读取设备特性位
   * 5. 协商特性（VERSION_1 必需，其他可选）
   * 6. 设置 FEATURES_OK 状态位
   * 7. 验证 FEATURES_OK（特性协商成功）
   * 8. 执行设备特定设置（配置 virtqueue）
   * 9. 设置 DRIVER_OK 状态位
   *
   * @param transport 传输层实例（类型必须为 Transport<LogFunc>&）
   * @param vq 预创建的 SplitVirtqueue（单队列）
   * @param platform 平台操作接口（提供物理地址转换和内存屏障）
   * @param driver_features 驱动希望启用的额外特性位（默认仅 VERSION_1）
   * @return 成功返回 VirtioBlk 实例，失败返回错误
   * @retval Error::kFeatureNegotiationFailed 特性协商失败（设备不支持
   * VERSION_1）
   */
  [[nodiscard]] static auto create(TransportType& transport, SplitVirtqueue& vq,
                                   const PlatformOps& platform,
                                   uint64_t driver_features = 0)
      -> Expected<VirtioBlk> {
    // 1. 重置设备
    transport.Reset();

    // 2. 设置 ACKNOWLEDGE 状态
    transport.SetStatus(TransportType::kAcknowledge);

    // 3. 设置 DRIVER 状态
    transport.SetStatus(TransportType::kAcknowledge | TransportType::kDriver);

    // 4. 特性协商
    uint64_t device_features = transport.GetDeviceFeatures();
    uint64_t wanted_features =
        static_cast<uint64_t>(ReservedFeature::kVersion1) | driver_features;
    uint64_t negotiated = device_features & wanted_features;

    // 必须支持 VERSION_1
    if ((negotiated & static_cast<uint64_t>(ReservedFeature::kVersion1)) == 0) {
      transport.SetStatus(Transport::kFailed);
      return ErrorCode::kFeatureNegotiationFailed;
    }

    transport.SetDriverFeatures(negotiated);

    // 5. 设置 FEATURES_OK
    transport.SetStatus(TransportType::kAcknowledge | TransportType::kDriver |
                        TransportType::kFeaturesOk);

    // 6. 验证 FEATURES_OK
    if ((transport.GetStatus() & TransportType::kFeaturesOk) == 0) {
      transport.SetStatus(TransportType::kFailed);
      return std::unexpected(Error{ErrorCode::kFeatureNegotiationFailed});
    }

    // 7. 配置 virtqueue 0（块设备仅使用一个队列）
    const uint32_t queue_idx = 0;
    transport.SetQueueNum(queue_idx, vq.size());
    transport.SetQueueDesc(queue_idx, vq.desc_phys());
    transport.SetQueueAvail(queue_idx, vq.avail_phys());
    transport.SetQueueUsed(queue_idx, vq.used_phys());
    transport.SetQueueReady(queue_idx, true);

    // 8. 设置 DRIVER_OK
    transport.SetStatus(TransportType::kAcknowledge | TransportType::kDriver |
                        TransportType::kFeaturesOk | TransportType::kDriverOk);

    return VirtioBlk(transport, vq, platform, negotiated);
  }

  /**
   * @brief 发起块设备读请求
   * @see virtio-v1.2#5.2.6 Device Operation
   *
   * 提交一个读取请求到设备。请求是异步的，实际完成通过中断通知。
   * 驱动程序应在收到中断后调用 process_used() 处理完成的请求。
   *
   * @param sector 起始扇区号（以 512 字节为单位）
   * @param data 数据缓冲区（至少 SECTOR_SIZE 字节，必须位于 DMA 可访问内存）
   * @param status_out 状态字节输出指针（必须位于 DMA 可访问内存）
   * @param header 请求头缓冲区指针（必须位于 DMA 可访问内存）
   * @return 成功表示请求已提交，失败返回错误
   * @retval Error::kNoDescriptors virtqueue 描述符不足
   *
   * @note 请求完成后 status_out 将包含 BlkStatus 值
   * @note 所有缓冲区在请求完成前不得释放或修改
   */
  [[nodiscard]] auto read(uint64_t sector, uint8_t* data, uint8_t* status_out,
                          BlkReqHeader* header) -> Expected<void> {
    return do_request(ReqType::kIn, sector, data, status_out, header);
  }

  /**
   * @brief 发起块设备写请求
   * @see virtio-v1.2#5.2.6 Device Operation
   *
   * 提交一个写入请求到设备。请求是异步的，实际完成通过中断通知。
   *
   * @param sector 起始扇区号（以 512 字节为单位）
   * @param data 数据缓冲区（至少 SECTOR_SIZE 字节，必须位于 DMA 可访问内存）
   * @param status_out 状态字节输出指针（必须位于 DMA 可访问内存）
   * @param header 请求头缓冲区指针（必须位于 DMA 可访问内存）
   * @return 成功表示请求已提交，失败返回错误
   * @retval Error::kNoDescriptors virtqueue 描述符不足
   *
   * @note 如果设备协商了 VIRTIO_BLK_F_RO，所有写请求将失败（状态
   * IOERR）
   * @note 所有缓冲区在请求完成前不得释放或修改
   */
  [[nodiscard]] auto write(uint64_t sector, const uint8_t* data,
                           uint8_t* status_out, BlkReqHeader* header)
      -> Expected<void> {
    return do_request(ReqType::kOut, sector, const_cast<uint8_t*>(data),
                      status_out, header);
  }

  /**
   * @brief 处理已完成的请求
   * @see virtio-v1.2#2.7.8 The Virtqueue Used Ring
   *
   * 遍历 Used Ring，释放已完成请求占用的描述符。
   * 通常在接收到设备中断后调用此函数。
   *
   * @return 本次处理的已完成请求数量
   *
   * @note 此函数会自动释放描述符链（header + data + status 三个描述符）
   * @note 调用者需要在调用前确保已执行适当的内存屏障
   */
  auto process_used() -> uint32_t {
    uint32_t count = 0;

    while (vq_.has_used()) {
      auto result = vq_.pop_used();
      if (!result.has_value()) {
        break;
      }

      auto elem = result.value();
      uint16_t head = static_cast<uint16_t>(elem.id);

      // 释放描述符链：header -> data -> status
      uint16_t idx = head;
      for (int i = 0; i < 3; ++i) {
        uint16_t next = vq_.desc(idx).next;
        vq_.free_desc(idx);
        idx = next;
      }

      ++count;
    }

    return count;
  }

  /**
   * @brief 确认设备中断
   * @see virtio-v1.2#2.3 Notifications
   *
   * 读取中断状态寄存器并写入中断确认寄存器，清除待处理的中断。
   * 通常在中断处理程序开始时调用。
   */
  auto ack_interrupt() -> void {
    uint32_t status = transport_.GetInterruptStatus();
    transport_.AckInterrupt(status);
  }

  /**
   * @brief 读取块设备配置空间
   * @see virtio-v1.2#5.2.4 Device configuration layout
   *
   * 读取设备的配置信息，包括容量、几何信息、拓扑信息等。
   * 配置空间的可用字段取决于协商的特性位。
   *
   * @return 块设备配置结构
   *
   * @note 实际生产环境中应使用 generation counter 机制确保配置一致性
   * @note 多字节字段可能需要多次读取，参考 virtio-v1.2#2.5.1
   */
  [[nodiscard]] auto read_config() const -> BlkConfig {
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
   * @see virtio-v1.2#5.2.4
   *
   * @return 设备容量（以 512 字节扇区为单位）
   */
  [[nodiscard]] auto capacity() const -> uint64_t {
    return transport_.ReadConfigU64(
        static_cast<uint32_t>(BlkConfigOffset::kCapacity));
  }

  /**
   * @brief 获取协商后的特性位
   *
   * @return 设备和驱动程序都支持的特性位掩码
   */
  [[nodiscard]] auto negotiated_features() const -> uint64_t {
    return negotiated_features_;
  }

  /**
   * @brief 获取传输层引用
   *
   * @return 传输层对象引用
   */
  [[nodiscard]] auto transport() -> TransportType& { return transport_; }

  /**
   * @brief 获取 virtqueue 引用
   *
   * @return SplitVirtqueue 对象引用
   */
  [[nodiscard]] auto virtqueue() -> SplitVirtqueue& { return vq_; }

 private:
  /**
   * @brief 私有构造函数
   *
   * 只能通过 create() 静态工厂方法创建实例。
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
   * @brief 使用 Logger 基类的 Log 方法
   *
   * 提供设备层日志功能，格式示例：
   * this->Log("[VirtioBlk] Read sector %llu", sector);
   */
  using Logger<LogFunc>::Log;

  /**
   * @brief 内部请求提交函数
   * @see virtio-v1.2#5.2.6 Device Operation
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
   */
  [[nodiscard]] auto do_request(ReqType type, uint64_t sector, uint8_t* data,
                                uint8_t* status_out, BlkReqHeader* header)
      -> Expected<void> {
    // 分配 3 个描述符：header -> data -> status
    auto desc0_result = vq_.alloc_desc();
    if (!desc0_result.has_value()) {
      return std::unexpected(desc0_result.error());
    }
    uint16_t desc0 = desc0_result.value();

    auto desc1_result = vq_.alloc_desc();
    if (!desc1_result.has_value()) {
      vq_.free_desc(desc0);
      return std::unexpected(desc1_result.error());
    }
    uint16_t desc1 = desc1_result.value();

    auto desc2_result = vq_.alloc_desc();
    if (!desc2_result.has_value()) {
      vq_.free_desc(desc0);
      vq_.free_desc(desc1);
      return std::unexpected(desc2_result.error());
    }
    uint16_t desc2 = desc2_result.value();

    // 填充请求头
    header->type = static_cast<uint32_t>(type);
    header->reserved = 0;
    header->sector = sector;

    // 设置描述符 0：请求头（设备只读）
    vq_.desc(desc0).addr = platform_.virt_to_phys(header);
    vq_.desc(desc0).len = sizeof(BlkReqHeader);
    vq_.desc(desc0).flags = SplitVirtqueue::kDescFNext;
    vq_.desc(desc0).next = desc1;

    // 设置描述符 1：数据缓冲区
    vq_.desc(desc1).addr = platform_.virt_to_phys(data);
    vq_.desc(desc1).len = kSectorSize;
    if (type == ReqType::kIn) {
      // 读取：设备写入数据
      vq_.desc(desc1).flags =
          SplitVirtqueue::kDescFNext | SplitVirtqueue::kDescFWrite;
    } else {
      // 写入：设备读取数据
      vq_.desc(desc1).flags = SplitVirtqueue::kDescFNext;
    }
    vq_.desc(desc1).next = desc2;

    // 设置描述符 2：状态字节（设备只写）
    vq_.desc(desc2).addr = platform_.virt_to_phys(status_out);
    vq_.desc(desc2).len = sizeof(uint8_t);
    vq_.desc(desc2).flags = SplitVirtqueue::kDescFWrite;
    vq_.desc(desc2).next = 0;

    // 内存屏障：确保描述符已写入内存
    if (platform_.wmb) {
      platform_.wmb();
    }

    // 提交到 Available Ring
    vq_.submit(desc0);

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
