# virtio_driver

一个 **header-only、跨平台** 的 VirtIO 设备驱动库，面向裸机/OS 内核等 freestanding 环境。

实现了 VirtIO 1.2 规范中的：

- **传输层**：MMIO（Modern v2 only）
- **Virtqueue**：Split Virtqueue（描述符管理、提交、回收）
- **设备驱动**：Block Device（读/写/刷新）

库本身不依赖任何特定架构，通过 `PlatformOps` 函数指针抽象平台差异。

> **状态**：项目处于活跃开发阶段。PCI 传输层、Console/GPU/Net/Input 设备驱动尚为占位文件。

## 特性

- **Header-only**：只需 `#include` 即可使用，无需编译链接
- **零动态分配**：不使用 `new` / `delete` / `malloc` / `free`，适合裸机和内核环境
- **Freestanding C++23**：仅依赖 freestanding 头文件（`<cstdint>` `<expected>` `<type_traits>` 等）
- **可选日志**：通过模板参数 `LogFunc` 注入日志函数，默认为 `std::nullptr_t`（无日志开销）
- **错误处理**：基于 `std::expected<T, Error>` 的类型安全错误传播
- **VirtIO 1.2 规范兼容**：仅支持 Modern VirtIO (v2, virtio 1.0+)

## 项目结构

```
include/virtio_driver/              # 公共头文件（header-only 库）
├── defs.h                           # DeviceId、ReservedFeature 枚举、Logger 模板
├── expected.hpp                     # ErrorCode、Error、Expected<T>
├── platform.h                       # PlatformOps（平台抽象函数指针）
├── transport/
│   ├── transport.hpp                # Transport<LogFunc> 抽象基类
│   ├── mmio.hpp                     # MmioTransport<LogFunc>（MMIO 传输层）
│   └── pci.hpp                      # PciTransport（占位）
├── device/
│   ├── device_initializer.hpp       # DeviceInitializer<LogFunc>（初始化序列编排）
│   ├── virtio_blk.hpp              # VirtioBlk<LogFunc>（块设备驱动）
│   └── virtio_console.h / ...      # 占位
└── virt_queue/
    ├── misc.hpp                     # AlignUp()、IsPowerOfTwo() 工具函数
    └── split.hpp                    # SplitVirtqueue（Split Virtqueue 实现）
```

## 快速开始

### 依赖

本库为 header-only，无需编译。将 `include/` 目录加入你的 include path 即可。

编译环境要求：

- 支持 C++23 的编译器（GCC 13+ / Clang 17+）
- 编译选项：`-std=c++2b -ffreestanding -nostdlib -fno-builtin -fno-rtti -fno-exceptions`

### 集成到你的项目

#### CMake（推荐）

```cmake
# 方式 1：作为子目录
add_subdirectory(path/to/virtio_driver)
target_link_libraries(your_target PRIVATE virtio_driver)

# 方式 2：仅添加头文件路径
target_include_directories(your_target PRIVATE path/to/virtio_driver/include)
```

#### 手动

将 `include/` 目录拷贝到你的项目中，并添加到编译器 include path：

```bash
-I path/to/virtio_driver/include
```

### 使用示例

以块设备（VirtIO Block Device）为例，展示完整的初始化和读写流程：

#### 1. 实现平台抽象接口

```cpp
#include "virtio_driver/platform.h"

// 裸机环境下的平台操作（以恒等映射为例）
virtio_driver::PlatformOps platform_ops = {
    .virt_to_phys = [](void* vaddr) -> uint64_t {
        return reinterpret_cast<uint64_t>(vaddr);
    }
};
```

#### 2. 预分配 DMA 内存

```cpp
#include "virtio_driver/device/virtio_blk.hpp"

using namespace virtio_driver::blk;

// 计算所需的 DMA 缓冲区大小
constexpr size_t kDmaSize = VirtioBlk<>::CalcDmaSize();

// 预分配 DMA 内存（页对齐，清零）
alignas(4096) static uint8_t dma_buf[kDmaSize];
memset(dma_buf, 0, sizeof(dma_buf));
```

