/**
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_VIRT_QUEUE_SPLIT_HPP_
#define VIRTIO_DRIVER_VIRT_QUEUE_SPLIT_HPP_

#include "virtio_driver/expected.hpp"
#include "virtio_driver/virt_queue/misc.hpp"

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
 * @warning 非线程安全：此类的所有方法均不是线程安全的。
 *          如果多个线程/核需要访问同一个
 * virtqueue，调用者必须使用外部同步机制（如自旋锁或互斥锁）。
 * @warning 单生产者-单消费者：描述符分配和提交应由同一线程执行，
 *                          已用缓冲区回收应由另一线程执行（通常在中断处理程序中）。
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
    [[nodiscard]] auto used_event(uint16_t queue_size) volatile
        -> volatile uint16_t* {
      return ring + queue_size;
    }

    [[nodiscard]] auto used_event(uint16_t queue_size) const volatile -> const
        volatile uint16_t* {
      return ring + queue_size;
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
    [[nodiscard]] auto avail_event(uint16_t queue_size) volatile
        -> volatile uint16_t* {
      // avail_event 位于 ring[queue_size] 之后，需要先转换为字节指针计算偏移
      auto* byte_ptr = reinterpret_cast<volatile uint8_t*>(ring);
      return reinterpret_cast<volatile uint16_t*>(byte_ptr + sizeof(UsedElem) *
                                                                 queue_size);
    }

    [[nodiscard]] auto avail_event(uint16_t queue_size) const volatile -> const
        volatile uint16_t* {
      const auto* byte_ptr = reinterpret_cast<const volatile uint8_t*>(ring);
      return reinterpret_cast<const volatile uint16_t*>(
          byte_ptr + sizeof(UsedElem) * queue_size);
    }
  } __attribute__((packed));

  /**
   * @brief 计算给定队列大小所需的 DMA 内存字节数
   *
   * 该静态方法计算分配 Split Virtqueue 所需的连续 DMA 内存大小，
   * 包括 Descriptor Table、Available Ring 和 Used Ring 三部分，
   * 以及各部分之间的对齐填充。
   *
   * @param queue_size 队列大小（必须为 2 的幂，范围：1 ~ 32768）
   * @param event_idx 是否启用 VIRTIO_F_EVENT_IDX 特性（影响 used_event 和
   * avail_event 字段）
   * @return 所需的 DMA 内存字节数
   *
   * @note queue_size 应来自设备报告的 queue_num_max
   * @note 该函数为 constexpr，可在编译期求值
   * @see virtio-v1.2#2.6 Split Virtqueues
   */
  [[nodiscard]] static constexpr auto CalcSize(uint16_t queue_size,
                                               bool event_idx = true,
                                               size_t used_align = Used::kAlign)
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
    size_t avail_off = AlignUp(desc_total, Avail::kAlign);
    size_t used_off = AlignUp(avail_off + avail_total, used_align);

    return used_off + used_total;
  }

  /**
   * @brief 从预分配的 DMA 缓冲区构造 SplitVirtqueue
   *
   * 在给定的 DMA 连续内存上初始化 Descriptor Table、Available Ring
   * 和 Used Ring，并构建空闲描述符链表。
   *
   * @param dma_buf  DMA 缓冲区虚拟地址（必须已清零，大小 >= CalcSize()）
   * @param phys_base DMA 缓冲区的客户机物理基地址
   * @param queue_size 队列大小（必须为 2 的幂，范围：1 ~ 32768）
   * @param event_idx 是否启用 VIRTIO_F_EVENT_IDX 特性
   * @param used_align Used Ring 的对齐要求（modern = 4，legacy MMIO = 4096）
   *
   * @pre dma_buf != nullptr
   * @pre queue_size 为 2 的幂
   * @pre dma_buf 指向的内存大小 >= CalcSize(queue_size, event_idx, used_align)
   * @pre dma_buf 已清零
   * @post IsValid() == true（前置条件满足时）
   * @post 所有描述符处于空闲链表中
   * @see virtio-v1.2#2.7
   */
  SplitVirtqueue(void* dma_buf, uint64_t phys_base, uint16_t queue_size,
                 bool event_idx = true, size_t used_align = Used::kAlign)
      : queue_size_(queue_size),
        event_idx_enabled_(event_idx),
        phys_base_(phys_base) {
    if (dma_buf == nullptr || !IsPowerOfTwo(queue_size)) {
      return;
    }

    // 计算各区域偏移量
    size_t desc_total = static_cast<size_t>(sizeof(Desc)) * queue_size;
    size_t avail_total = sizeof(uint16_t) * (2 + queue_size);
    if (event_idx) {
      avail_total += sizeof(uint16_t);
    }

    desc_offset_ = 0;
    avail_offset_ = AlignUp(desc_total, Avail::kAlign);
    size_t used_total = sizeof(uint16_t) * 2 + sizeof(UsedElem) * queue_size;
    if (event_idx) {
      used_total += sizeof(uint16_t);
    }
    used_offset_ = AlignUp(avail_offset_ + avail_total, used_align);

    // 设置指针
    auto* base = static_cast<uint8_t*>(dma_buf);
    desc_ = reinterpret_cast<volatile Desc*>(base + desc_offset_);
    avail_ = reinterpret_cast<volatile Avail*>(base + avail_offset_);
    used_ = reinterpret_cast<volatile Used*>(base + used_offset_);

    // 初始化空闲描述符链表
    for (uint16_t i = 0; i < queue_size; ++i) {
      desc_[i].next = static_cast<uint16_t>(i + 1);
    }
    // 末尾描述符使用 sentinel 值，避免越界索引
    desc_[queue_size - 1].next = 0xFFFF;
    free_head_ = 0;
    num_free_ = queue_size;
    last_used_idx_ = 0;

    is_valid_ = true;
  }

  /**
   * @brief 检查 virtqueue 是否成功初始化
   *
   * @return true 表示初始化成功，false 表示参数无效
   */
  [[nodiscard]] auto IsValid() const -> bool { return is_valid_; }

  /**
   * @brief 从空闲链表分配一个描述符
   *
   * 从空闲描述符链表中取出一个描述符，供上层使用。
   * 调用者必须填充描述符的 addr、len、flags 和 next 字段。
   *
   * @return 成功返回描述符索引（range: 0 ~ queue_size-1）；
   *         空闲链表为空时返回 ErrorCode::kNoFreeDescriptors
   *
   * @warning 非线程安全：多个线程同时调用可能导致竞态条件
   * @see virtio-v1.2#2.7.5 The Virtqueue Descriptor Table
   * @see virtio-v1.2#2.7.13 Supplying Buffers to The Device
   */
  [[nodiscard]] auto AllocDesc() -> Expected<uint16_t> {
    if (num_free_ == 0) {
      return std::unexpected(Error{ErrorCode::kNoFreeDescriptors});
    }

    uint16_t idx = free_head_;
    free_head_ = desc_[free_head_].next;
    --num_free_;

    return idx;
  }

  /**
   * @brief 归还描述符到空闲链表
   *
   * 将不再使用的描述符放回空闲链表，供后续分配使用。
   * 对于描述符链，调用者必须按正确的顺序释放链中的每个描述符。
   *
   * @param idx 要释放的描述符索引（必须为之前分配的有效索引）
   *
   * @warning 非线程安全：多个线程同时调用可能导致竞态条件
   * @warning 释放已释放的描述符或无效索引会导致空闲链表损坏
   * @see virtio-v1.2#2.7.14 Receiving Used Buffers From The Device
   */
  auto FreeDesc(uint16_t idx) -> Expected<void> {
    if (idx >= queue_size_) {
      return std::unexpected(Error{ErrorCode::kInvalidDescriptor});
    }
    desc_[idx].next = free_head_;
    free_head_ = idx;
    ++num_free_;
    return {};
  }

  /**
   * @brief 获取描述符的可变引用
   *
   * 用于设置描述符的 addr、len、flags 和 next 字段。
   * 调用者必须确保索引有效（通过 AllocDesc() 分配）。
   *
   * @param idx 描述符索引（必须 < queue_size）
   * @return 描述符的 volatile 引用（用于与设备共享内存）
   * @see virtio-v1.2#2.7.5 The Virtqueue Descriptor Table
   */
  [[nodiscard]] auto GetDesc(uint16_t idx) -> Expected<volatile Desc*> {
    if (idx >= queue_size_) {
      return std::unexpected(Error{ErrorCode::kInvalidDescriptor});
    }
    return &desc_[idx];
  }

  /**
   * @brief 获取描述符的只读引用
   *
   * @param idx 描述符索引（必须 < queue_size）
   * @return 描述符的 const volatile 指针，失败返回错误
   */
  [[nodiscard]] auto GetDesc(uint16_t idx) const
      -> Expected<const volatile Desc*> {
    if (idx >= queue_size_) {
      return std::unexpected(Error{ErrorCode::kInvalidDescriptor});
    }
    return &desc_[idx];
  }

  /**
   * @brief 将描述符链提交到 Available Ring
   *
   * 将描述符链的头部索引放入 Available Ring，使其对设备可见。
   * 调用此方法后，调用者应使用内存屏障确保 idx 更新对设备可见，
   * 然后通过 Transport::NotifyQueue() 通知设备。
   *
   * @param head 描述符链头部索引（必须为有效的已分配描述符）
   *
   * @note 调用者必须在调用此方法前使用写屏障 (wmb) 确保描述符写入完成
   * @note 调用者必须在调用此方法后使用内存屏障 (mb) 确保 idx 更新对设备可见
   * @note 这样设计是因为 SplitVirtqueue 不保存 PlatformOps 引用，
   *       由上层管理内存屏障
   *
   * @warning 非线程安全：多个线程同时调用可能导致竞态条件
   * @see virtio-v1.2#2.7.13 Supplying Buffers to The Device
   */
  auto Submit(uint16_t head) -> void {
    uint16_t idx = avail_->idx;
    avail_->ring[idx % queue_size_] = head;

    // 编译器屏障：防止编译器重排序
    // 真正的内存屏障由调用者负责（通过 PlatformOps）
    asm volatile("" ::: "memory");

    avail_->idx = idx + 1;
  }

  /**
   * @brief 检查 Used Ring 中是否有已完成的缓冲区
   *
   * 通过比较驱动程序上次处理的 idx 与设备当前的 idx，
   * 判断是否有新的已处理缓冲区可供回收。
   *
   * @return true 表示有已完成的缓冲区可用，false 表示没有
   * @see virtio-v1.2#2.7.14 Receiving Used Buffers From The Device
   */
  [[nodiscard]] auto HasUsed() const -> bool {
    return last_used_idx_ != used_->idx;
  }

  /**
   * @brief 从 Used Ring 弹出一个已完成的元素
   *
   * 从 Used Ring 中取出下一个设备已处理完成的缓冲区。
   * 返回的 UsedElem 包含：
   * - id: 描述符链头索引（对应之前提交的 head）
   * - len: 设备写入的字节数（仅对 Device-writable 缓冲区有意义）
   *
   * @return 成功返回 UsedElem{id, len}；
   *         无可用元素时返回 ErrorCode::kNoUsedBuffers
   *
   * @warning 非线程安全：多个线程同时调用可能导致竞态条件
   * @see virtio-v1.2#2.7.14 Receiving Used Buffers From The Device
   */
  [[nodiscard]] auto PopUsed() -> Expected<UsedElem> {
    if (!HasUsed()) {
      return std::unexpected(Error{ErrorCode::kNoUsedBuffers});
    }

    uint16_t idx = last_used_idx_ % queue_size_;
    UsedElem elem;
    elem.id = used_->ring[idx].id;
    elem.len = used_->ring[idx].len;

    ++last_used_idx_;

    return elem;
  }

  /**
   * @brief 获取描述符表的物理地址
   *
   * 返回 Descriptor Table 在客户机物理内存中的地址。
   * 该地址应通过 Transport::SetQueueDesc() 配置给设备。
   *
   * @return 描述符表的 64 位物理地址
   * @see virtio-v1.2#2.7.5 The Virtqueue Descriptor Table
   */
  [[nodiscard]] auto DescPhys() const -> uint64_t {
    return phys_base_ + desc_offset_;
  }

  /**
   * @brief 获取 Available Ring 的物理地址
   *
   * 返回 Available Ring 在客户机物理内存中的地址。
   * 该地址应通过 Transport::SetQueueAvail() 配置给设备。
   *
   * @return Available Ring 的 64 位物理地址
   * @see virtio-v1.2#2.7.6 The Virtqueue Available Ring
   */
  [[nodiscard]] auto AvailPhys() const -> uint64_t {
    return phys_base_ + avail_offset_;
  }

  /**
   * @brief 获取 Used Ring 的物理地址
   *
   * 返回 Used Ring 在客户机物理内存中的地址。
   * 该地址应通过 Transport::SetQueueUsed() 配置给设备。
   *
   * @return Used Ring 的 64 位物理地址
   * @see virtio-v1.2#2.7.8 The Virtqueue Used Ring
   */
  [[nodiscard]] auto UsedPhys() const -> uint64_t {
    return phys_base_ + used_offset_;
  }

  /**
   * @brief 获取队列大小
   *
   * @return 队列大小（描述符数量）
   */
  [[nodiscard]] auto Size() const -> uint16_t { return queue_size_; }

  /**
   * @brief 获取当前空闲描述符数量
   *
   * 返回当前可用于分配的描述符数量。
   * 调用者可以根据此值判断是否有足够的描述符可供使用。
   *
   * @return 空闲描述符数量（range: 0 ~ queue_size）
   */
  [[nodiscard]] auto NumFree() const -> uint16_t { return num_free_; }

  /**
   * @brief 获取 Available Ring 的 used_event 字段
   *
   * 该字段用于 EVENT_IDX 特性，
   * 驱动程序通过写入此字段告知设备什么时候需要发送中断。
   *
   * @return used_event 字段指针，如果未启用 EVENT_IDX 则返回 nullptr
   *
   * @note 仅当协商 VIRTIO_F_EVENT_IDX 特性时有效
   * @see virtio-v1.2#2.7.10 Available Buffer Notification Suppression
   */
  [[nodiscard]] auto AvailUsedEvent() -> volatile uint16_t* {
    return event_idx_enabled_ ? avail_->used_event(queue_size_) : nullptr;
  }

  [[nodiscard]] auto AvailUsedEvent() const -> const volatile uint16_t* {
    return event_idx_enabled_ ? avail_->used_event(queue_size_) : nullptr;
  }

  /**
   * @brief 获取 Used Ring 的 avail_event 字段
   *
   * 该字段用于 EVENT_IDX 特性，
   * 设备通过写入此字段告知驱动程序什么时候需要发送通知。
   *
   * @return avail_event 字段指针，如果未启用 EVENT_IDX 则返回 nullptr
   *
   * @note 仅当协商 VIRTIO_F_EVENT_IDX 特性时有效
   * @see virtio-v1.2#2.7.10 Available Buffer Notification Suppression
   * @see virtio-v1.2#2.7.21 Driver notifications
   */
  [[nodiscard]] auto UsedAvailEvent() -> volatile uint16_t* {
    return event_idx_enabled_ ? used_->avail_event(queue_size_) : nullptr;
  }

  [[nodiscard]] auto UsedAvailEvent() const -> const volatile uint16_t* {
    return event_idx_enabled_ ? used_->avail_event(queue_size_) : nullptr;
  }

  /// @name 构造/析构函数
  /// @{
  SplitVirtqueue(const SplitVirtqueue&) = delete;
  SplitVirtqueue(SplitVirtqueue&&) = delete;
  auto operator=(const SplitVirtqueue&) -> SplitVirtqueue& = delete;
  auto operator=(SplitVirtqueue&&) -> SplitVirtqueue& = delete;
  ~SplitVirtqueue() = default;
  /// @}

 private:
  /// 描述符表指针（指向 DMA 内存）
  volatile Desc* desc_ = nullptr;
  /// Available Ring 指针（指向 DMA 内存）
  volatile Avail* avail_ = nullptr;
  /// Used Ring 指针（指向 DMA 内存）
  volatile Used* used_ = nullptr;

  /// 队列大小（描述符数量，必须为 2 的幂）
  uint16_t queue_size_ = 0;
  /// 空闲描述符链表头索引
  uint16_t free_head_ = 0;
  /// 空闲描述符数量
  uint16_t num_free_ = 0;
  /// 上次处理到的 Used Ring 索引（用于 PopUsed）
  uint16_t last_used_idx_ = 0;

  /// DMA 内存物理基地址（客户机物理地址）
  uint64_t phys_base_ = 0;
  /// 描述符表在 DMA 内存中的偏移量（字节）
  size_t desc_offset_ = 0;
  /// Available Ring 在 DMA 内存中的偏移量（字节）
  size_t avail_offset_ = 0;
  /// Used Ring 在 DMA 内存中的偏移量（字节）
  size_t used_offset_ = 0;
  /// 是否启用 VIRTIO_F_EVENT_IDX 特性
  bool event_idx_enabled_ = false;
  /// 初始化是否成功
  bool is_valid_ = false;
};

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_VIRT_QUEUE_SPLIT_HPP_ */
