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
└── driver/
    ├── virtio/           # VirtIO 族
    │   ├── traits.hpp    # VirtioTraits concept
    │   ├── defs.h
    │   ├── transport/    # MMIO, PCI
    │   ├── virt_queue/   # Split virtqueue
    │   └── device/       # blk, net(占位), gpu(占位), etc.
    ├── ns16550a/         # UART
    ├── pl011/            # UART
    └── acpi/             # ACPI 表解析

test/                     # QEMU RISC-V 集成测试
```

## 核心架构模式

### 1. 组合式 Traits（正交能力概念）

```cpp
// 基础：仅需日志
template <typename T>
concept EnvironmentTraits = requires { T::Log(...); };

// 内存屏障
template <typename T>
concept BarrierTraits = requires { T::Mb(); T::Rmb(); T::Wmb(); };

// DMA 地址转换
template <typename T>
concept DmaTraits = requires(void* p, uintptr_t a) {
  T::VirtToPhys(p); T::PhysToVirt(a);
};

// VirtIO 组合约束
template <typename T>
concept VirtioTraits = EnvironmentTraits<T> && BarrierTraits<T> && DmaTraits<T>;
```

- NS16550A / PL011 仅需 `EnvironmentTraits`（无 DMA）
- VirtIO 需要 `VirtioTraits`（全部三个）
- 自定义驱动可按需组合

### 2. Ops 层（CRTP + Deducing this）

```
DeviceOperationsBase<Derived>
├── CharDevice<Derived>    // PutChar, GetChar, Poll
└── BlockDevice<Derived>   // ReadBlocks, WriteBlocks, Flush
```

- 基类提供 Open/Release/Read/Write + 线程安全 opened_ 状态
- 派生类只覆写 `DoXxx` 方法
- 未实现的操作默认返回 `kDeviceNotSupported`

### 3. 命名空间

```
device_framework                     # 框架公共类型
device_framework::virtio             # VirtIO 族
device_framework::virtio::blk        # VirtIO 块设备
device_framework::ns16550a           # NS16550A 驱动
device_framework::pl011              # PL011 驱动
device_framework::acpi               # ACPI 驱动
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

2. **创建驱动目录**：`include/device_framework/driver/<name>/`

3. **实现底层驱动**：`<name>.hpp`
   - 直接与硬件交互（volatile MMIO）
   - 放入 `device_framework::<name>` 命名空间

4. **实现 Device 适配器**：`<name>_device.hpp`
   - 继承 `CharDevice` 或 `BlockDevice`
   - 覆写 `DoOpen`、`DoCharRead`/`DoCharWrite` 或 `DoReadBlocks`/`DoWriteBlocks` 等
   - 声明 CRTP 基类为 friend

5. **更新文档**：README.md 目录结构和特性列表

## 构建与测试

```bash
# 配置（RISC-V 交叉编译）
cmake -B build -DCMAKE_TOOLCHAIN_FILE=test/riscv64-toolchain.cmake

# 编译
cmake --build build

# QEMU 测试
cmake --build build --target test_run

# GDB 调试
cmake --build build --target test_debug
```

## 常见问题

1. **`VirtioEnvironmentTraits` 找不到？** → 已重命名为 `VirtioTraits`，位于 `device_framework::virtio`
2. **`virtio_driver::` 命名空间？** → 已迁移至 `device_framework::virtio::`
3. **include 路径？** → `virtio_driver/xxx` → `device_framework/driver/virtio/xxx`
4. **NullTraits 位置？** → `device_framework::NullTraits`（框架级），VirtIO 可用 `NullVirtioTraits`（别名）
