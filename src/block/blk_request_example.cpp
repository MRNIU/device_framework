/**
 * @file blk_request_example.cpp
 * @brief BlkRequest RAII 封装使用示例
 * @copyright Copyright The virtio_driver Contributors
 *
 * 本文件展示了如何使用 BlkRequest 类进行块设备操作，
 * 相比传统方式，RAII 封装提供了更安全、更简洁的接口。
 */

#include "blk_request.hpp"
#include "platform.h"
#include "transport/mmio.hpp"
#include "virtio_blk.hpp"

using namespace virtio_driver;
using namespace virtio_driver::blk;

/**
 * @brief 示例 1：同步读取 - 最简单的使用方式
 *
 * 使用 wait() 阻塞等待请求完成，类似于传统的同步 I/O。
 */
void example_sync_read(VirtioBlk& blk) {
  uint8_t data[512];

  // 创建并提交读取请求
  auto req_result = BlkRequest::read(blk, 0, data);
  if (!req_result.has_value()) {
    // 处理错误（例如：描述符不足）
    return;
  }

  auto req = std::move(req_result.value());

  // 阻塞等待完成
  auto status = req.wait();

  if (status == BlkStatus::kOk) {
    // 读取成功，使用 data
    for (size_t i = 0; i < 512; ++i) {
      // 处理数据...
    }
  } else if (status == BlkStatus::kIoErr) {
    // 处理 I/O 错误
  } else if (status == BlkStatus::kUnsupp) {
    // 操作不支持
  }

  // req 析构时自动清理资源
}

/**
 * @brief 示例 2：同步写入
 */
void example_sync_write(VirtioBlk& blk) {
  uint8_t data[512];

  // 准备要写入的数据
  for (size_t i = 0; i < 512; ++i) {
    data[i] = static_cast<uint8_t>(i);
  }

  // 创建并提交写入请求
  auto req = BlkRequest::write(blk, 0, data).value();

  // 等待完成
  auto status = req.wait();

  if (status == BlkStatus::kOk) {
    // 写入成功
  }
}

/**
 * @brief 示例 3：异步操作 - 提交后继续执行其他任务
 *
 * 适合需要并发处理多个请求的场景。
 */
void example_async_operations(VirtioBlk& blk) {
  uint8_t data1[512];
  uint8_t data2[512];

  // 提交多个请求（异步）
  auto req1 = BlkRequest::read(blk, 0, data1).value();
  auto req2 = BlkRequest::read(blk, 1, data2).value();

  // 执行其他任务...
  // do_something_else();

  // 轮询检查请求是否完成
  while (!req1.is_complete() || !req2.is_complete()) {
    // 在实际环境中，这里应该处理中断
    blk.process_used();

    // 检查第一个请求
    if (req1.is_complete()) {
      auto status = req1.status().value();
      if (status == BlkStatus::kOk) {
        // 处理 data1
      }
    }

    // 检查第二个请求
    if (req2.is_complete()) {
      auto status = req2.status().value();
      if (status == BlkStatus::kOk) {
        // 处理 data2
      }
    }
  }
}

/**
 * @brief 示例 4：缓存刷新
 *
 * 确保所有待写入的数据都被持久化到存储介质。
 */
void example_flush(VirtioBlk& blk) {
  // 首先写入一些数据
  uint8_t data[512] = {0};
  auto write_req = BlkRequest::write(blk, 0, data).value();
  write_req.wait();

  // 刷新缓存，确保数据已写入持久化存储
  auto flush_req = BlkRequest::flush(blk).value();
  auto status = flush_req.wait();

  if (status == BlkStatus::kOk) {
    // 刷新成功，数据已持久化
  }
}

/**
 * @brief 示例 5：RAII 自动清理 - 作用域结束时自动等待完成
 */
void example_raii_cleanup(VirtioBlk& blk) {
  uint8_t data[512];

  {
    // 在内部作用域创建请求
    auto req = BlkRequest::read(blk, 0, data).value();

    // 不显式调用 wait()
    // ...

  }  // req 析构时会自动等待请求完成并清理资源

  // 此时可以安全地使用 data（请求已完成）
}

/**
 * @brief 示例 6：错误处理
 */
