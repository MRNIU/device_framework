/**
 * @file traits.hpp
 * @brief 平台环境特征约束 (VirtioEnvironmentTraits concept)
 *
 * 统一平台抽象层：用 C++23 concept 替代原有的 PlatformOps 函数指针 +
 * LogFunc 模板参数的二元设计。所有核心类通过单一 Traits 模板参数获取
 * 日志、内存屏障、地址转换能力。
 *
 * @copyright Copyright The virtio_driver Contributors
 * @see 架构文档 §2.1
 */

#ifndef VIRTIO_DRIVER_TRAITS_HPP_
#define VIRTIO_DRIVER_TRAITS_HPP_

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace virtio_driver {

/// @brief 平台环境特征约束
/// @see 架构文档 §2.1
template <typename T>
concept VirtioEnvironmentTraits = requires(void* ptr, uintptr_t phys) {
  // 日志输出（std::nullptr_t 时编译期消除）
  { T::Log(static_cast<const char*>("")) } -> std::same_as<int>;

  // 内存屏障
  { T::Mb() } -> std::same_as<void>;
  { T::Rmb() } -> std::same_as<void>;
  { T::Wmb() } -> std::same_as<void>;

  // 虚拟地址 ↔ 物理地址转换
  { T::VirtToPhys(ptr) } -> std::same_as<uintptr_t>;
  { T::PhysToVirt(phys) } -> std::same_as<void*>;
};

/// @brief 零开销默认 Traits（日志编译期消除，屏障为空操作，地址恒等映射）
struct NullTraits {
  static auto Log(const char* /*fmt*/, ...) -> int { return 0; }
  static auto Mb() -> void {}
  static auto Rmb() -> void {}
  static auto Wmb() -> void {}
  static auto VirtToPhys(void* p) -> uintptr_t {
    return reinterpret_cast<uintptr_t>(p);
  }
  static auto PhysToVirt(uintptr_t a) -> void* {
    return reinterpret_cast<void*>(a);
  }
};

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_TRAITS_HPP_ */
