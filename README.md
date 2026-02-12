# virtio_driver

一个 **header-only、跨平台** 的 VirtIO 设备驱动库，面向裸机/OS 内核等 freestanding 环境。

实现了 VirtIO 1.2 规范中的：

- **传输层**：MMIO（Modern v2 only），PCI（占位）
- **Virtqueue**：Split Virtqueue（Scatter-Gather 描述符链、Event Index 通知抑制）
- **设备驱动**：Block Device（同步/异步 IO、多队列预留）

采用 C++23 现代特性（Deducing `this`、Concepts）实现 **零开销抽象**——无虚表、无动态分配、无标准库依赖。通过 `VirtioEnvironmentTraits` concept 统一平台抽象（日志、内存屏障、地址转换）。

> **状态**：项目处于活跃开发阶段。PCI 传输层、Packed Virtqueue、Console/GPU/Net/Input 设备驱动尚为占位文件。

## 特性

- **Header-only**：只需 `#include` 即可使用，无需编译链接
- **零动态分配**：不使用 `new` / `delete` / `malloc` / `free`，适合裸机和内核环境
- **零虚表开销**：Transport 和 Virtqueue 层使用 C++23 Deducing `this` 实现编译期多态
- **Freestanding C++23**：仅依赖 freestanding 头文件（`<cstdint>` `<expected>` `<concepts>` 等）
- **统一平台抽象**：通过 `VirtioEnvironmentTraits` concept 注入日志、内存屏障、地址转换，默认 `NullTraits` 零开销
- **Scatter-Gather IO**：基于 `IoVec` 的描述符链自动组装，支持多缓冲区批量传输
- **异步 IO 模型**：`EnqueueRead`/`EnqueueWrite` + `Kick` + `HandleInterrupt` 回调模式，支持批量提交
- **Event Index**：支持 `VIRTIO_F_EVENT_IDX` 通知抑制，减少不必要的 Kick 和中断
- **错误处理**：基于 `std::expected<T, Error>` 的类型安全错误传播
- **性能统计**：内置 `VirtioStats`（传输字节数、省略的 Kick 次数、中断次数等）
- **VirtIO 1.2 规范兼容**：仅支持 Modern VirtIO (v2, virtio 1.0+)

## 项目结构

```
include/virtio_driver/              # 公共头文件（header-only 库）
├── defs.h                           # DeviceId、ReservedFeature 枚举
├── expected.hpp                     # ErrorCode、Error、Expected<T>
├── traits.hpp                       # VirtioEnvironmentTraits concept、NullTraits
├── transport/
│   ├── transport.hpp                # Transport<Traits> 基类（Deducing this，零虚表）
│   ├── mmio.hpp                     # MmioTransport<Traits>（MMIO 传输层）
│   └── pci.hpp                      # PciTransport<Traits>（占位）
├── device/
│   ├── device_initializer.hpp       # DeviceInitializer<Traits, TransportImpl>
│   ├── virtio_blk.hpp               # VirtioBlk<Traits, TransportT, VirtqueueT>
│   └── virtio_console.h / ...       # 占位
└── virt_queue/
    ├── misc.hpp                     # AlignUp()、IsPowerOfTwo()、IoVec
    ├── virtqueue_base.hpp           # VirtqueueBase<Traits>（Deducing this 基类）
    └── split.hpp                    # SplitVirtqueue<Traits>（Split Virtqueue 实现）
```

## 快速开始

### 依赖

本库为 header-only，无需编译。将 `include/` 目录加入你的 include path 即可。

编译环境要求：

- 支持 C++23 的编译器（GCC 14+ / Clang 18+，需要 Deducing `this` 支持）
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

#### 1. 实现平台 Traits

```cpp
#include "virtio_driver/traits.hpp"

// 裸机环境下的平台 Traits（以 RISC-V 为例）
struct MyTraits {
    static auto Log(const char* fmt, ...) -> int {
        // 你的日志输出实现（如 UART 串口输出）
        return 0;
    }
    static auto Mb()  -> void { asm volatile("fence iorw, iorw" ::: "memory"); }
    static auto Rmb() -> void { asm volatile("fence ir, ir"     ::: "memory"); }
    static auto Wmb() -> void { asm volatile("fence ow, ow"     ::: "memory"); }
    static auto VirtToPhys(void* p)     -> uintptr_t { return reinterpret_cast<uintptr_t>(p); }
    static auto PhysToVirt(uintptr_t a) -> void*     { return reinterpret_cast<void*>(a); }
};
```

#### 2. 预分配 DMA 内存

```cpp
#include "virtio_driver/device/virtio_blk.hpp"

using namespace virtio_driver::blk;

// 计算所需的 DMA 缓冲区大小
constexpr size_t kDmaSize = VirtioBlk<MyTraits>::CalcDmaSize();

// 预分配 DMA 内存（页对齐，清零）
alignas(4096) static uint8_t dma_buf[kDmaSize];
memset(dma_buf, 0, sizeof(dma_buf));
```

#### 3. 创建块设备并读写

```cpp
// MMIO 设备基地址（例如 QEMU virt 机器的第一个 VirtIO 设备）
constexpr uint64_t kMmioBase = 0x10001000;

// 创建块设备（内部自动完成传输层初始化、特性协商、Virtqueue 配置、设备激活）
auto blk_result = VirtioBlk<MyTraits>::Create(kMmioBase, dma_buf);
if (!blk_result.has_value()) {
    // 初始化失败，可通过 blk_result.error().message() 获取错误描述
    return;
}
auto& blk = *blk_result;

// 读取设备配置
uint64_t capacity = blk.GetCapacity();  // 设备容量（512B 扇区数）

// 数据缓冲区必须位于 DMA 可访问的内存中
alignas(16) static uint8_t data_buf[kSectorSize];

// 写入扇区 0（同步，内部基于异步接口实现）
for (size_t i = 0; i < kSectorSize; ++i) {
    data_buf[i] = static_cast<uint8_t>(i & 0xFF);
}
auto write_result = blk.Write(0, data_buf);

// 读取扇区 0（同步）
memset(data_buf, 0, sizeof(data_buf));
auto read_result = blk.Read(0, data_buf);
```

