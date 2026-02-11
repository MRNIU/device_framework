/**
 * @file blk_request.hpp
 * @brief VirtIO 块设备 RAII 请求封装
 * @copyright Copyright The virtio_driver Contributors
 *
 * 该文件提供了 BlkRequest 类，用于自动管理块设备请求的生命周期。
 * 使用 RAII 模式自动处理资源清理，避免手动管理缓冲区带来的内存安全问题。
 *
 * ### 使用示例
 *
 * ```cpp
 * // 同步读取
 * VirtioBlk blk = ...;
 * uint8_t data[512];
 *
 * auto req = BlkRequest::read(blk, 0, data);
 * auto status = req.wait();  // 阻塞等待完成
 * if (status == BlkStatus::kOk) {
 *     // 使用 data
 * }
 * // 析构时自动清理
 *
 * // 异步读取
 * auto req = BlkRequest::read(blk, 0, data);
 * // ... 执行其他操作 ...
 * if (req.is_complete()) {
 *     auto status = req.status().value();
 * }
 *
 * // 写入
 * auto req = BlkRequest::write(blk, 0, data);
 * req.wait();
 * ```
 */

#ifndef VIRTIO_DRIVER_SRC_INCLUDE_BLK_REQUEST_HPP_
#define VIRTIO_DRIVER_SRC_INCLUDE_BLK_REQUEST_HPP_

#include <optional>

#include "virtio_blk.hpp"

namespace virtio_driver::blk {

/**
 * @brief VirtIO 块设备请求 RAII 封装
 *
 * 该类使用 RAII 模式管理块设备请求的生命周期：
 * - 自动管理请求头和状态字节的内存
 * - 提供同步和异步操作接口
 * - 析构时自动清理资源
 *
 * ### 生命周期保证
 * - 请求头和状态字节在对象生命周期内始终有效
 * - 数据缓冲区由调用者管理，但必须在请求完成前保持有效
 * - 移动语义支持（禁止拷贝）
 *
 * ### 线程安全
 * 此类不是线程安全的，同一个请求对象不应在多线程间共享。
 * VirtioBlk 本身也不提供线程安全保证。
 */
class BlkRequest {
 public:
  /**
   * @brief 创建读取请求
   *
   * @param blk 块设备实例
   * @param sector 起始扇区号（以 512 字节为单位）
   * @param data 数据缓冲区（至少 512 字节，必须在请求完成前保持有效）
   * @return Expected<BlkRequest> 成功返回请求对象，失败返回错误
   * @retval ErrorCode::kNoDescriptors virtqueue 描述符不足
   */
  [[nodiscard]] static auto read(VirtioBlk& blk, uint64_t sector, uint8_t* data)
      -> Expected<BlkRequest> {
    BlkRequest req(blk, ReqType::kIn, sector, data);
    auto result = req.submit();
    if (!result.has_value()) {
      return result.error();
    }
    return req;
  }

  /**
   * @brief 创建写入请求
   *
   * @param blk 块设备实例
   * @param sector 起始扇区号（以 512 字节为单位）
   * @param data 数据缓冲区（至少 512 字节，必须在请求完成前保持有效）
   * @return Expected<BlkRequest> 成功返回请求对象，失败返回错误
   * @retval ErrorCode::kNoDescriptors virtqueue 描述符不足
   */
  [[nodiscard]] static auto write(VirtioBlk& blk, uint64_t sector,
                                  const uint8_t* data) -> Expected<BlkRequest> {
    BlkRequest req(blk, ReqType::kOut, sector, const_cast<uint8_t*>(data));
    auto result = req.submit();
    if (!result.has_value()) {
      return result.error();
    }
    return req;
  }

  /**
   * @brief 创建缓存刷新请求
   *
   * @param blk 块设备实例
   * @return Expected<BlkRequest> 成功返回请求对象，失败返回错误
   * @retval ErrorCode::kNoDescriptors virtqueue 描述符不足
   *
   * @note 需要设备协商 VIRTIO_BLK_F_FLUSH 特性
   */
  [[nodiscard]] static auto flush(VirtioBlk& blk) -> Expected<BlkRequest> {
    // Flush 不需要数据缓冲区，但为了兼容 do_request，分配临时缓冲区
    BlkRequest req(blk, ReqType::kFlush, 0, nullptr);
    // 分配内部数据缓冲区（虽然 Flush 不使用，但 do_request 需要）
    req.internal_data_buffer_[0] = 0;
    req.data_ = req.internal_data_buffer_;
    auto result = req.submit();
    if (!result.has_value()) {
      return result.error();
    }
    return req;
  }

  /**
   * @brief 析构函数 - 自动清理资源
   *
   * 如果请求未完成，会自动等待完成再清理。
   * 建议在析构前显式调用 wait() 以避免意外阻塞。
   */
  ~BlkRequest() {
    if (submitted_ && !completed_) {
      // 请求已提交但未完成，需要等待
      wait();
    }
  }

  // 禁止拷贝
  BlkRequest(const BlkRequest&) = delete;
  auto operator=(const BlkRequest&) -> BlkRequest& = delete;

