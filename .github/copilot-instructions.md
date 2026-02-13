# device_framework — Copilot / AI 指南

## 项目概述

`device_framework` 是一个 **header-only、freestanding C++23** 设备框架，提供统一的设备驱动抽象。
原名 `virtio_driver`，现已扩展为通用设备框架。

### 技术栈

- **C++23**（`-std=c++23 -ffreestanding`）
- **Deducing this**（P0847）替代 CRTP `static_cast` 或虚表
- **Concepts** 约束 Traits
- **`std::expected`** 错误处理（无异常）
- **Freestanding**：不依赖 OS，bare-metal / kernel 均可

## 项目结构

```
include/device_framework/
├── defs.h                # DeviceType 枚举
├── expected.hpp          # ErrorCode, Error, Expected<T>
├── traits.hpp            # EnvironmentTraits, BarrierTraits, DmaTraits, NullTraits
├── ops/                  # 设备操作抽象层
│   ├── device_ops_base.hpp
│   ├── char_device.hpp
│   └── block_device.hpp
├── ns16550a.hpp          # ★ NS16550A 公开入口
├── pl011.hpp             # ★ PL011 公开入口
├── virtio_blk.hpp        # ★ VirtIO 块设备公开入口
├── acpi.hpp              # ★ ACPI 公开入口
└── detail/               # 实现细节（用户不应直接包含）
    ├── uart_device.hpp   # UartDevice<Derived, DriverType> 通用 UART 适配层（使用 UartDriver concept 约束）
    ├── virtio/           # VirtIO 族
    │   ├── traits.hpp    # VirtioTraits concept
    │   ├── defs.h
    │   ├── transport/
    │   │   ├── transport.hpp  # Transport<Traits> 基类
    │   │   ├── mmio.hpp       # MmioTransport（完整实现）
    │   │   └── pci.hpp        # PciTransport（占位）
    │   ├── virt_queue/
    │   │   ├── virtqueue_base.hpp  # VirtqueueBase<Traits> 基类
    │   │   ├── split.hpp           # SplitVirtqueue（完整实现）
    │   │   └── misc.hpp            # 工具函数（AlignUp, IoVec 等）
    │   └── device/
    │       ├── device_initializer.hpp  # DeviceInitializer 初始化流程编排
    │       ├── virtio_blk_defs.h       # 块设备数据结构定义
    │       ├── virtio_blk.hpp          # 块设备驱动
    │       ├── virtio_blk_device.hpp   # BlockDevice 适配器
    │       ├── virtio_console.h   # Console 设备（占位）
    │       ├── virtio_gpu.h       # GPU 设备（占位）
    │       ├── virtio_input.h     # Input 设备（占位）
    │       └── virtio_net.h       # Net 设备（占位）
    ├── ns16550a/         # UART
    ├── pl011/            # UART
    └── acpi/             # ACPI 表结构定义

cmake/
└── riscv64-toolchain.cmake  # RISC-V 交叉编译工具链

test/                     # QEMU RISC-V 集成测试
```

## 核心架构模式

### 1. 组合式 Traits（正交能力概念）

```cpp
// 基础：仅需日志
template <typename T>
concept EnvironmentTraits = requires(const char* s) {
  { T::Log(s) } -> std::same_as<int>;
};

// 内存屏障
template <typename T>
concept BarrierTraits = requires { T::Mb(); T::Rmb(); T::Wmb(); };

// DMA 地址转换
template <typename T>
concept DmaTraits = requires(void* p, uintptr_t a) {
  { T::VirtToPhys(p) } -> std::same_as<uintptr_t>;
  { T::PhysToVirt(a) } -> std::same_as<void*>;
};

// VirtIO 组合约束
template <typename T>
concept VirtioTraits = EnvironmentTraits<T> && BarrierTraits<T> && DmaTraits<T>;

// UART 驱动接口约束（detail::UartDevice 使用）
template <typename T>
concept UartDriver = requires(const T& driver, uint8_t ch) {
  { driver.PutChar(ch) } -> std::same_as<void>;
  { driver.TryGetChar() } -> std::same_as<std::optional<uint8_t>>;
  { driver.HasData() } -> std::same_as<bool>;
};
```

- NS16550A / PL011 仅需 `EnvironmentTraits`（无 DMA）
- VirtIO 需要 `VirtioTraits`（全部三个）
- 自定义驱动可按需组合

### 2. Ops 层（Deducing this）

```
DeviceOperationsBase<Derived>   // Open, Release, Read, Write, Mmap, Ioctl, HandleInterrupt
├── CharDevice<Derived>          // PutChar, GetChar, Poll
└── BlockDevice<Derived>         // ReadBlocks, WriteBlocks, ReadBlock, WriteBlock, Flush, GetCapacity
```

- 所有公影方法使用 Deducing this（`this Derived& self`），非传统 CRTP `static_cast`
- 基类提供 Open/Release/Read/Write/Mmap/Ioctl/HandleInterrupt + 线程安全 opened_ 状态
- 派生类只覆写 `DoXxx` 方法
- 未实现的操作默认返回 `kDeviceNotSupported`
- 额外类型：`OpenFlags`, `ProtFlags`, `MapFlags`, `PollEvents`

### 3. 命名空间

