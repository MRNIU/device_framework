/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_SRC_INCLUDE_VIRT_QUEUE_SPLIT_HPP_
#define VIRTIO_DRIVER_SRC_INCLUDE_VIRT_QUEUE_SPLIT_HPP_

#include "expected.hpp"
#include "misc.hpp"

namespace virtio_driver {

/**
 * @brief Split Virtqueue 管理类
 *
 * 管理 split virtqueue 的描述符分配/释放、缓冲区提交、已用缓冲区回收。
 * 使用预分配的 DMA 内存，自身不进行任何堆内存分配。
 *
 * 内存布局（在 DMA 缓冲区中连续排列）：
 * ```
 * [Descriptor Table]  aligned to 16
 * [Available Ring]    aligned to 2
 * [Used Ring]         aligned to 4
 * ```
 *
 * @see virtio-v1.2#2.7
 */
class SplitVirtqueue {
 public:
  /**
   * @brief Descriptor Flags
   * @see virtio-v1.2#2.7.5 The Virtqueue Descriptor Table
   */
  enum DescFlags : uint16_t {
    /// 标记缓冲区通过 next 字段继续
    kDescFNext = 1,
    /// 标记缓冲区为设备只写(否则为设备只读)
    kDescFWrite = 2,
    /// 标记缓冲区包含描述符列表(间接描述符)
    kDescFIndirect = 4
  };

  /**
   * @brief Available Ring Flags
   * @see virtio-v1.2#2.7.6 The Virtqueue Available Ring
   */
  enum AvailFlags : uint16_t {
    /// 设备应该不发送中断
    kAvailFNoInterrupt = 1
  };

  /**
   * @brief Used Ring Flags
   * @see virtio-v1.2#2.7.8 The Virtqueue Used Ring
   */
  enum UsedFlags : uint16_t {
    /// 驱动不需要通知
    kUsedFNoNotify = 1
  };

  /**
   * @brief Virtqueue 描述符表条目
   *
   * 描述符表引用驱动程序使用的缓冲区。每个描述符描述一个缓冲区，
   * 该缓冲区对设备是只读的("设备可读")或只写的("设备可写")。
   *
   * @note 16 字节对齐
   * @see virtio-v1.2#2.7.5 The Virtqueue Descriptor Table
   */
  struct Desc {
    /// Descriptor Table 对齐要求(字节)
    static constexpr size_t kAlign = 16;
    /// 缓冲区的客户机物理地址 (little-endian)
    uint64_t addr;
    /// 缓冲区长度(字节) (little-endian)
    uint32_t len;
    /// 标志位: DescFlags (little-endian)
    uint16_t flags;
    /// 下一个描述符的索引(当 flags & kDescFNext 时有效) (little-endian)
    uint16_t next;
  } __attribute__((packed));

  /**
   * @brief Virtqueue Available Ring
   *
   * Available Ring 用于驱动程序向设备提供缓冲区。
   * 驱动程序将描述符链的头部放入环中。
   *
   * @note 实际 ring[] 大小由 queue_size 决定
   * @note 2 字节对齐
   * @see virtio-v1.2#2.7.6 The Virtqueue Available Ring
   */
  struct Avail {
    /// Available Ring 对齐要求(字节)
    static constexpr size_t kAlign = 2;
    /// 标志位: AvailFlags (little-endian)
    uint16_t flags;
    /// 驱动程序将下一个描述符条目放入环中的位置(模 queue_size) (little-endian)
    uint16_t idx;
    /// 可用描述符头索引数组 ring[queue_size] (little-endian)
    uint16_t ring[];

    /**
     * @brief 获取 used_event 字段的指针
     * @param queue_size 队列大小
     * @return used_event 字段指针
     * @note 仅当协商 VIRTIO_F_EVENT_IDX 特性时使用
     * @see virtio-v1.2#2.7.10 Available Buffer Notification Suppression
     */
    [[nodiscard]] volatile uint16_t* used_event(uint16_t queue_size) volatile {
      return &ring[queue_size];
    }

