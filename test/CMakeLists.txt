# Copyright The virtio_driver Contributors

PROJECT (test)

ENABLE_LANGUAGE (ASM)
ENABLE_LANGUAGE (C)
ENABLE_LANGUAGE (CXX)

# 添加可执行文件
ADD_EXECUTABLE (
    ${PROJECT_NAME} main.cpp boot.S uart.cpp trap.cpp test.cpp mmio_test.cpp
    # virtio_blk_test.cpp
)

# 设置编译选项
TARGET_COMPILE_OPTIONS (${PROJECT_NAME} PRIVATE -fno-stack-protector
                                                -fno-builtin -nostdlib)

# 设置链接选项
TARGET_LINK_OPTIONS (
    ${PROJECT_NAME}
    PRIVATE
    # 链接脚本
    -T
    ${CMAKE_CURRENT_SOURCE_DIR}/link.ld
    # 静态链接
    -static
    # 不链接标准库
    -nostdlib
    # 禁用 relax 优化
    -mno-relax
    # 确保链接器能找到链接脚本中的符号
    -Wl,--build-id=none)

# 添加要链接的库
TARGET_LINK_LIBRARIES (${PROJECT_NAME} PRIVATE virtio_driver)

# 生成反汇编文件以便调试
ADD_CUSTOM_COMMAND (
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJDUMP} -D $<TARGET_FILE:${PROJECT_NAME}> >
            ${PROJECT_NAME}.asm
    COMMENT "Generating disassembly file: ${PROJECT_NAME}.asm")

# 创建 images 目录
FILE (MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/images)

# 创建测试用的磁盘镜像（如果不存在）
IF(NOT EXISTS ${CMAKE_BINARY_DIR}/images/test.img)
    MESSAGE (STATUS "Creating test disk image...")
    EXECUTE_PROCESS (
        COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/images/test.img bs=1M
                count=64 RESULT_VARIABLE DD_RESULT)
    IF(DD_RESULT)
        MESSAGE (WARNING "Failed to create test disk image")
    ENDIF()
ENDIF()

# 添加 test_run 目标来启动 QEMU
ADD_CUSTOM_TARGET (
    test_run
    COMMAND
        qemu-system-riscv64 -nographic -serial stdio -monitor
        telnet::2333,server,nowait -machine virt -kernel
        $<TARGET_FILE:${PROJECT_NAME}> -m 128M
        # VirtIO 块设备 (块存储)
        -drive
        file=${CMAKE_BINARY_DIR}/images/test.img,if=none,format=raw,id=hd0
        -device virtio-blk-device,drive=hd0
        # VirtIO 网络设备
        -netdev user,id=net0 -device virtio-net-device,netdev=net0
        # VirtIO GPU 设备
        -device virtio-gpu-device
        # VirtIO 输入设备
        -device virtio-keyboard-device -device virtio-mouse-device
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running test in QEMU with VirtIO devices...")

# 添加 test_debug 目标来启动 QEMU 调试模式
ADD_CUSTOM_TARGET (
    test_debug
    COMMAND
        qemu-system-riscv64 -nographic -serial stdio -machine virt -kernel
        $<TARGET_FILE:${PROJECT_NAME}> -m 128M -s -S
        # VirtIO 块设备 (块存储)
        -drive
        file=${CMAKE_BINARY_DIR}/images/test.img,if=none,format=raw,id=hd0
        -device virtio-blk-device,drive=hd0
        # VirtIO 网络设备
        -netdev user,id=net0 -device virtio-net-device,netdev=net0
        # VirtIO GPU 设备
        -device virtio-gpu-device
        # VirtIO 输入设备
        -device virtio-keyboard-device -device virtio-mouse-device
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT
        "Running test in QEMU with GDB server (port 1234) and VirtIO devices..."
)
