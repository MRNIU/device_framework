/**
 * @file platform_impl.h
 * @brief 测试环境的平台操作实现
 * @copyright Copyright The virtio_driver Contributors
 */

#ifndef VIRTIO_DRIVER_TESTS_PLATFORM_IMPL_H_
#define VIRTIO_DRIVER_TESTS_PLATFORM_IMPL_H_

#include <cstdint>
#include <cstring>

#include "platform.h"

// 简单的内存分配器（用于测试）
// 在实际内核中，应该使用物理页分配器
namespace test_platform {

// 简单的静态内存池（256KB）
constexpr size_t kMemoryPoolSize = 256 * 1024;
alignas(4096) static uint8_t memory_pool[kMemoryPoolSize];
static size_t memory_pool_offset = 0;

/**
 * @brief 简单的页分配实现
 */
inline void* alloc_pages(size_t num_pages) {
  const size_t page_size = 4096;
  size_t bytes = num_pages * page_size;

  // 对齐到页边界
  size_t aligned_offset =
      (memory_pool_offset + page_size - 1) & ~(page_size - 1);

  if (aligned_offset + bytes > kMemoryPoolSize) {
    return nullptr;  // 内存不足
  }

  void* ptr = &memory_pool[aligned_offset];
  memory_pool_offset = aligned_offset + bytes;

  // 清零内存
  memset(ptr, 0, bytes);

  return ptr;
}

/**
 * @brief 简单的页释放实现（当前实现不支持真正的释放）
 */
inline void free_pages(void* ptr, size_t num_pages) {
  // 简单实现：不回收内存
  // 在实际内核中，应该将页返回到页分配器
  (void)ptr;
  (void)num_pages;
}

/**
 * @brief 虚拟地址转物理地址
 * 在测试环境中，假设虚拟地址等于物理地址
 */
inline uint64_t virt_to_phys(void* vaddr) {
  return reinterpret_cast<uint64_t>(vaddr);
}

/**
 * @brief 内存屏障实现
 */
inline void memory_barrier() { __asm__ volatile("fence rw, rw" ::: "memory"); }

inline void read_memory_barrier() {
  __asm__ volatile("fence r, r" ::: "memory");
}

inline void write_memory_barrier() {
  __asm__ volatile("fence w, w" ::: "memory");
}

/**
 * @brief 获取平台操作结构
 */
inline virtio_driver::PlatformOps get_platform_ops() {
  return virtio_driver::PlatformOps{
      .alloc_pages = alloc_pages,
      .free_pages = free_pages,
      .virt_to_phys = virt_to_phys,
      .mb = memory_barrier,
      .rmb = read_memory_barrier,
      .wmb = write_memory_barrier,
      .page_size = 4096,
  };
}

}  // namespace test_platform

#endif /* VIRTIO_DRIVER_TESTS_PLATFORM_IMPL_H_ */
