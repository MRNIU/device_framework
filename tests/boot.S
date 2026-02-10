/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include "macro.S"

.section .text.boot
.global _boot
.type _boot, @function
.extern _start
_boot:
    // Check if dtb address (a1) is zero
    beqz a1, 2f

2:
    add t0, a0, 1
    slli t0, t0, 12
    la sp, stack_top
    add sp, sp, t0

    // 将 a0 的值写入 tp
    mv tp, a0
    // 保存 sbi 传递的参数
    // 开辟栈空间
    addi sp, sp, -8*2
    // a0 为启动核 id
    SdBase a0, 0, sp
    // a1 为 dtb 地址
    SdBase a1, 1, sp

    // 跳转到 C 代码执行
    call _start

    wfi

// 声明所属段
.section .bss.boot
// 16 字节对齐
.align 16
.global stack_top
stack_top:
    .space 4096
