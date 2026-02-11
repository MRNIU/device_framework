# Virtio 设备驱动架构设计

## 设计原则

遵循**组合优于继承**（Composition over Inheritance）和**单一职责原则**（Single Responsibility Principle）。

## 层次结构

```
Transport<LogFunc>              // 传输层接口（MMIO/PCI）
    ↑
    │ 引用
    │
DeviceInitializer<LogFunc>      // 设备初始化器（组合 Transport）
    ↑                           // 负责：Init(), SetupQueue(), Activate()
    │ 使用
    │
VirtioDevice<LogFunc>           // 设备基类（组合 Transport）
    ↑                           // 负责：通用设备操作和状态管理
    │ 继承
    ├─ VirtioBlk<LogFunc>       // 块设备（专注：读写、刷新）
    ├─ VirtioNet<LogFunc>       // 网络设备（专注：收发包）
    ├─ VirtioInput<LogFunc>     // 输入设备（专注：事件处理）
    └─ VirtioGpu<LogFunc>       // GPU 设备（专注：图形操作）
```

## 职责划分

### 1. Transport（传输层）
**职责**：提供底层硬件寄存器访问接口

- ✅ **如何**与设备通信（MMIO/PCI 寄存器读写）
- ✅ 读写设备状态、特性位、配置空间
- ✅ 配置队列地址和大小
- ✅ 中断状态读取和确认
- ❌ **不负责**初始化流程编排

**示例**：
- `MmioTransport`：MMIO 传输实现
- `PciTransport`：PCI 传输实现（未来）

### 2. DeviceInitializer（设备初始化器）
**职责**：编排设备初始化流程

- ✅ 执行 virtio 标准初始化序列（步骤 1-8）
- ✅ 特性协商逻辑
- ✅ 队列配置和激活
- ❌ **不负责**设备特定操作（读写数据等）

**关键方法**：
```cpp
auto Init(uint64_t driver_features) -> Expected<uint64_t>;
auto SetupQueue(...) -> Expected<void>;
auto Activate() -> Expected<void>;
```

**使用方式**：组合（在设备 `create()` 方法中使用）

### 3. VirtioDevice（设备基类）
**职责**：提供所有 virtio 设备的通用功能

- ✅ 持有 Transport 引用
- ✅ 存储协商后的特性位
- ✅ 提供通用设备信息访问（device_id、vendor_id 等）
- ✅ 提供特性检查方法（`has_feature()`）
- ✅ 提供设备状态检查（`is_active()`、`needs_reset()`）
- ❌ **不负责**设备特定操作

**关键方法**：
```cpp
auto transport() -> Transport<LogFunc>&;
auto features() const -> uint64_t;
auto has_feature(uint64_t bit) const -> bool;
auto device_id() const -> uint32_t;
auto is_active() const -> bool;
```

**使用方式**：继承（设备类继承此基类）

### 4. VirtioBlk / VirtioInput / ... (具体设备)
**职责**：实现设备特定的功能

- ✅ 设备特定的操作（如块设备的读写、输入设备的事件处理）
- ✅ 在 `create()` 工厂方法中使用 DeviceInitializer
- ✅ 继承 VirtioDevice 获得通用功能
- ❌ **不负责**初始化流程（委托给 DeviceInitializer）

## 使用示例

### 原始设计（不推荐）
```cpp
// VirtioBlk 直接在 create() 中重复实现初始化流程
class VirtioBlk : public Logger<LogFunc> {
 public:
  static auto create(...) -> Expected<VirtioBlk> {
    // 手动实现步骤 1-8（代码重复）
    transport.Reset();
    transport.SetStatus(kAcknowledge);
    transport.SetStatus(kAcknowledge | kDriver);
    // ... 50 行初始化代码 ...
    return VirtioBlk(...);
  }
};
```

**问题**：
- ❌ 每个设备都重复实现初始化流程
- ❌ 初始化逻辑与设备操作混在一起
- ❌ 难以测试和维护

