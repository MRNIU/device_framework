# virtio_driver Copilot Instructions

## 项目概述

一个 **header-only、跨平台** 的 VirtIO 设备驱动库，面向裸机/OS 内核等 freestanding 环境。实现了 VirtIO 1.2 规范中的传输层（MMIO）、Split Virtqueue 和块设备驱动。库本身不依赖任何特定架构，通过 `PlatformOps` 函数指针抽象平台差异。当前使用 RISC-V 64 位 + QEMU `virt` 机器作为测试环境。项目处于活跃开发阶段，Console/GPU/Net/Input 设备和 PCI 传输层尚为占位文件。

## 技术栈

| 组件 | 技术选型 |
|------|---------|
| 语言标准 | C23 / C++23（`-std=c++2b`，`-ffreestanding`） |
| 构建系统 | CMake 3.27+，使用 CMakePresets（preset 名: `build`） |
| 测试编译器 | `riscv64-linux-gnu-gcc` / `g++` 交叉工具链（仅测试用） |
| 测试模拟器 | `qemu-system-riscv64`（virt 机器，128M 内存，仅测试用） |
| 代码风格 | Google Style（`.clang-format` / `.clang-tidy`） |
| 测试框架 | 自定义裸机测试框架（`test/test.h` 中的 `EXPECT_TRUE` / `EXPECT_EQ` / `EXPECT_NE` 宏） |
| Pre-commit | clang-format、clang-tidy、cmake-format、cmake-lint、shellcheck |

## 项目结构

```
include/                        # 公共头文件（header-only 库）
├── defs.h                      # DeviceId、ReservedFeature 枚举、Logger 模板
├── expected.hpp                # ErrorCode、Error、Expected<T> (std::expected 别名)
├── platform.h                  # PlatformOps 结构（内存分配/屏障/地址转换的函数指针）
├── transport/
│   ├── transport.hpp           # Transport<LogFunc> 抽象基类（纯虚接口）
│   ├── mmio.hpp                # MmioTransport<LogFunc>（Modern v2 only）
│   └── pci.hpp                 # PciTransport（占位，@todo）
├── device/
│   ├── device_initializer.hpp  # DeviceInitializer<LogFunc>（标准初始化序列编排）
│   ├── virtio_blk.hpp          # VirtioBlk<LogFunc>（块设备驱动，读/写/刷新）
│   └── virtio_console.h / virtio_gpu.h / virtio_net.h / virtio_input.h  # 占位
└── virt_queue/
    ├── misc.hpp                # AlignUp()、IsPowerOfTwo() 工具函数
    └── split.hpp               # SplitVirtqueue（描述符管理/提交/回收）

test/                           # 裸机 QEMU 测试环境
├── boot.S                      # 启动汇编（S-mode，设置栈/中断/跳转 _start）
├── link.ld                     # 链接脚本（入口 0x80200000）
├── main.cpp                    # 测试入口（_start → test_main）
├── test.h / test.cpp           # 自定义测试框架（EXPECT_* 宏 + 统计）
├── uart.h / uart.cpp           # UART 驱动（串口输出）
├── plic.h                      # PLIC 中断控制器
├── trap.cpp                    # 中断处理入口
├── mmio_test.cpp               # MMIO 传输层测试
├── virtio_blk_test.cpp         # 块设备读写测试
└── CMakeLists.txt              # 测试构建（含 QEMU 启动目标）

cmake/riscv64-toolchain.cmake   # 交叉编译工具链文件
CMakePresets.json               # CMake 预设（preset: "build"）
```

## 构建和运行命令

```bash
# 先决条件：安装 riscv64-linux-gnu-gcc 和 qemu-system-riscv64

# 配置（仅首次或修改 CMakeLists.txt 后）
cmake --preset build

# 编译
cd build && make test

# 在 QEMU 中运行测试（会阻塞在 wfi 循环，用 Ctrl-A X 退出）
make test_run

# GDB 调试模式（监听 localhost:1234）
make test_debug
```

注意：`make test` 编译测试二进制，`make test_run` / `make test_debug` 是 CUSTOM_TARGET。构建目录固定为项目根下的 `build/`。

## 核心架构模式

### 模板化 LogFunc 参数
所有核心类使用 `template <class LogFunc = std::nullptr_t>` 参数实现可选日志。实现自定义 Logger 时需定义 `operator()(const char* format, ...) const -> int`。

### 设备初始化流程（必须遵循）
```
Transport 构造 → DeviceInitializer::Init(features) → SetupQueue() → Activate()
```
对应 virtio-v1.2§3.1.1 的步骤 1-8。参照 `virtio_blk_test.cpp` 以及 `VirtioBlk::Create()` 中的实现。

### 错误处理
使用 `Expected<T>` = `std::expected<T, Error>`，错误码定义在 `expected.hpp` 的 `ErrorCode` 枚举中。

### 平台抽象
用户必须实现 `PlatformOps` 结构中的函数指针（`alloc_pages` / `free_pages` / `virt_to_phys` / `mb` / `rmb` / `wmb` / `page_size`）。测试环境使用恒等映射 + RISC-V fence 指令。

## 编码规范

