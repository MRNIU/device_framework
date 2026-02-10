# virtio_driver 测试环境

这是一个最小可运行的 RISC-V 64 裸机测试环境，用于测试和开发 virtio_driver。

## 功能特性

- ✅ RISC-V 64 裸机环境（S-mode，基于 OpenSBI）
- ✅ UART 串口输出/输入（支持中断模式）
- ✅ 支持 QEMU virt 机器
- ✅ Supervisor 模式中断支持
- ✅ PLIC (平台级中断控制器) 集成
- ✅ VirtIO 设备自动扫描和检测
- ✅ VirtIO 设备支持（块设备、网络、GPU、输入设备）
- ✅ 外部中断处理框架（UART + VirtIO）
- ✅ 基本的异常处理框架
- ✅ 无虚拟内存管理（物理地址直接访问）

## 文件结构

```
tests/
├── boot.S                  # 启动汇编代码（包含中断向量设置）
├── link.ld                 # 链接脚本
├── macro.S                 # 汇编宏定义
├── main.cpp                # 主程序（C++），中断系统初始化
├── uart.h                  # UART 驱动头文件
├── uart.cpp                # UART 驱动实现（支持输入中断）
├── trap.cpp                # 中断和异常处理
├── plic.h                  # PLIC (平台级中断控制器) 支持
├── virtio_mmio_probe.h     # VirtIO MMIO 设备探测
├── virtio_mmio_probe.cpp   # VirtIO 设备扫描实现
└── CMakeLists.txt          # CMake 构建配置（包含 VirtIO 设备配置）
```

## 编译和运行

### 前置要求

- CMake >= 3.27
- RISC-V 64 工具链 (riscv64-linux-gnu-gcc)
- QEMU RISC-V 64 (qemu-system-riscv64)

### 编译

```bash
# 在项目根目录
mkdir -p build
cd build
cmake ..
make
```

### 运行测试

```bash
# 在 build 目录中
make test_run
```

QEMU 启动参数包含以下 VirtIO 设备：
- **virtio-blk-device**: 块设备（使用 64MB 磁盘镜像）
- **virtio-net-device**: 网络设备（用户模式网络）
- **virtio-gpu-device**: GPU 设备
- **virtio-keyboard-device**: 键盘输入设备
- **virtio-mouse-device**: 鼠标输入设备

### 调试

```bash
# 启动 GDB 调试服务器（端口 1234）
make test_debug

# 在另一个终端连接 GDB
riscv64-linux-gnu-gdb build/bin/test
(gdb) target remote :1234
(gdb) continue
```

## 中断系统

本测试环境运行在 **S-mode (Supervisor Mode)**，由 OpenSBI 提供 M-mode 运行时支持。

### 支持的中断类型

- **Supervisor 软件中断** (SSI): 用于核间通信
- **Supervisor 定时器中断** (STI): 定时器中断（通过 SBI 设置）
- **Supervisor 外部中断** (SEI): 来自 PLIC 的外部设备中断

### PLIC 配置

PLIC (Platform-Level Interrupt Controller) 用于管理外部设备中断：

- **基地址**: 0x0C000000
- **Context**: Context 1 (S-mode hart 0)
- **支持的中断源**:
  - VirtIO 设备 (IRQ 1-8)
  - UART0 (IRQ 10)
- **中断优先级**: 1 (所有设备)
- **UART 输入中断

UART 支持接收数据中断，可以响应键盘输入：

- **中断触发**: 当 UART 接收缓冲区有数据时触发 IRQ 10
- **自动回显**: 收到的字符会自动回显到终端
- **特殊处理**: 回车符 (\r) 会自动转换为换行 (\n)

在 QEMU 中输入字符会触发中断，系统会显示中断信息并回显字符。

### 添加新的中断处理器

