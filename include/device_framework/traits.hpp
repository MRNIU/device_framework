/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_TRAITS_HPP_
#define DEVICE_FRAMEWORK_TRAITS_HPP_

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace device_framework {

/// @name 正交能力 Concepts
/// 驱动族通过 concept refinement 按需组合所需的平台能力。
/// @{

/**
 * @brief 基础环境特征约束（所有驱动必需）
 *
 * 提供日志输出能力，是最小的 Traits 约束。
 * 非 DMA 驱动（如 NS16550A、PL011）仅需满足此 concept。
 */
template <typename T>
concept EnvironmentTraits = requires {
  { T::Log(static_cast<const char*>("")) } -> std::same_as<int>;
};

/**
 * @brief 内存屏障能力
 *
 * 需要与设备共享内存的驱动（DMA、多核共享缓冲区）应额外满足此约束。
 */
template <typename T>
concept BarrierTraits = requires {
  { T::Mb() } -> std::same_as<void>;
  { T::Rmb() } -> std::same_as<void>;
  { T::Wmb() } -> std::same_as<void>;
};

/**
 * @brief DMA 地址转换能力
 *
 * 需要在虚拟地址和物理地址之间转换的驱动应额外满足此约束。
 */
template <typename T>
concept DmaTraits = requires(void* ptr, uintptr_t phys) {
  { T::VirtToPhys(ptr) } -> std::same_as<uintptr_t>;
  { T::PhysToVirt(phys) } -> std::same_as<void*>;
};

/// @}

/**
 * @brief 零开销默认 Traits
 *
 * 满足全部 concept（EnvironmentTraits + BarrierTraits + DmaTraits），
 * 所有方法在编译期消除。
 */
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

}  // namespace device_framework

#endif /* DEVICE_FRAMEWORK_TRAITS_HPP_ */