#### 3. 创建块设备并读写

```cpp
// MMIO 设备基地址（例如 QEMU virt 机器的第一个 VirtIO 设备）
constexpr uint64_t kMmioBase = 0x10001000;

// 创建块设备（内部自动完成传输层初始化、Virtqueue 配置、特性协商、设备激活）
auto blk_result = VirtioBlk<>::Create(kMmioBase, dma_buf, platform_ops);
if (!blk_result.has_value()) {
    // 初始化失败，可通过 blk_result.error().message() 获取错误描述
    return;
}
auto& blk = *blk_result;

// 读取设备配置
uint64_t capacity = blk.GetCapacity();  // 设备容量（512B 扇区数）
auto config = blk.ReadConfig();         // 完整配置信息

// 数据缓冲区必须位于 DMA 可访问的内存中
alignas(16) static uint8_t data_buf[kSectorSize];

// 写入扇区 0（同步，内部自动处理请求构建、轮询、完成确认）
for (size_t i = 0; i < kSectorSize; ++i) {
    data_buf[i] = static_cast<uint8_t>(i & 0xFF);
}
auto write_result = blk.Write(0, data_buf);
if (!write_result.has_value()) {
    // 写入失败
}

// 读取扇区 0（同步）
memset(data_buf, 0, sizeof(data_buf));
auto read_result = blk.Read(0, data_buf);
if (read_result.has_value()) {
    // data_buf 中即为读取到的数据
}
```

### 可选：启用日志

通过模板参数注入自定义日志函数：

```cpp
struct MyLogger {
    auto operator()(const char* format, ...) const -> int {
        // 你的日志输出实现（如 UART 串口输出）
        va_list ap;
        va_start(ap, format);
        int ret = vprintf(format, ap);  // 替换为你的输出函数
        va_end(ap);
        return ret;
    }
};

// 模板参数注入日志
auto blk_result = virtio_driver::blk::VirtioBlk<MyLogger>::Create(
    mmio_base, dma_buf, platform_ops);
```

不传 `LogFunc` 模板参数时默认为 `std::nullptr_t`，编译器会优化掉所有日志调用（零开销）。

## 核心 API

### `PlatformOps`

平台抽象接口，使用前必须实现所有函数指针：

| 函数指针 | 签名 | 说明 |
|---------|------|------|
| `virt_to_phys` | `uint64_t (*)(void* vaddr)` | 虚拟地址转物理地址 |

### `MmioTransport<LogFunc>`

MMIO 传输层，继承自 `Transport<LogFunc>`：

| 方法 | 说明 |
|------|------|
| `MmioTransport(uint64_t base)` | 构造并验证 MMIO 设备（魔数、版本、设备 ID） |
| `IsValid()` | 检查初始化是否成功 |
| `GetDeviceId()` | 获取设备类型 ID |
| `GetDeviceFeatures()` | 读取设备支持的 64 位特性位 |
| `GetInterruptStatus()` | 读取中断状态 |
| `AckInterrupt(status)` | 确认中断 |
| `NotifyQueue(idx)` | 通知设备处理指定队列 |

### `SplitVirtqueue`

Split Virtqueue 管理（描述符分配/提交/回收）：

| 方法 | 说明 |
|------|------|
| `SplitVirtqueue(buf, phys, size, ...)` | 从预分配的 DMA 缓冲区构造 |
| `CalcSize(queue_size, ...)` | 计算所需的 DMA 内存大小（静态） |
| `AllocDesc()` | 分配一个空闲描述符 |
| `FreeDesc(idx)` | 释放描述符 |
| `Submit(head)` | 将描述符链提交到 Available Ring |
| `HasUsed()` | 检查是否有已完成的请求 |
| `PopUsed()` | 取出一个已完成的请求 |

### `VirtioBlk<LogFunc>`

