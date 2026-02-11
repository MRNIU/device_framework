# virtio_driver Copilot Instructions

### 你作为 AI 的角色

当用户要求你实现某个模块时：
1. **先阅读对应的接口头文件**——理解类声明、纯虚方法、Doxygen 契约（`@pre`/`@post`/`@note`）
2. **在独立的 `.cpp` 文件中生成实现**——不要修改头文件中的接口定义
3. **确保实现符合契约**——满足前置条件检查、后置条件保证、不引入额外的公共接口

## 技术栈

| 组件 | 技术选型 |
|------|---------|
| 语言标准 | C23 / C++23 |
| 构建系统 | CMake 3.27+ (CMakePresets) |
| 编译器 | GCC 交叉编译工具链 |
| 模拟器 | QEMU |
| 代码风格 | Google Style (clang-format/clang-tidy) |
| 测试框架 | GoogleTest |

## 编码规范

### 接口文件规范（最重要）

当创建或修改接口头文件时，必须遵循以下规范：

- **只包含声明**：类声明、纯虚接口、类型定义、常量，不包含方法实现
- **Doxygen 契约文档**：每个类和方法必须包含 `@brief`、`@pre`（前置条件）、`@post`（后置条件）
- **最小化 include**：接口头文件只包含声明所需的头文件，实现所需的头文件放在 `.cpp` 中
- **性能例外**：标记 `__always_inline` 的方法（如 `SpinLock::Lock()`）允许保留在头文件中

### 代码风格
- **格式化**: Google Style，使用 `.clang-format` 自动格式化
- **静态检查**: 使用 `.clang-tidy` 配置
- **Pre-commit**: 自动执行 clang-format、cmake-format、shellcheck

### Git Commit 规范
```
<type>(<scope>): <subject>

type: feat|fix|bug|docs|style|refactor|perf|test|build|revert|merge|sync|comment
scope: 可选，影响的模块 (如 arch, driver, libc)
subject: 不超过50字符，不加句号
```

### 命名约定
- **文件**: 小写下划线 (`kernel_log.hpp`)
- **类/结构体**: PascalCase (`TaskManager`)
- **函数**: PascalCase (`ArchInit`) 或 snake_case (`sys_yield`)
- **变量**: snake_case (`per_cpu_data`)
- **宏**: SCREAMING_SNAKE_CASE (`SIMPLEKERNEL_DEBUG`)
- **常量**: kCamelCase (`kPageSize`)
- **内核专用 libc/libc++ 头文件**: 使用 `sk_` 前缀 (`sk_cstdio`, `sk_vector`)

## 注意事项

### 常见陷阱
- 代码中禁止使用标准库的动态内存分配
- 编译选项使用 `-ffreestanding`，在 https://en.cppreference.com/w/cpp/freestanding.html 查阅可用库函数
