
# Virtio 1.2 Guest Driver (C++23 Header-only) 架构设计

本项目提供一个适用于 Freestanding 环境、零动态内存分配的 Virtio 1.2 (Modern 1.0+) Guest 驱动栈。整体架构深度应用 C++23 现代特性（如 Deducing `this`、Concepts），以 Policy-based Design 划分为四个高度解耦的层次。

驱动核心聚焦于**高性能 IO**与**零开销抽象**，支持多队列（Multi-queue）、Scatter-Gather IO 以及基于 Event Index 的中断风暴抑制。

## 1. 核心分层架构与边界

### 1.1 系统边界界定 (Device Discovery)

**本驱动库明确不包含设备发现与总线枚举机制。** 解析设备树（DTB）以寻找 MMIO 节点，或遍历 PCIe 总线读取配置空间，均属于内核总线驱动（Bus Driver）的职责。本驱动的 Transport 层仅接收系统发现并校验后的抽象句柄（如物理基址或 BDF 配置）并执行 Virtio 规范初始化。

### 1.2 四层抽象结构

* **Virtqueue 层 (队列管理)：**
* 提供环形缓冲区抽象，支持多队列（Multi-queue）实例化。
* **C++23 进化**：彻底废弃传统 CRTP 的 `static_cast`，利用 **C++23 Deducing `this` (Explicit object parameter)** 实现极简的编译期多态，分发至 `SplitVirtqueue` 或 `PackedVirtqueue`。
* **核心特性**：原生支持基于 `IoVec` 的 Scatter-Gather 描述符链组装；完整实现 Event Index (`VIRTIO_RING_F_EVENT_IDX`) 以优化通知机制。


* **Transport 层 (传输总线)：**
* **C++23 进化**：彻底废弃传统 CRTP 的 `static_cast`，利用 **C++23 Deducing `this` (Explicit object parameter)** 实现极简的编译期多态，分发至 `MmioTransport` 或 `PciTransport`。
* 负责设备特性协商、状态机流转与中断的确认（Ack）。


* **Device 层 (设备实现)：**
* 按需组合 `Virtqueue` (变长数组/Span 支持多队列) 和 `Transport`，解析具体协议（如 virtio-blk, virtio-net）。


* **Interface 层 (用户 API)：**
* 面向内核调度器的异步接口。分离请求入队（Enqueue）与硬件通知（Kick），支持批量提交（Batching）。采用回调/Token 模式完成异步通知。



## 2. 运行环境与并发安全模型

### 2.1 底层环境约束 (Environment Traits)

通过 C++23 `concept` 约束，内核环境需注入日志、物理内存转换与屏障操作。

**关于 DMA 内存一致性：本库不管理 cache 属性。** 提供给 Virtqueue 结构和 IO 数据缓冲区的内存，其 Non-cacheable 映射或 cache 刷新策略**完全由平台层（调用方）负责保证**。在典型的 RISC-V/ARM 裸机环境中，可通过页表属性将相关内存区域配置为 Device/Uncached 类型。

```cpp
template<typename T>
concept VirtioEnvironmentTraits = requires(void* ptr, uintptr_t phys) {
    // 日志输出：LogFunc 为无状态可调用类型
    // 签名要求：operator()(const char* format, Args&&... args) const -> int
    // 使用 std::nullptr_t 作为 LogFunc 时日志在编译期完全消除（零开销）
    { T::Log(static_cast<const char*>("")) } -> std::same_as<int>;

    // 内存屏障保证指令执行与内存可见性顺序
    { T::Mb() } -> std::same_as<void>;
    { T::Rmb() } -> std::same_as<void>;
    { T::Wmb() } -> std::same_as<void>;

    // 虚拟地址与物理地址（DMA 所需）的双向转换
    { T::VirtToPhys(ptr) } -> std::same_as<uintptr_t>;
    { T::PhysToVirt(phys) } -> std::same_as<void*>;
};

```

> **日志实现参考**：核心类通过继承 `Logger<LogFunc>` 获得 `Log()` 方法。`LogFunc` 模板参数默认为 `std::nullptr_t`（编译期消除），启用时须为无状态可调用类型，满足 `operator()(const char* format, Args&&...) const -> int` 签名。参见 `defs.h` 中的 `Logger` 模板。

### 2.2 并发安全模型

**驱动层内部不提供、也不隐式获取任何锁。** 本库不对调用方的并发模型做任何假设——无论是 Per-CPU Queue、多核共享队列还是单核中断/主循环竞争场景，**所有同步责任（如 Spinlock、中断屏蔽等）均由调用方承担**。

调用方应根据自身的运行模型保证：
- 同一 Virtqueue 的 `Enqueue` / `Kick` / `HandleInterrupt` 不被并发调用；
- 若多核共享同一队列，需在调用前持有适当的锁；
- 单核场景下，若 ISR 与主循环操作同一队列，需确保关中断保护。