  /**
   * @brief 移动构造函数
   */
  BlkRequest(BlkRequest&& other) noexcept
      : blk_(other.blk_),
        header_(other.header_),
        status_byte_(other.status_byte_),
        data_(other.data_),
        desc_head_(other.desc_head_),
        submitted_(other.submitted_),
        completed_(other.completed_) {
    // 标记源对象为已完成，避免重复清理
    other.blk_ = nullptr;
    other.submitted_ = false;
    other.completed_ = true;
  }

  /**
   * @brief 移动赋值运算符
   */
  auto operator=(BlkRequest&& other) noexcept -> BlkRequest& {
    if (this != &other) {
      // 清理当前资源
      if (submitted_ && !completed_) {
        wait();
      }

      // 移动资源
      blk_ = other.blk_;
      header_ = other.header_;
      status_byte_ = other.status_byte_;
      data_ = other.data_;
      desc_head_ = other.desc_head_;
      submitted_ = other.submitted_;
      completed_ = other.completed_;

      // 标记源对象为已完成
      other.blk_ = nullptr;
      other.submitted_ = false;
      other.completed_ = true;
    }
    return *this;
  }

  /**
   * @brief 检查请求是否已完成
   *
   * 非阻塞查询，会触发 process_used() 处理已完成的请求。
   *
   * @return true 如果请求已完成，false 否则
   */
  [[nodiscard]] auto is_complete() -> bool {
    if (completed_) {
      return true;
    }

    // 处理已完成的请求
    check_completion();
    return completed_;
  }

  /**
   * @brief 获取请求状态（非阻塞）
   *
   * @return std::optional<BlkStatus> 如果请求已完成返回状态，否则返回
   * std::nullopt
   */
  [[nodiscard]] auto status() -> std::optional<BlkStatus> {
    if (!is_complete()) {
      return std::nullopt;
    }
    return static_cast<BlkStatus>(status_byte_);
  }

  /**
   * @brief 等待请求完成（阻塞）
   *
   * 阻塞直到请求完成，然后返回状态。
   *
   * @return BlkStatus 请求完成状态
   *
   * @note 此函数会不断轮询 process_used()，在实际环境中应使用中断驱动
   */
  [[nodiscard]] auto wait() -> BlkStatus {
    while (!is_complete()) {
      // 在实际环境中，这里应该是等待中断
      // 当前简单轮询实现
      if (blk_ != nullptr) {
        blk_->ProcessUsed();
      }
    }
    return static_cast<BlkStatus>(status_byte_);
  }

  /**
   * @brief 获取请求头（用于调试）
   *
   * @return const BlkReqHeader& 请求头引用
   */
  [[nodiscard]] auto header() const -> const BlkReqHeader& { return header_; }

 private:
  /**
   * @brief 私有构造函数
   *
   * @param blk 块设备实例
   * @param type 请求类型
   * @param sector 起始扇区号
   * @param data 数据缓冲区（可为 nullptr，例如 flush 请求）
   */
  BlkRequest(VirtioBlk& blk, ReqType type, uint64_t sector, uint8_t* data)
      : blk_(&blk),
        header_(),
        status_byte_(0xFF),  // 初始化为无效状态
        data_(data),
        desc_head_(0),
        submitted_(false),
        completed_(false) {
    // 初始化请求头
    header_.type = static_cast<uint32_t>(type);
    header_.reserved = 0;
    header_.sector = sector;
  }

  /**
   * @brief 提交请求到设备
   *
   * @return Expected<void> 成功返回空，失败返回错误
   */
  [[nodiscard]] auto submit() -> Expected<void> {
    if (blk_ == nullptr) {
      return std::unexpected(Error{ErrorCode::kInvalidArgument});
    }

    auto type = static_cast<ReqType>(header_.type);

    // 直接调用底层 DoRequest 方法（VirtioBlk 是友元）
    // 对于 flush 等不需要数据的请求，data_ 可以为 nullptr
    auto result =
        blk_->DoRequest(type, header_.sector, data_, &status_byte_, &header_);

    if (result.has_value()) {
      submitted_ = true;
    }

    return result;
  }

  /**
   * @brief 检查请求完成状态
   *
   * 处理 used ring 并检查当前请求是否已完成。
   */
  auto check_completion() -> void {
    if (blk_ == nullptr || completed_) {
      return;
    }

    // 处理已完成的请求
    blk_->ProcessUsed();

    // 通过状态字节判断是否完成
    // 如果状态字节被设备修改（不再是 0xFF），说明请求已完成
    if (status_byte_ != 0xFF) {
      completed_ = true;
    }
  }

  /// 块设备指针（使用指针支持移动语义）
  VirtioBlk* blk_;
  /// 请求头（内部管理）
  BlkReqHeader header_;
  /// 状态字节（内部管理）
  uint8_t status_byte_;
  /// 数据缓冲区指针（由调用者管理）
  uint8_t* data_;
  /// 内部数据缓冲区（用于不需要数据的请求，如 Flush）
  uint8_t internal_data_buffer_[kSectorSize];
  /// 描述符链头索引（用于跟踪和清理）
  uint16_t desc_head_;
  /// 请求是否已提交
  bool submitted_;
  /// 请求是否已完成
  bool completed_;
};

}  // namespace virtio_driver::blk

#endif /* VIRTIO_DRIVER_SRC_INCLUDE_BLK_REQUEST_HPP_ */