    [[nodiscard]] const volatile uint16_t* used_event(uint16_t queue_size) const
        volatile {
      return &ring[queue_size];
    }
  } __attribute__((packed));

  /**
   * @brief Virtqueue Used Ring 元素
   *
   * Used Ring 中的每个条目是一个 (id, len) 对。
   *
   * @see virtio-v1.2#2.7.8 The Virtqueue Used Ring
   */
  struct UsedElem {
    /// 描述符链头的索引 (little-endian)
    uint32_t id;
    /// 设备写入描述符链的总字节数 (little-endian)
    uint32_t len;
  } __attribute__((packed));

  /**
   * @brief Virtqueue Used Ring
   *
   * Used Ring 是设备完成缓冲区处理后返回它们的地方。
   * 它只由设备写入，由驱动程序读取。
   *
   * @note 实际 ring[] 大小由 queue_size 决定
   * @note 4 字节对齐
   * @see virtio-v1.2#2.7.8 The Virtqueue Used Ring
   */
  struct Used {
    /// Used Ring 对齐要求(字节)
    static constexpr size_t kAlign = 4;
    /// 标志位: UsedFlags (little-endian)
    uint16_t flags;
    /// 设备将下一个描述符条目放入环中的位置(模 queue_size) (little-endian)
    uint16_t idx;
    /// 已用描述符元素数组 ring[queue_size]
    UsedElem ring[];

    /**
     * @brief 获取 avail_event 字段的指针
     * @param queue_size 队列大小
     * @return avail_event 字段指针
     * @note 仅当协商 VIRTIO_F_EVENT_IDX 特性时使用
     * @see virtio-v1.2#2.7.10 Available Buffer Notification Suppression
     */
    [[nodiscard]] volatile uint16_t* avail_event(uint16_t queue_size) volatile {
      return reinterpret_cast<volatile uint16_t*>(&ring[queue_size]);
    }

    [[nodiscard]] const volatile uint16_t* avail_event(
        uint16_t queue_size) const volatile {
      return reinterpret_cast<const volatile uint16_t*>(&ring[queue_size]);
    }
  } __attribute__((packed));

  /**
   * @brief 计算给定队列大小所需的 DMA 内存字节数
   * @param queue_size 队列大小（必须为 2 的幂）
   * @param event_idx 是否包含 EVENT_IDX 相关字段
   * @return 所需字节数
   */
  [[nodiscard]] static constexpr auto calc_size(uint16_t queue_size,
                                                bool event_idx = true)
      -> size_t {
    // Descriptor Table: sizeof(Desc) * queue_size
    size_t desc_total = static_cast<size_t>(sizeof(Desc)) * queue_size;

    // Available Ring: flags(2) + idx(2) + ring[N](2*N) + used_event(2, 可选)
    size_t avail_total = sizeof(uint16_t) * (2 + queue_size);
    if (event_idx) {
      avail_total += sizeof(uint16_t);
    }

    // Used Ring: flags(2) + idx(2) + ring[N](sizeof(UsedElem)*N) +
    // avail_event(2, 可选)
    size_t used_total = sizeof(uint16_t) * 2 + sizeof(UsedElem) * queue_size;
    if (event_idx) {
      used_total += sizeof(uint16_t);
    }

    // 按对齐要求排列
    size_t avail_off = align_up(desc_total, Avail::kAlign);
    size_t used_off = align_up(avail_off + avail_total, Used::kAlign);

    return used_off + used_total;
  }