块设备驱动：

| 方法 | 说明 |
|------|------|
| `Create(mmio_base, dma_buf, platform, queue_size, features)` | 静态工厂方法，内部创建 Transport/Virtqueue 并完成完整初始化 |
| `Read(sector, data)` | 同步读请求（内部完成轮询与状态检查） |
| `Write(sector, data)` | 同步写请求（内部完成轮询与状态检查） |
| `ReadConfig()` | 读设备配置空间 |
| `GetCapacity()` | 获取设备容量（扇区数） |
| `AckInterrupt()` | 确认设备中断（供中断处理程序调用） |
| `CalcDmaSize(queue_size)` | 静态方法，计算 Virtqueue 所需 DMA 缓冲区大小 |

### 错误处理

所有可失败操作返回 `Expected<T>`（即 `std::expected<T, Error>`）：

```cpp
auto result = blk.Read(0, data_buf);
if (!result.has_value()) {
    ErrorCode code = result.error().code;
    const char* msg = result.error().message();
    // 处理错误...
}
```

错误码定义在 `ErrorCode` 枚举中，包括：`kInvalidMagic`、`kFeatureNegotiationFailed`、`kNoFreeDescriptors`、`kIoError`、`kTimeout` 等。

## 设备初始化流程

遵循 virtio-v1.2 §3.1.1 定义的标准初始化序列：

```
1. Reset           → 写 0 到 status 寄存器
2. ACKNOWLEDGE      → 识别为 virtio 设备
3. DRIVER           → 驱动程序知道如何驱动
4. 特性协商         → 读设备特性 & 驱动特性 → 写回交集
5. FEATURES_OK      → 设置并验证
6. 配置 Virtqueue   → 设置队列地址和大小
7. DRIVER_OK        → 设备激活，开始运行
```

使用 `VirtioBlk::Create()` 会自动完成上述全部步骤。如需手动控制，可直接使用 `DeviceInitializer`：

```cpp
virtio_driver::DeviceInitializer<> initializer(transport);

auto features = initializer.Init(wanted_features);  // 步骤 1-5
initializer.SetupQueue(0, desc_phys, avail_phys, used_phys, queue_size);  // 步骤 6
initializer.Activate();  // 步骤 7
```

## 构建和运行测试

测试使用 RISC-V 64 位 + QEMU virt 机器作为裸机测试环境。

### 先决条件

```bash
# Ubuntu / Debian
sudo apt install gcc-riscv64-linux-gnu g++-riscv64-linux-gnu qemu-system-misc

# macOS (Homebrew)
brew install riscv64-elf-gcc qemu
```

### 构建

```bash
# 配置（仅首次或修改 CMakeLists.txt 后）
cmake --preset build

# 编译测试
cd build && make test
```

### 运行测试

```bash
# 在 QEMU 中运行（用 Ctrl-A X 退出）
make test_run

# 或限时运行（CI 场景）
timeout 5 make test_run || true
```

### GDB 调试

```bash
# 终端 1：启动 QEMU 调试模式（监听 localhost:1234）
make test_debug

# 终端 2：连接 GDB
riscv64-linux-gnu-gdb build/bin/test
(gdb) target remote :1234
(gdb) break test_main
(gdb) continue
```

## 注意事项

- **仅支持 Modern VirtIO (v2)**：不兼容 Legacy 设备。QEMU 需添加 `-global virtio-mmio.force-legacy=false`（测试目标已配置）
- **所有 DMA 缓冲区必须由调用者预分配**：库不进行任何动态内存分配
- **非线程安全**：`SplitVirtqueue` 的方法不是线程安全的，多核场景需外部加锁
- **volatile 访问**：所有 MMIO 寄存器和 DMA 共享内存使用 `volatile` 指针
- **内存屏障**：`VirtioBlk` 内部使用 `std::atomic_thread_fence` 保证描述符和 Available Ring 的可见性

## License

[MIT](LICENSE)