void example_error_handling(VirtioBlk& blk) {
  uint8_t data[512];

  // 尝试创建请求
  auto req_result = BlkRequest::read(blk, 0, data);

  if (!req_result.has_value()) {
    // 请求创建失败
    auto error = req_result.error();

    switch (error) {
      case ErrorCode::kNoDescriptors:
        // virtqueue 描述符不足，需要等待一些请求完成
        blk.process_used();
        // 重试...
        break;

      case ErrorCode::kInvalidParameter:
        // 参数无效
        break;

      default:
        // 其他错误
        break;
    }
    return;
  }

  // 请求创建成功，等待完成
  auto req = std::move(req_result.value());
  auto status = req.wait();

  // 处理请求状态
  if (status != BlkStatus::kOk) {
    // 请求失败
  }
}

/**
 * @brief 示例 7：移动语义 - 转移请求所有权
 */
auto create_and_submit_read(VirtioBlk& blk, uint64_t sector, uint8_t* data)
    -> Result<BlkRequest> {
  // 创建请求并返回（利用移动语义）
  return BlkRequest::read(blk, sector, data);
}

void example_move_semantics(VirtioBlk& blk) {
  uint8_t data[512];

  // 从函数接收请求对象
  auto req_result = create_and_submit_read(blk, 0, data);
  if (!req_result.has_value()) {
    return;
  }

  auto req = std::move(req_result.value());

  // 可以将请求存储到容器中
  std::vector<BlkRequest> pending_requests;
  pending_requests.push_back(std::move(req));

  // 稍后处理
  for (auto& r : pending_requests) {
    r.wait();
  }
}

/**
 * @brief 对比：传统方式 vs RAII 封装
 */
void comparison_example(VirtioBlk& blk) {
  uint8_t data[512];

  // === 传统方式 ===
  {
    // 需要手动管理这些缓冲区的生命周期
    BlkReqHeader header{};
    uint8_t status_byte;

    // 提交请求
    auto result = blk.read(0, data, &status_byte, &header);
    if (!result.has_value()) {
      return;
    }

    // 手动轮询等待完成
    while (status_byte == 0xFF) {  // 0xFF 表示未完成
      blk.process_used();
    }

    // 检查状态
    auto status = static_cast<BlkStatus>(status_byte);
    if (status == BlkStatus::kOk) {
      // 使用 data
    }

    // 需要确保 header 和 status_byte 在请求期间一直有效
    // 如果在栈上分配，必须小心管理作用域
  }

  // === RAII 封装方式 ===
  {
    // 所有缓冲区自动管理
    auto req = BlkRequest::read(blk, 0, data).value();

    // 简单的阻塞等待
    auto status = req.wait();

    if (status == BlkStatus::kOk) {
      // 使用 data
    }

    // 自动清理，无需担心生命周期
  }
}

/**
 * @brief 完整示例：初始化设备并使用 RAII 封装
 */
void full_example() {
  // 1. 假设已经有了平台操作接口
  PlatformOps platform{
      .virt_to_phys = [](const void* virt) -> uint64_t {
        // 实际实现：虚拟地址转物理地址
        return reinterpret_cast<uint64_t>(virt);
      },
      .mb = []() { /* 内存屏障 */},
      .rmb = []() { /* 读屏障 */},
      .wmb = []() { /* 写屏障 */},
  };

  // 2. 创建传输层（假设使用 MMIO）
  uint64_t base_addr = 0x10001000;  // 示例地址
  auto transport_result = MmioTransport::create(base_addr);
  if (!transport_result.has_value()) {
    return;
  }
  auto& transport = transport_result.value();

  // 3. 分配并创建 virtqueue
  uint16_t queue_size = 16;
  size_t vq_size = SplitVirtqueue::calc_size(queue_size);
  // 实际环境中应该使用 DMA 内存分配
  auto* vq_mem = new uint8_t[vq_size];
  uint64_t vq_phys = reinterpret_cast<uint64_t>(vq_mem);

  auto vq_result = SplitVirtqueue::create(vq_mem, vq_size, queue_size, vq_phys);
  if (!vq_result.has_value()) {
    delete[] vq_mem;
    return;
  }
  auto& vq = vq_result.value();

  // 4. 创建并初始化块设备
  auto blk_result = VirtioBlk::create(transport, vq, platform);
  if (!blk_result.has_value()) {
    delete[] vq_mem;
    return;
  }
  auto& blk = blk_result.value();

  // 5. 读取设备配置
  auto config = blk.read_config();
  // uint64_t capacity = config.capacity;  // 扇区数

  // 6. 使用 RAII 封装进行 I/O 操作
  uint8_t data[512];

  // 同步读取
  auto req = BlkRequest::read(blk, 0, data).value();
  auto status = req.wait();

  if (status == BlkStatus::kOk) {
    // 数据读取成功
  }

  // 7. 清理（实际环境中可能需要更复杂的清理逻辑）
  delete[] vq_mem;
}