```
device_framework                     # 框架公共类型（ErrorCode, Error, Expected, Traits, Ops）
device_framework::detail             # 内部实现细节（用户不应直接使用）
device_framework::detail::virtio     # VirtIO 族内部实现
device_framework::detail::virtio::blk  # VirtIO 块设备内部实现
device_framework::detail::ns16550a   # NS16550A 内部实现
device_framework::detail::pl011      # PL011 内部实现
device_framework::detail::acpi       # ACPI 内部实现

# 以下为公开命名空间（通过 using namespace 从 detail 重导出）
device_framework::virtio             # VirtIO 族（含 VirtioTraits, NullVirtioTraits）
device_framework::virtio::blk        # VirtIO 块设备（VirtioBlkDevice）
device_framework::ns16550a           # NS16550A 驱动（Ns16550aDevice）
device_framework::pl011              # PL011 驱动（Pl011Device）
device_framework::acpi               # ACPI 驱动（Acpi）
```

## 编码规范

### 注释风格（SimpleKernel 标准）

```cpp
/**
 * @copyright Copyright The device_framework Contributors
 */

/**
 * @brief 简短描述
 *
 * 详细描述（可选）
 *
 * @param  name  参数说明
 * @return 返回值说明
 * @see    引用文档
 * @pre    前置条件
 * @post   后置条件
 */
auto Foo(int name) -> ReturnType;

/// 单行简短注释
auto Bar() -> void;

/// @name 分组名称
/// @{
/* 分组成员 */
/// @}
```

### 命名约定

- 类型：`PascalCase`（`MmioTransport`、`CharDevice`）
- 方法：`PascalCase`（`GetChar`、`ReadBlocks`）
- 成员变量：`snake_case_`（`base_addr_`、`opened_`）
- 常量/枚举值：`kPascalCase`（`kSuccess`、`kDeviceError`）
- 命名空间：`snake_case`（`device_framework`、`virtio`）
- 文件名：`snake_case`（`device_ops_base.hpp`）

### 返回类型

```cpp
// 总是使用 trailing return type
auto Foo() -> ReturnType;
auto Bar() const -> bool;

// 使用 Expected 错误处理
auto Open(OpenFlags flags) -> Expected<void>;
auto Read(std::span<uint8_t> buf) -> Expected<size_t>;
```

### 头文件保护

```cpp
#ifndef DEVICE_FRAMEWORK_OPS_CHAR_DEVICE_HPP_
#define DEVICE_FRAMEWORK_OPS_CHAR_DEVICE_HPP_
// ...
#endif /* DEVICE_FRAMEWORK_OPS_CHAR_DEVICE_HPP_ */
```

路径规则：`DEVICE_FRAMEWORK_[PATH]_[FILENAME]_HPP_`

## 添加新设备驱动的步骤

1. **确定 Traits 需求**：分析驱动需要哪些平台能力
   - 仅日志 → `EnvironmentTraits`
   - 需要 DMA → `EnvironmentTraits && DmaTraits`
   - 需要屏障 → 追加 `BarrierTraits`

2. **创建驱动目录**：`include/device_framework/detail/<name>/`

3. **实现底层驱动**：`detail/<name>/<name>.hpp`
   - 直接与硬件交互（volatile MMIO）
   - 放入 `device_framework::detail::<name>` 命名空间

4. **实现 Device 适配器**：`detail/<name>/<name>_device.hpp`
   - 继承 `CharDevice` 或 `BlockDevice`
   - 覆写 `DoOpen`、`DoCharRead`/`DoCharWrite` 或 `DoReadBlocks`/`DoWriteBlocks` 等
   - 声明 CRTP 基类为 friend

5. **创建公开入口头文件**：`include/device_framework/<name>.hpp`
   - `#include "device_framework/detail/<name>/<name>_device.hpp"`
   - 使用 `using namespace detail::<name>;` 重导出到公开命名空间
   - 用户只通过此文件访问驱动

6. **更新文档**：README.md 目录结构和特性列表

## 构建与测试

```bash
# 使用 CMake Presets（推荐）
cmake --preset build
cmake --build build

# 或手动指定工具链
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv64-toolchain.cmake
cmake --build build

# QEMU 测试
cmake --build build --target test_run

# GDB 调试
cmake --build build --target test_debug
```

## 常见问题

1. **`VirtioEnvironmentTraits` 找不到？** → 已重命名为 `VirtioTraits`，实际定义在 `device_framework::detail::virtio`，通过公开头文件重导出到 `device_framework::virtio`
2. **`virtio_driver::` 命名空间？** → 已迁移至 `device_framework::virtio::` （内部为 `device_framework::detail::virtio::`）
3. **include 路径？** → 用户应使用顶层公开头文件（`device_framework/ns16550a.hpp` 等），实现细节在 `device_framework/detail/`
4. **NullTraits 位置？** → `device_framework::NullTraits`（框架级），VirtIO 可用 `NullVirtioTraits`（`device_framework::detail::virtio` 中的别名，重导出到 `device_framework::virtio`）
5. **工具链文件位置？** → `cmake/riscv64-toolchain.cmake`（不在 test/ 中）
6. **ACPI 状态？** → 当前仅包含 ACPI 表结构定义（RSDP, RSDT, XSDT, FADT, DSDT），解析功能尚未实现
7. **PCI Transport？** → `PciTransport` 仅为占位实现，所有方法返回默认值