  /**
   * @brief 在预分配的 DMA 内存上创建 SplitVirtqueue
   * @param mem DMA 内存的虚拟地址（由平台层分配，必须已清零或由此函数清零）
   * @param mem_size 内存大小（字节），必须 >= calc_size(queue_size)
   * @param queue_size 队列大小（必须为 2 的幂，且 <= 设备报告的 queue_num_max）
   * @param phys_base DMA 内存的物理基地址
   * @return 成功返回实例，失败返回错误
   */
  [[nodiscard]] static auto create(void* mem, size_t mem_size,
                                   uint16_t queue_size, uint64_t phys_base)
      -> Result<SplitVirtqueue> {
    // 检查参数有效性
    if (mem == nullptr) {
      return Error::kInvalidArgument;
    }
    if (queue_size == 0 || (queue_size & (queue_size - 1)) != 0) {
      // 队列大小必须是 2 的幂
      return Error::kInvalidArgument;
    }

    size_t required_size = calc_size(queue_size, true);
    if (mem_size < required_size) {
      return Error::kOutOfMemory;
    }

    SplitVirtqueue vq;
    vq.queue_size_ = queue_size;
    vq.phys_base_ = phys_base;
    vq.event_idx_enabled_ = true;  // 默认启用 EVENT_IDX

    // 计算各部分的偏移
    vq.desc_offset_ = 0;
    size_t desc_total = sizeof(Desc) * queue_size;

    vq.avail_offset_ = align_up(desc_total, Avail::kAlign);
    size_t avail_total =
        sizeof(uint16_t) * (2 + queue_size + 1);  // +1 for used_event

    vq.used_offset_ = align_up(vq.avail_offset_ + avail_total, Used::kAlign);

    // 设置指针
    auto base = static_cast<uint8_t*>(mem);
    vq.desc_ = reinterpret_cast<volatile Desc*>(base + vq.desc_offset_);
    vq.avail_ = reinterpret_cast<volatile Avail*>(base + vq.avail_offset_);
    vq.used_ = reinterpret_cast<volatile Used*>(base + vq.used_offset_);

    // 初始化描述符空闲链表
    for (uint16_t i = 0; i < queue_size; ++i) {
      vq.desc_[i].next = (i + 1) % queue_size;
    }
    vq.free_head_ = 0;
    vq.num_free_ = queue_size;
    vq.last_used_idx_ = 0;

    // 清零 Available 和 Used Ring
    vq.avail_->flags = 0;
    vq.avail_->idx = 0;
    vq.used_->flags = 0;
    vq.used_->idx = 0;

    return vq;
  }

  /**
   * @brief 从空闲链表分配一个描述符
   * @return 成功返回描述符索引；空闲链表为空时返回 NoFreeDescriptors
   */
  [[nodiscard]] auto alloc_desc() -> Result<uint16_t> {
    if (num_free_ == 0) {
      return Error::kNoFreeDescriptors;
    }

    uint16_t idx = free_head_;
    free_head_ = desc_[free_head_].next;
    --num_free_;

    return idx;
  }

  /**
   * @brief 归还描述符到空闲链表
   * @param idx 描述符索引
   */
  auto free_desc(uint16_t idx) -> void {
    desc_[idx].next = free_head_;
    free_head_ = idx;
    ++num_free_;
  }

  /**
   * @brief 获取描述符的可变引用，用于设置 addr/len/flags/next
   * @param idx 描述符索引
   */
  [[nodiscard]] auto desc(uint16_t idx) -> volatile Desc& { return desc_[idx]; }
  /// 获取描述符的只读引用
  [[nodiscard]] auto desc(uint16_t idx) const -> const volatile Desc& {
    return desc_[idx];
  }

  /**
   * @brief 将描述符链提交到 Available Ring
   * @param head 链头描述符索引
   * @note 调用者应在 submit 前后适当插入内存屏障
   */
  auto submit(uint16_t head) -> void {
    uint16_t idx = avail_->idx;
    avail_->ring[idx % queue_size_] = head;

    // 内存屏障：确保描述符写入在更新 idx 之前完成
    __sync_synchronize();

    avail_->idx = idx + 1;
  }