### 编译约束（最重要）
- **禁止动态内存分配**：不可使用 `new` / `delete` / `malloc` / `free` / STL 容器
- **禁用标准库**：编译选项 `-nostdlib -fno-builtin -fno-rtti -fno-exceptions`
- **Freestanding 环境**：仅可使用 freestanding 头文件（`<cstdint>` `<cstddef>` `<type_traits>` `<expected>` `<array>` 等），参考 https://en.cppreference.com/w/cpp/freestanding.html
- **所有结构体需 `__attribute__((packed))`**（与硬件/DMA 共享的结构）

### 代码风格
- **格式化**：Google Style，由 `.clang-format` 强制执行
- **静态检查**：`.clang-tidy` 配置（启用 bugprone/google/misc/modernize/performance/readability 检查）
- **CMake 格式**：由 `.cmake-format.json` 配置

### 命名约定
| 类别 | 风格 | 示例 |
|------|------|------|
| 文件 | snake_case | `virtio_blk.hpp`、`mmio_test.cpp` |
| 类/结构体 | PascalCase | `MmioTransport`、`SplitVirtqueue` |
| 函数/方法 | PascalCase | `GetDeviceId()`、`SetQueueReady()` |
| 变量 | snake_case | `queue_size_`（成员加 `_` 后缀） |
| 常量 | kCamelCase | `kMmioMagicValue`、`kSectorSize` |
| 枚举值 | kCamelCase | `kBlock`、`kVersion1` |
| 宏 | SCREAMING_SNAKE | `EXPECT_TRUE`、`EXPECT_EQ` |
| 命名空间 | snake_case | `virtio_driver`、`virtio_driver::blk` |

### 头文件规范
- 接口头文件使用 Doxygen 注释：`@brief`、`@param`、`@return`、`@pre`、`@post`、`@see virtio-v1.2#章节号`
- Header-only 模板实现直接放在 `.hpp` 中
- `#ifndef` 守卫格式：`VIRTIO_DRIVER_SRC_INCLUDE_<PATH>_<NAME>_HPP_`

### 返回值风格
- 使用 trailing return type：`[[nodiscard]] auto Foo() -> RetType`
- 可失败操作返回 `Expected<T>`

### Git Commit 规范
```
<type>(<scope>): <subject>

type: feat | fix | docs | style | refactor | perf | test | build | revert
scope: transport, device, virt_queue, test, build（可选）
subject: ≤50 字符，不加句号
```

## 添加新设备驱动的步骤

1. 在 `include/device/` 中创建 `virtio_<name>.hpp`
2. 参照 `virtio_blk.hpp` 的模式：定义特性枚举、配置结构、请求结构
3. 创建 `class Virtio<Name><LogFunc>` 模板类，持有 `Transport<LogFunc>&`、`SplitVirtqueue&`、`PlatformOps&`
4. 提供 `static Create()` 工厂方法（内部使用 `DeviceInitializer`）
5. 在 `test/` 中创建 `virtio_<name>_test.cpp`，注册到 `test/CMakeLists.txt` 的 `ADD_EXECUTABLE` 列表
6. 在 `test/main.cpp` 中调用测试函数
7. 在 `test/test.h` 中声明测试函数原型

## 添加测试的步骤

1. 创建 `test/<name>_test.cpp`
2. 使用 `test/test.h` 中的宏：`EXPECT_TRUE(cond, msg)`、`EXPECT_EQ(expected, actual, msg)`、`EXPECT_NE`、`LOG`、`LOG_HEX`
3. 在函数开头调用 `test_framework_init()`，结尾调用 `test_framework_print_summary()`
4. 将源文件加入 `test/CMakeLists.txt` 的 `ADD_EXECUTABLE` 列表
5. 在 `test/test.h` 中前向声明，在 `test/main.cpp` 中调用

## QEMU 测试环境要点

- MMIO 设备地址范围：`0x10001000` ~ `0x10008000`（间隔 0x1000，最多 8 个设备）
- QEMU 启动时挂载设备：virtio-blk-device、virtio-net-device、virtio-gpu-device、virtio-keyboard-device、virtio-mouse-device
- 测试磁盘镜像：`build/images/test.img`（64MB 零填充 raw），构建时自动创建
- UART 输出地址：`0x10000000`（ns16550a）
- PLIC 基地址：`0x0c000000`
- S-mode 运行，入口地址 `0x80200000`

## 常见陷阱

- QEMU virt 机器的 MMIO 设备默认为 Legacy (v1)，需要添加 `-global virtio-mmio.force-legacy=false` 强制使用 Modern (v2) 模式。`test_run` 和 `test_debug` 目标已配置此选项
- 本项目仅支持 Modern VirtIO (v2, virtio 1.0+)，不支持 Legacy 设备。`VirtioBlk::Create()` 会强制要求协商 `VIRTIO_F_VERSION_1`
- `volatile` 指针用于所有 MMIO 寄存器访问和 DMA 共享内存
- 裸机无 `printf`，调试输出使用 `uart_puts()` / `uart_put_hex()`
- `make test_run` 会阻塞终端（QEMU 前台运行），用 `timeout 5 make test_run || true` 做 CI 自动化测试
- 链接时必须使用 `-mno-relax` 禁用 RISC-V linker relaxation