> **设计动机**：锁策略高度依赖内核架构，驱动层硬编码锁机制会引入不必要的开销和耦合。

## 3. 核心接口定义

引入 Scatter-Gather 支持、多队列索引与批量提交通知。采用回调/Token 模式实现异步 IO。

> **注意**：以下 `ErrorCode` 仅为示意，实际错误码定义参见 `expected.hpp` 中的完整 `ErrorCode` 枚举。

```cpp
#include <expected>
#include <span>
#include <utility>

enum class ErrorCode { Success, QueueFull, IOError, InvalidArg /* 示意，完整定义见 expected.hpp */ };

// 用于 Scatter-Gather IO 的物理内存向量
struct IoVec {
    uintptr_t phys_addr;
    size_t len;
};

// 性能监控统计数据
struct VirtioStats {
    uint64_t bytes_transferred{0};
    uint64_t kicks_elided{0};       // 借助 Event Index 省略的 Kick 次数
    uint64_t interrupts_handled{0};
    uint64_t queue_full_errors{0};
};

template<
    VirtioEnvironmentTraits Traits,
    template<class> class Transport = MmioTransport,
    template<class> class Virtqueue = SplitVirtqueue
>
class BlockDevice {
public:
    using TransportConfig = typename Transport<Traits>::Config;
    using UserData = void*; // 回调模式下的 Token

    // ---------------- 1. 初始化 ----------------

    // 获取多队列所需的总内存大小
    // @return pair.first = 总字节数, pair.second = 对齐要求（字节）
    [[nodiscard]] static constexpr auto GetRequiredVqMemSize(
        uint16_t queue_count, uint32_t queue_size) -> std::pair<size_t, size_t>;

    // 创建设备实例
    // @param queue_count 期望的队列数量。实际使用 min(queue_count, 设备报告的 num_queues)，
    //        即由设备配置空间决定最大队列数，调用方传入的仅为期望值上限。
    // @param queue_size 每个队列的描述符数量，必须为 2 的幂
    [[nodiscard]] static auto Create(
        TransportConfig transport_cfg,
        void* vq_mem,
        uint16_t queue_count = 1,
        uint32_t queue_size = 128
    ) -> std::expected<BlockDevice, ErrorCode>;

    // ---------------- 2. 异步 IO (回调/Token + 批量提交) ----------------

    // 异步提交读请求。仅入队描述符，不触发硬件通知。
    // @param token 用户自定义上下文指针，在 HandleInterrupt 回调时原样传回
    [[nodiscard]] auto EnqueueRead(uint16_t queue_index, uint64_t sector,
        std::span<IoVec> buffers, UserData token)
        -> std::expected<void, ErrorCode>;

    // 异步提交写请求。仅入队描述符，不触发硬件通知。
    // 针对 Write 操作，Virtqueue 层在分配 Descriptor 时，应将 flag 设为无需设备写入（不包含 VRING_DESC_F_WRITE）
    [[nodiscard]] auto EnqueueWrite(uint16_t queue_index, uint64_t sector,
        std::span<IoVec> buffers, UserData token)
        -> std::expected<void, ErrorCode>;

    // 批量触发：结合 Event Index 决定是否真正写寄存器通知硬件
    void Kick(uint16_t queue_index);

    // ---------------- 3. 中断与监控 ----------------

    // 中断处理：在 ISR 中调用，遍历 Used Ring 中已完成的请求，
    // 对每个请求调用 on_complete 回调。
    // @tparam CompletionCallback 签名要求：void(UserData token, ErrorCode status)
    //         - token: 提交时传入的用户上下文指针
    //         - status: 设备返回的完成状态（如 VIRTIO_BLK_S_OK / S_IOERR）
    template<typename CompletionCallback>
    void HandleInterrupt(CompletionCallback&& on_complete);

    // 获取监控指标
    [[nodiscard]] auto GetStats() const -> VirtioStats;

    // ... 容量与块大小获取方法 ...
};

```

## 4. 关键技术细节

1. **Deducing `this` 替代 CRTP 示例 (Transport 内部)**：
```cpp
class TransportBase {
public:
    // 编译期静态分发，无需 static_cast<Derived*>(this)
    void AcknowledgeInterrupt(this auto&& self) {
        auto status = self.ReadIsr();
        if (status) self.WriteAck(status);
    }
};

```


2. **Scatter-Gather (描述符链)**：当上层传入 `std::span<IoVec>` 时，Virtqueue 层自动从 Available Ring 分配连续的描述符，设置 `VRING_DESC_F_NEXT` 标志并串联，最终仅将头部 Index 暴露给硬件，最大化单次 IO 的吞吐量。