  /// 检查 Used Ring 中是否有已完成的缓冲区
  [[nodiscard]] auto has_used() const -> bool {
    return last_used_idx_ != used_->idx;
  }

  /**
   * @brief 从 Used Ring 弹出一个已完成的元素
   * @return 成功返回 UsedElem{id, len}；无可用元素时返回 NoUsedBuffers
   */
  [[nodiscard]] auto pop_used() -> Result<UsedElem> {
    if (!has_used()) {
      return Error::kNoUsedBuffers;
    }

    uint16_t idx = last_used_idx_ % queue_size_;
    UsedElem elem = used_->ring[idx];

    ++last_used_idx_;

    return elem;
  }

  /// 获取描述符表的物理地址
  [[nodiscard]] auto desc_phys() const -> uint64_t {
    return phys_base_ + desc_offset_;
  }

  /// 获取 Available Ring 的物理地址
  [[nodiscard]] auto avail_phys() const -> uint64_t {
    return phys_base_ + avail_offset_;
  }

  /// 获取 Used Ring 的物理地址
  [[nodiscard]] auto used_phys() const -> uint64_t {
    return phys_base_ + used_offset_;
  }

  /// 获取队列大小
  [[nodiscard]] auto size() const -> uint16_t { return queue_size_; }

  /// 获取当前空闲描述符数量
  [[nodiscard]] auto num_free() const -> uint16_t { return num_free_; }

  /**
   * @brief 获取 Available Ring 的 used_event 字段
   * @return used_event 字段指针，如果未启用 EVENT_IDX 则返回 nullptr
   * @note 仅当协商 VIRTIO_F_EVENT_IDX 特性时有效
   * @see virtio-v1.2#2.7.10
   */
  [[nodiscard]] auto avail_used_event() -> volatile uint16_t* {
    return event_idx_enabled_ ? avail_->used_event(queue_size_) : nullptr;
  }
  [[nodiscard]] auto avail_used_event() const -> const volatile uint16_t* {
    return event_idx_enabled_ ? avail_->used_event(queue_size_) : nullptr;
  }

  /**
   * @brief 获取 Used Ring 的 avail_event 字段
   * @return avail_event 字段指针，如果未启用 EVENT_IDX 则返回 nullptr
   * @note 仅当协商 VIRTIO_F_EVENT_IDX 特性时有效
   * @see virtio-v1.2#2.7.10
   */
  [[nodiscard]] auto used_avail_event() -> volatile uint16_t* {
    return event_idx_enabled_ ? used_->avail_event(queue_size_) : nullptr;
  }
  [[nodiscard]] auto used_avail_event() const -> const volatile uint16_t* {
    return event_idx_enabled_ ? used_->avail_event(queue_size_) : nullptr;
  }

 private:
  SplitVirtqueue() = default;

  /// 描述符表指针
  volatile Desc* desc_ = nullptr;
  /// Available Ring 指针
  volatile Avail* avail_ = nullptr;
  /// Used Ring 指针
  volatile Used* used_ = nullptr;

  /// 队列大小
  uint16_t queue_size_ = 0;
  /// 空闲描述符链表头索引
  uint16_t free_head_ = 0;
  /// 空闲描述符数量
  uint16_t num_free_ = 0;
  /// 上次处理到的 used ring 索引
  uint16_t last_used_idx_ = 0;

  /// DMA 内存物理基地址
  uint64_t phys_base_ = 0;
  /// 描述符表在 DMA 内存中的偏移
  size_t desc_offset_ = 0;
  /// Available Ring 在 DMA 内存中的偏移
  size_t avail_offset_ = 0;
  /// Used Ring 在 DMA 内存中的偏移
  size_t used_offset_ = 0;
  /// 是否启用 EVENT_IDX 特性
  bool event_idx_enabled_ = false;
};

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_SRC_INCLUDE_VIRT_QUEUE_SPLIT_HPP_ */