### 异步 IO 示例

```cpp
using namespace virtio_driver;
using namespace virtio_driver::blk;

alignas(16) static uint8_t buf[kSectorSize];
IoVec data_iov{MyTraits::VirtToPhys(buf), kSectorSize};

// 入队读请求（不触发硬件通知）
auto result = blk.EnqueueRead(0, /*sector=*/0, &data_iov, 1, /*token=*/nullptr);

// 批量通知设备（含 Event Index 抑制逻辑）
blk.Kick(0);

// 中断处理回调
blk.HandleInterrupt([](void* token, virtio_driver::ErrorCode status) {
    // 处理完成的请求
});
```

### 使用默认 NullTraits（零日志/零屏障）

不传 `Traits` 模板参数时默认为 `NullTraits`，所有日志调用和内存屏障在编译期消除（零开销）：

```cpp
auto blk_result = VirtioBlk<>::Create(kMmioBase, dma_buf);
```

## 核心 API

### `VirtioEnvironmentTraits` concept

平台环境特征约束，所有核心类通过 `Traits` 模板参数注入：

| 静态方法 | 签名 | 说明 |
|---------|------|------|
| `Log` | `int(const char* fmt, ...)` | 日志输出 |
| `Mb` | `void()` | 全内存屏障 |
| `Rmb` | `void()` | 读内存屏障 |
| `Wmb` | `void()` | 写内存屏障 |
| `VirtToPhys` | `uintptr_t(void*)` | 虚拟地址转物理地址 |
| `PhysToVirt` | `void*(uintptr_t)` | 物理地址转虚拟地址 |

### `MmioTransport<Traits>`

MMIO 传输层（Deducing `this` 编译期多态，无虚表）：

| 方法 | 说明 |
|------|------|
| `MmioTransport(uint64_t base)` | 构造并验证 MMIO 设备（魔数、版本、设备 ID） |
| `IsValid()` | 检查初始化是否成功 |
| `GetDeviceId()` | 获取设备类型 ID |
| `GetDeviceFeatures()` | 读取设备支持的 64 位特性位 |
| `GetInterruptStatus()` | 读取中断状态 |
| `AckInterrupt(status)` | 确认中断 |
| `NotifyQueue(idx)` | 通知设备处理指定队列 |

### `SplitVirtqueue<Traits>`

Split Virtqueue 管理（Scatter-Gather 描述符链、Event Index）：

| 方法 | 说明 |
|------|------|
| `SplitVirtqueue(buf, phys, size, event_idx)` | 从预分配的 DMA 缓冲区构造 |
| `CalcSize(queue_size, event_idx)` | 计算所需的 DMA 内存大小（静态） |
| `SubmitChain(readable, r_count, writable, w_count)` | 提交 Scatter-Gather 描述符链 |
| `AllocDesc()` | 分配一个空闲描述符 |
| `FreeDesc(idx)` / `FreeChain(head)` | 释放描述符/描述符链 |
| `Submit(head)` | 将描述符链提交到 Available Ring |
| `HasUsed()` | 检查是否有已完成的请求 |
| `PopUsed()` | 取出一个已完成的请求 |

### `VirtioBlk<Traits, TransportT, VirtqueueT>`

块设备驱动（泛化 Transport/Virtqueue 类型参数）：

| 方法 | 说明 |
|------|------|
| `Create(mmio_base, dma_buf, queue_count, queue_size, features)` | 静态工厂方法 |
| `GetRequiredVqMemSize(queue_count, queue_size)` | 计算多队列所需 DMA 内存大小 |
| `CalcDmaSize(queue_size)` | 计算单队列所需 DMA 内存大小 |
| `Read(sector, data)` | 同步读（基于异步接口实现） |
| `Write(sector, data)` | 同步写（基于异步接口实现） |
| `EnqueueRead(queue_index, sector, buffers, count, token)` | 异步提交读请求 |
| `EnqueueWrite(queue_index, sector, buffers, count, token)` | 异步提交写请求 |
| `Kick(queue_index)` | 通知设备（含 Event Index 抑制） |
| `HandleInterrupt(callback)` | 中断处理回调 |
| `GetCapacity()` | 获取设备容量（扇区数） |
| `ReadConfig()` | 读设备配置空间 |
| `GetStats()` | 获取性能统计数据 |

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
MmioTransport<MyTraits> transport(mmio_base);
DeviceInitializer<MyTraits, MmioTransport<MyTraits>> initializer(transport);

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
- **所有 DMA 缓冲区必须由调用者预分配**：库不进行任何动态内存分配。DMA 内存的 Non-cacheable 映射或 cache 刷新策略由平台层（调用方）负责
- **驱动层不持有锁**：同步责任由调用方承担。同一 Virtqueue 的 `Enqueue`/`Kick`/`HandleInterrupt` 不可被并发调用
- **volatile 访问**：所有 MMIO 寄存器和 DMA 共享内存使用 `volatile` 指针
- **内存屏障**：通过 `Traits::Mb()`/`Rmb()`/`Wmb()` 由平台层提供，库内部在关键路径自动调用

## License

[MIT](LICENSE)