### 新设计（推荐）
```cpp
// VirtioBlk 继承 VirtioDevice，使用 DeviceInitializer
class VirtioBlk : public VirtioDevice<LogFunc> {
 public:
  static auto create(Transport<LogFunc>& transport, ...) -> Expected<VirtioBlk> {
    // 步骤 1-6: 使用 DeviceInitializer 完成初始化
    DeviceInitializer<LogFunc> initializer(transport);
    auto features = initializer.Init(driver_features);
    if (!features) { return std::unexpected(features.error()); }

    // 步骤 7: 配置设备特定的队列
    initializer.SetupQueue(0, ...);

    // 步骤 8: 激活设备
    initializer.Activate();

    // 创建设备对象（调用基类 VirtioDevice 构造函数）
    return VirtioBlk(transport, vq, platform, features.value());
  }

  // 设备特定操作
  auto read_sector(uint64_t sector, uint8_t* buffer) -> Expected<void> {
    // 使用基类的 transport() 和 has_feature()
    if (this->has_feature(kRo)) { return Error::kReadOnly; }
    // ... 实现读取逻辑 ...
  }

 private:
  VirtioBlk(Transport<LogFunc>& transport, ...)
      : VirtioDevice<LogFunc>(transport, features), ... {}
};
```

**优势**：
- ✅ 初始化逻辑复用（所有设备共享）
- ✅ 代码简洁（create() 方法只有 ~20 行）
- ✅ 关注点分离：初始化 vs 设备操作
- ✅ 易于测试和维护

## 为什么不让设备继承 DeviceInitializer？

### 语义问题
```cpp
class VirtioBlk : public DeviceInitializer<LogFunc> {
  // 问题：块设备"是一个"初始化器 ❓
  // 不符合 is-a 关系，语义不清晰
};
```

### 接口污染
```cpp
VirtioBlk blk = ...;
blk.Init(...);        // ❌ 设备已初始化，不应该再暴露 Init()
blk.SetupQueue(...);  // ❌ 队列已配置，不应该被外部随意调用
blk.Activate();       // ❌ 设备已激活，不应该重复激活
blk.read_sector(...); // ✅ 这才是设备应该提供的接口
```

### 单一职责原则
- **DeviceInitializer**：负责初始化流程（临时使用）
- **VirtioDevice**：负责设备生命周期管理（持久存在）
- **VirtioBlk**：负责块设备特定操作（业务逻辑）

把它们混在一起违反单一职责原则。

## 推荐的实现步骤

### 1. 保持 Transport 和 DeviceInitializer 不变
这两个类已经设计良好，职责清晰。

### 2. 创建 VirtioDevice 基类
```cpp
// include/device/virtio_device.hpp（已创建）
template <class LogFunc = std::nullptr_t>
class VirtioDevice : public Logger<LogFunc> {
 protected:
  VirtioDevice(Transport<LogFunc>& transport, uint64_t features);
 // ...
};
```

### 3. 重构现有设备类
逐步将 VirtioBlk、VirtioInput 等改为继承 VirtioDevice：

```cpp
// Before
class VirtioBlk : public Logger<LogFunc> { ... };

// After
class VirtioBlk : public VirtioDevice<LogFunc> {
 public:
  static auto create(...) -> Expected<VirtioBlk> {
    DeviceInitializer<LogFunc> initializer(transport);
    // ... 使用 initializer ...
    return VirtioBlk(transport, features, ...);
  }
 private:
  VirtioBlk(Transport<LogFunc>& transport, uint64_t features, ...)
      : VirtioDevice<LogFunc>(transport, features), ... {}
};
```

### 4. 保持向后兼容（可选）
如果需要渐进式迁移，可以暂时保留旧的 create() 实现，
新增一个 create_v2() 使用新设计。

## 总结

| 类 | 职责 | 与设备关系 | 生命周期 |
|----|------|-----------|---------|
| Transport | 硬件寄存器访问 | 被引用 | 持久 |
| DeviceInitializer | 初始化流程编排 | 临时使用 | 短暂（仅初始化时） |
| VirtioDevice | 通用设备功能 | 被继承 | 持久 |
| VirtioBlk/Input/... | 设备特定操作 | 最终使用 | 持久 |

**设计模式**：
- Transport ← DeviceInitializer：**组合**（has-a）
- Transport ← VirtioDevice：**组合**（has-a）
- VirtioDevice ← VirtioBlk：**继承**（is-a）
- DeviceInitializer + VirtioBlk：**使用**（uses）