在 [trap.cpp](trap.cpp#L28) 的 `trap_handler` 函数中，根据 IRQ 号添加设备特定的处理逻辑：

```cpp
if (irq >= VIRTIO0_IRQ && irq <= VIRTIO7_IRQ) {
    // 调用具体 VirtIO 设备的中断处理函数
    virtio_device_handle_interrupt(irq);
} else if (irq == UART0_IRQ) {
    // UART 中断处理
    uart_handle_interrupt(
- **探测方法**: 读取 MMIO 魔数 (0x74726976 "virt") 和设备 ID
- **显示信息**: 基地址、设备类型、供应商 ID、版本号、IRQ

启动时会自动扫描并显示所有可用的 VirtIO 设备。

### 添加新的中断处理器

在 [trap.cpp](trap.cpp#L28) 的 `trap_handler` 函数中，根据 IRQ 号添加设备特定的处理逻辑：

```cpp
if (irq >= VIRTIO0_IRQ && irq <= VIRTIO7_IRQ) {
    // 调用具体 VirtIO 设备的中断处理函数
    virtio_device_handle_interrupt(irq);
}
```

## 内存布局

- **入口地址**: 0x80200000
- **内核代码段**: 从 0x80200000 开始
- **栈空间**: 每个 hart 4KB
- **物理内存**: 128MB (QEMU 配置)
- **无虚拟内存管理**: 直接使用物理地址

这将启动 QEMU 并运行测试程序。你会看到类似以下的输出：

```
========================================
virtio_driver Test Environment
========================================
Hart ID: 0x0000000000000000
DTB Address: 0x0000000087e00000

Test: Hello from C++!
Test: UART is working!

[SUCCESS] All tests passed!
========================================

[INFO] Entering infinite loop...
```

使用 `Ctrl+A` 然后按 `X` 退出 QEMU。

### 调试

要启动 GDB 调试模式：

```bash
make test_debug
```

然后在另一个终端中连接 GDB：

```bash
riscv64-linux-gnu-gdb build/tests/test-riscv64
(gdb) target remote :1234
(gdb) break _start
(gdb) continue
```

## UART API

测试环境提供了以下 UART 函数用于调试输出：

```c
void uart_init();                  // 初始化 UART（QEMU 中可选）
void uart_putc(char c);            // 输出单个字符
void uart_puts(const char *str);   // 输出字符串
void uart_put_hex(uint64_t num);   // 输出十六进制数
```

## 内存布局

- **启动地址**: 0x80200000（OpenSBI 跳转地址）
- **栈大小**: 4KB × CPU 核心数（默认 1 核）
- **内存**: 128MB (QEMU 配置)

## 添加新的测试

在 [main.cpp](main.cpp) 的 `test_main` 函数中添加你的测试代码：

```cpp
void test_main(uint32_t hart_id, uint8_t *dtb) {
    // 初始化代码
    uart_puts("Starting my test...\n");

    // 你的测试代码

    uart_puts("Test completed!\n");
}
```

## 配置选项

在 [CMakeLists.txt](CMakeLists.txt) 中可以配置：

- `SIMPLEKERNEL_DEFAULT_STACK_SIZE`: 每个核心的栈大小（字节）
- `SIMPLEKERNEL_MAX_CORE_COUNT`: 最大核心数
- `USE_NO_RELAX`: 是否禁用 RISC-V relax 优化

## 故障排查

### QEMU 无法启动

确保安装了 `qemu-system-riscv64`:

```bash
sudo apt install qemu-system-misc  # Debian/Ubuntu
```

### 编译错误

确保使用正确的工具链：

```bash
riscv64-linux-gnu-gcc --version
```

### 没有输出

检查入口点地址是否正确：

```bash
riscv64-linux-gnu-readelf -h build/tests/test-riscv64 | grep Entry
# 应该显示: Entry point address: 0x80200000
```

## 下一步

- 集成 virtio_driver 库
- 添加 VirtIO 设备初始化
- 实现块设备测试
- 添加网络设备测试
