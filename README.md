# device_framework

> **Header-only, Freestanding C++23 Device Framework**
>
> 统一的设备驱动框架，通过组合式 Traits 和 Deducing this 实现零开销的设备抽象。VirtIO 块设备、UART、ACPI 等驱动均可开箱即用。

## ✨ 特性

- **Header-only** — 纯 `.hpp`，无需编译静态/动态库
- **Freestanding** — 不依赖 OS，bare-metal / OS kernel 均可使用
- **C++23** — 利用 Deducing this（P0847）、concepts、`std::expected` 等实现零开销抽象
- **组合式 Traits** — 正交能力概念（Logging、Barrier、DMA），按需组合
- **统一 Ops 层** — `CharDevice` / `BlockDevice` 提供一致的 Open/Read/Write/Release 接口，支持 Mmap、Ioctl、HandleInterrupt
- **多驱动族** — VirtIO（MMIO）、NS16550A、PL011、ACPI

## 📁 目录结构

```
include/device_framework/
├── defs.h                               # DeviceType 枚举
├── expected.hpp                         # ErrorCode, Error, Expected<T>
├── traits.hpp                           # EnvironmentTraits, BarrierTraits, DmaTraits, NullTraits
│
├── ops/                                 # 设备操作抽象层（公开）
│   ├── device_ops_base.hpp              # DeviceOperationsBase<Derived>
│   ├── char_device.hpp                  # CharDevice<Derived>
│   └── block_device.hpp                 # BlockDevice<Derived>
│
├── ns16550a.hpp                         # ★ NS16550A 公开入口
├── pl011.hpp                            # ★ PL011 公开入口
├── virtio_blk.hpp                       # ★ VirtIO 块设备公开入口
├── acpi.hpp                             # ★ ACPI 公开入口
│
└── detail/                              # 实现细节（用户不应直接包含）
    ├── uart_device.hpp                  # UartDevice<Derived, DriverType> 通用 UART 适配层
    ├── ns16550a/                        # NS16550A UART
    │   ├── ns16550a.hpp                 # 底层驱动
    │   └── ns16550a_device.hpp          # CharDevice 适配器
    ├── pl011/                           # PL011 UART
    │   ├── pl011.hpp                    # 底层驱动
    │   └── pl011_device.hpp             # CharDevice 适配器
    ├── virtio/                          # VirtIO 驱动族
    │   ├── traits.hpp                   # VirtioTraits = Env + Barrier + DMA
    │   ├── defs.h                       # DeviceId, ReservedFeature
    │   ├── transport/                   # 传输层
    │   │   ├── transport.hpp            # Transport<Traits> 基类
    │   │   ├── mmio.hpp                 # MmioTransport（完整实现）
    │   │   └── pci.hpp                  # PciTransport（占位）
    │   ├── virt_queue/                  # 虚拟队列
    │   │   ├── virtqueue_base.hpp       # VirtqueueBase<Traits> 基类
    │   │   ├── split.hpp               # SplitVirtqueue（完整实现）
    │   │   └── misc.hpp                # 工具函数（AlignUp, IoVec 等）
    │   └── device/                      # 设备实现
    │       ├── device_initializer.hpp   # DeviceInitializer 初始化流程编排
    │       ├── virtio_blk_defs.h        # 块设备数据结构定义
    │       ├── virtio_blk.hpp           # 块设备驱动
    │       ├── virtio_blk_device.hpp    # BlockDevice 适配器
    │       ├── virtio_console.h         # Console 设备（占位）
    │       ├── virtio_gpu.h             # GPU 设备（占位）
    │       ├── virtio_input.h           # Input 设备（占位）
    │       └── virtio_net.h             # Net 设备（占位）
    └── acpi/                            # ACPI 表结构定义
        └── acpi.hpp

cmake/
└── riscv64-toolchain.cmake              # RISC-V 交叉编译工具链

test/                                    # QEMU RISC-V 集成测试
```

## 🏗️ 架构

### 三层架构

```mermaid
graph TB
    A["Traits 层<br>EnvironmentTraits · BarrierTraits · DmaTraits"] --> B
    B["Ops 层<br>DeviceOperationsBase · CharDevice · BlockDevice"] --> C
    C["Driver 层<br>VirtIO · NS16550A · PL011 · ACPI"]
```

### 组合式 Traits

不同驱动按需组合平台能力：

| 驱动族 | Traits 约束 | 要求 |
|--------|-----------|------|
| NS16550A / PL011 | `EnvironmentTraits` | 仅日志 |
| VirtIO | `VirtioTraits` | Log + Barrier + DMA |
| ACPI | 无 Traits 约束 | 仅构造时传入 RSDP 地址 |
| 未来 USB/NVMe | 自定义组合 | Log + DMA（或更多） |

```cpp
// 实现平台 Traits
struct MyTraits {
  static auto Log(const char* fmt, ...) -> int { /* ... */ }
  static auto Mb() -> void { asm volatile("fence" ::: "memory"); }
  static auto Rmb() -> void { asm volatile("fence ir,ir" ::: "memory"); }
  static auto Wmb() -> void { asm volatile("fence ow,ow" ::: "memory"); }
  static auto VirtToPhys(void* p) -> uintptr_t { return (uintptr_t)p; }
  static auto PhysToVirt(uintptr_t a) -> void* { return (void*)a; }
};

// MyTraits 同时满足 EnvironmentTraits、BarrierTraits、DmaTraits
// 可用于 VirtIO 驱动（VirtioTraits 约束）
// 也可用于 NS16550A（只要求 EnvironmentTraits）
```

## 🚀 快速开始

### 作为子模块

```bash
git submodule add https://github.com/MRNIU/device_framework.git
```

### CMake 集成

```cmake
add_subdirectory(device_framework)
target_link_libraries(your_target PRIVATE device_framework)
```

### 使用 NS16550A 字符设备

```cpp
#include "device_framework/ns16550a.hpp"

device_framework::ns16550a::Ns16550aDevice uart(0x10000000);
uart.OpenReadWrite();
uart.PutChar('H');
uart.PutChar('i');
uart.Release();
```

### 使用 VirtIO 块设备

```cpp
#include "device_framework/virtio_blk.hpp"

using BlkDev = device_framework::virtio::blk::VirtioBlkDevice<MyTraits>;

// 计算并分配 DMA 缓冲区
auto dma_size = BlkDev::CalcDmaSize(/* queue_size = */ 128);
void* dma_buf = /* 分配 dma_size 字节的 DMA 缓冲区 */;

// 创建设备实例
auto result = BlkDev::Create(mmio_base, dma_buf);
if (result) {
  auto& blk = *result;
  blk.OpenReadWrite();
  blk.ReadBlock(0, buffer);
  blk.Release();
}
```

## 🔨 构建与测试

```bash
# 使用 CMake Presets（推荐）
cmake --preset build
cmake --build build

# 或手动指定工具链
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv64-toolchain.cmake
cmake --build build

# QEMU 中运行测试
cmake --build build --target test_run

# GDB 调试模式
cmake --build build --target test_debug
```

## 📜 许可证

MIT License — 详见 [LICENSE](LICENSE)
