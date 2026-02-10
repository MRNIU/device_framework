/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DEVICES_VIRTIO_BLK_H
#define VIRTIO_DEVICES_VIRTIO_BLK_H

#include "expected.hpp"
#include "platform.h"
#include "transport/transport.hpp"
#include "virt_queue/split.hpp"

namespace virtio::blk {

// ============================================================================
// Block Device Feature Bits
// @see virtio-v1.1#5.2.3
// ============================================================================

namespace blk_feature {

/// Maximum size of any single segment is in size_max
static constexpr const uint64_t kSizeMax = 1ULL << 1;
/// Maximum number of segments in a request is in seg_max
static constexpr const uint64_t kSegMax = 1ULL << 2;
/// Disk-style geometry specified in geometry
static constexpr const uint64_t kGeometry = 1ULL << 4;
/// Device is read-only
static constexpr const uint64_t kRo = 1ULL << 5;
/// Block size of disk is in blk_size
static constexpr const uint64_t kBlkSize = 1ULL << 6;
/// Cache flush command support
static constexpr const uint64_t kFlush = 1ULL << 9;
/// Device exports information on optimal I/O alignment
static constexpr const uint64_t kTopology = 1ULL << 10;
/// Device can toggle its cache between writeback and writethrough modes
static constexpr const uint64_t kConfigWce = 1ULL << 11;
/// Device can support discard command
static constexpr const uint64_t kDiscard = 1ULL << 13;
/// Device can support write zeroes command
static constexpr const uint64_t kWriteZeroes = 1ULL << 14;

} /* namespace blk_feature */

// ============================================================================
// Block Device Configuration
// @see virtio-v1.1#5.2.4
// ============================================================================

/**
 * @brief 块设备配置空间布局
 * @see virtio-v1.1#5.2.4
 */
struct BlkConfig {
  uint64_t capacity;
  uint32_t size_max;
  uint32_t seg_max;

  struct {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
  } geometry;

  uint32_t blk_size;

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

  uint8_t writeback;
  uint8_t unused0[3];
  uint32_t max_discard_sectors;
  uint32_t max_discard_seg;
  uint32_t discard_sector_alignment;
  uint32_t max_write_zeroes_sectors;
  uint32_t max_write_zeroes_seg;
  uint8_t write_zeroes_may_unmap;
  uint8_t unused1[3];
} __attribute__((packed));

/// 配置空间字段偏移量
namespace blk_config_offset {

static constexpr uint32_t kCapacity = 0;
static constexpr uint32_t kSizeMax = 8;
static constexpr uint32_t kSegMax = 12;
static constexpr uint32_t kGeometryCylinders = 16;
static constexpr uint32_t kGeometryHeads = 18;
static constexpr uint32_t kGeometrySectors = 19;
static constexpr uint32_t kBlkSize = 20;
static constexpr uint32_t kTopologyPhysBlockExp = 24;
static constexpr uint32_t kTopologyAlignOffset = 25;
static constexpr uint32_t kTopologyMinIoSize = 26;
static constexpr uint32_t kTopologyOptIoSize = 28;
static constexpr uint32_t kWriteback = 32;

} /* namespace blk_config_offset */

// ============================================================================
// Block Device Request
// @see virtio-v1.1#5.2.6
// ============================================================================

/// 块设备请求类型
enum class ReqType : uint32_t {
  /// 读取
  kIn = 0,
  /// 写入
  kOut = 1,
  /// 刷新
  kFlush = 4,
  /// 丢弃
  kDiscard = 11,
  /// 写零
  kWriteZeroes = 13,
};

/// 块设备返回状态
enum class BlkStatus : uint8_t {
  /// 操作成功
  kOk = 0,
  /// IO 错误
  kIoErr = 1,
  /// 不支持的操作
  kUnsupp = 2,
};

/**
 * @brief 块设备请求头（描述符 0 的内容）
 * @see virtio-v1.1#5.2.6
 */
struct BlkReqHeader {
  /// 请求类型 (ReqType)
  uint32_t type;
  /// 保留字段，必须为 0
  uint32_t reserved;
  /// 起始扇区号
  uint64_t sector;
} __attribute__((packed));

/// 扇区大小（字节）
static constexpr const size_t kSectorSize = 512;

// ============================================================================
// VirtioBlk 驱动类
// ============================================================================

/**
 * @brief Virtio 块设备驱动
 *
 * 使用 Split Virtqueue，通过 3 个描述符链（header + data + status）
 * 发起块设备读写请求。
 *
 * 典型使用流程：
 * ```
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
 * // 4. 发起读写请求
 * BlkReqHeader header{};
 * uint8_t data[512];
 * uint8_t status_byte;
 * blk.read(0, data, &status_byte, &header);
 * ```
 *
 * @see virtio-v1.1#5.2
 */
class VirtioBlk {
 public:
  /**
   * @brief 创建并初始化块设备
   *
   * 执行完整的 virtio 初始化序列：
   * 1. 重置、ACKNOWLEDGE、DRIVER
   * 2. 特性协商（VERSION_1 + 指定的块设备特性）
   * 3. FEATURES_OK
   * 4. 设置 virtqueue
   * 5. DRIVER_OK
   *
   * @param transport 传输层实例（需已通过 create 验证）
   * @param vq 预创建的 SplitVirtqueue
   * @param platform 平台操作接口
   * @param driver_features 驱动希望启用的额外特性位（默认仅 VERSION_1）
   * @return 成功返回 VirtioBlk 实例，失败返回错误
   */
  [[nodiscard]] static auto create(Transport& transport, SplitVirtqueue& vq,
                                   const PlatformOps& platform,
                                   uint64_t driver_features = 0)
      -> Result<VirtioBlk>;

