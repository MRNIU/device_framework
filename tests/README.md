# virtio_driver 测试环境

这是一个最小可运行的 RISC-V 64 裸机测试环境，用于测试和开发 virtio_driver。

## 功能特性

- ✅ RISC-V 64 裸机环境
- ✅ UART 串口输出（可用于调试）
- ✅ 支持 QEMU virt 机器
- ✅ OpenSBI 支持
- ✅ 基本的异常处理框架

## 文件结构

```
tests/
├── boot.S          # 启动汇编代码
├── link.ld         # 链接脚本
├── macro.S         # 汇编宏定义
├── main.cpp        # 主程序（C++)
├── uart.h          # UART 驱动头文件
├── uart.c          # UART 驱动实现
├── trap.c          # 异常处理
└── CMakeLists.txt  # CMake 构建配置
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
