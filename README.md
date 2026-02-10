# virtio_driver

这是一个刚开始的项目，还在设计阶段，文件结构和内容会频繁变动。以下是猜测的目录结构：


virtio_driver/
├── include/
│   └── virtio/
│       ├── virtio.h              # Virtio 核心定义（设备状态、特性位等）
│       ├── virtio_ring.h         # Virtqueue 环形缓冲区定义
│       ├── virtio_config.h       # 配置空间访问接口
│       ├── virtio_pci.h          # PCI 传输层头文件
│       ├── virtio_mmio.h         # MMIO 传输层头文件
│       └── devices/
│           ├── virtio_blk.h      # 块设备定义
│           ├── virtio_net.h      # 网络设备定义，占位，暂不实现
│           ├── virtio_console.h  # 控制台设备定义，占位，暂不实现
│           ├── virtio_gpu.h      # GPU 设备定义，占位，暂不实现
│           └── virtio_input.h    # 输入设备定义，占位，暂不实现
│
├── src/
│   ├── core/
│   │   ├── virtio.cpp              # Virtio 核心初始化/协商逻辑
│   │   ├── virtqueue.cpp           # Virtqueue 操作（分配、添加缓冲区、通知等）
│   │   └── features.cpp            # 特性协商
│   │
│   ├── transport/
│   │   ├── virtio_pci.cpp          # PCI 传输层实现，占位，暂不实现
│   │   ├── virtio_pci_modern.cpp   # PCI Modern (1.0+) 实现，占位，暂不实现
│   │   └── virtio_mmio.cpp         # MMIO 传输层实现
│   │
│   └── devices/
│       ├── virtio_blk.cpp          # 块设备驱动
│       ├── virtio_net.cpp          # 网络设备驱动，占位，暂不实现
│       ├── virtio_console.cpp      # 控制台驱动，占位，暂不实现
│       ├── virtio_gpu.cpp          # GPU 驱动，占位，暂不实现
│       └── virtio_input.cpp        # 输入设备驱动，占位，暂不实现
│
├── platform/                      # 平台适配层（与你的内核对接）
│   ├── platform.h                # 平台抽象接口
│   ├── memory.c                  # 内存分配/DMA 映射
│   ├── io.c                      # I/O 读写（MMIO/PIO）
│   ├── interrupt.c               # 中断注册/处理
│   └── pci.c                     # PCI 枚举/配置空间访问
│
├── docs/
│   ├── ARCHITECTURE.md           # 架构说明
│   ├── PORTING.md                # 移植指南
│   └── DEVICES.md                # 各设备实现说明
│
├── tests/                         # 单元测试/模拟测试
│   ├── test_virtqueue.cpp
│   └── test_virtio_blk.cpp
│
├── examples/                      # 使用示例
│   └── simple_blk_read.cpp
│
├── CMakeLists.txt / Makefile
├── README.md
└── LICENSE