  /**
   * @brief 发起块设备读请求
   * @param sector 起始扇区号
   * @param data 数据缓冲区，至少 SECTOR_SIZE 字节，必须位于 DMA 可访问内存
   * @param status_out 状态字节输出，必须位于 DMA 可访问内存
   * @param header 请求头缓冲区，必须位于 DMA 可访问内存
   * @return 成功表示请求已提交（非阻塞），实际结果通过中断 + process_used 获取
   */
  [[nodiscard]] auto read(uint64_t sector, uint8_t* data, uint8_t* status_out,
                          BlkReqHeader* header) -> Result<void>;

  /**
   * @brief 发起块设备写请求
   * @param sector 起始扇区号
   * @param data 数据缓冲区，至少 SECTOR_SIZE 字节，必须位于 DMA 可访问内存
   * @param status_out 状态字节输出，必须位于 DMA 可访问内存
   * @param header 请求头缓冲区，必须位于 DMA 可访问内存
   * @return 成功表示请求已提交
   */
  [[nodiscard]] auto write(uint64_t sector, const uint8_t* data,
                           uint8_t* status_out, BlkReqHeader* header)
      -> Result<void>;

  /**
   * @brief 处理已完成的请求
   *
   * 遍历 Used Ring，释放已完成请求占用的描述符。
   * 通常在中断处理中调用。
   * @return 本次处理的已完成请求数量
   */
  auto process_used() -> uint32_t;

  /// 确认中断，读取 InterruptStatus 并写入 InterruptACK
  auto ack_interrupt() -> void;

  /**
   * @brief 读取块设备配置
   *
   * 带 generation 检查确保配置一致性
   * @see virtio-v1.1#4.2.2.2
   */
  [[nodiscard]] auto read_config() const -> BlkConfig;

  /// 获取设备容量（扇区数）
  [[nodiscard]] auto capacity() const -> uint64_t;

  /// 获取协商后的特性位
  [[nodiscard]] auto negotiated_features() const -> uint64_t {
    return negotiated_features_;
  }

  /// 获取传输层引用
  [[nodiscard]] auto transport() -> Transport& { return transport_; }

  /// 获取 virtqueue 引用
  [[nodiscard]] auto virtqueue() -> SplitVirtqueue& { return vq_; }

 private:
  VirtioBlk(Transport& transport, SplitVirtqueue& vq,
            const PlatformOps& platform, uint64_t features);

  /// 内部请求提交
  [[nodiscard]] auto do_request(ReqType type, uint64_t sector, uint8_t* data,
                                uint8_t* status_out, BlkReqHeader* header)
      -> Result<void>;

  Transport& transport_;
  SplitVirtqueue& vq_;
  const PlatformOps& platform_;
  uint64_t negotiated_features_;
};

} /* namespace virtio::blk */

#endif /* VIRTIO_DEVICES_VIRTIO_BLK_H */
