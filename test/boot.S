/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include "macro.S"

// S-mode CSR 定义
.equ SSTATUS_SIE, 0x2       // Supervisor 模式全局中断使能
.equ SIE_SEIE, 0x200        // Supervisor 模式外部中断使能
.equ SIE_STIE, 0x20         // Supervisor 模式定时器中断使能
.equ SIE_SSIE, 0x2          // Supervisor 模式软件中断使能

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

    // 设置 Supervisor 模式中断向量
    la t0, _s_trap_entry
    csrw stvec, t0

    // 使能 Supervisor 模式全局中断
    li t0, SSTATUS_SIE
    csrs sstatus, t0

    // 使能外部中断、定时器中断、软件中断
    li t0, SIE_SEIE | SIE_STIE | SIE_SSIE
    csrw sie, t0

    // 跳转到 C 代码执行
    call _start

    wfi

// Supervisor 模式中断/异常入口
.section .text
.align 4
.global _s_trap_entry
_s_trap_entry:
    // 保存所有寄存器到栈上
    addi sp, sp, -32*8

    sd x1, 0*8(sp)
    sd x2, 1*8(sp)
    sd x3, 2*8(sp)
    sd x4, 3*8(sp)
    sd x5, 4*8(sp)
    sd x6, 5*8(sp)
    sd x7, 6*8(sp)
    sd x8, 7*8(sp)
    sd x9, 8*8(sp)
    sd x10, 9*8(sp)
    sd x11, 10*8(sp)
    sd x12, 11*8(sp)
    sd x13, 12*8(sp)
    sd x14, 13*8(sp)
    sd x15, 14*8(sp)
    sd x16, 15*8(sp)
    sd x17, 16*8(sp)
    sd x18, 17*8(sp)
    sd x19, 18*8(sp)
    sd x20, 19*8(sp)
    sd x21, 20*8(sp)
    sd x22, 21*8(sp)
    sd x23, 22*8(sp)
    sd x24, 23*8(sp)
    sd x25, 24*8(sp)
    sd x26, 25*8(sp)
    sd x27, 26*8(sp)
    sd x28, 27*8(sp)
    sd x29, 28*8(sp)
    sd x30, 29*8(sp)
    sd x31, 30*8(sp)

    // 读取 scause, sepc, stval (S-mode CSR)
    csrr a0, scause
    csrr a1, sepc
    csrr a2, stval

    // 调用 C 中断处理函数
    call trap_handler

    // 恢复所有寄存器
    ld x1, 0*8(sp)
    ld x2, 1*8(sp)
    ld x3, 2*8(sp)
    ld x4, 3*8(sp)
    ld x5, 4*8(sp)
    ld x6, 5*8(sp)
    ld x7, 6*8(sp)
    ld x8, 7*8(sp)
    ld x9, 8*8(sp)
    ld x10, 9*8(sp)
    ld x11, 10*8(sp)
    ld x12, 11*8(sp)
    ld x13, 12*8(sp)
    ld x14, 13*8(sp)
    ld x15, 14*8(sp)
    ld x16, 15*8(sp)
    ld x17, 16*8(sp)
    ld x18, 17*8(sp)
    ld x19, 18*8(sp)
    ld x20, 19*8(sp)
    ld x21, 20*8(sp)
    ld x22, 21*8(sp)
    ld x23, 22*8(sp)
    ld x24, 23*8(sp)
    ld x25, 24*8(sp)
    ld x26, 25*8(sp)
    ld x27, 26*8(sp)
    ld x28, 27*8(sp)
    ld x29, 28*8(sp)
    ld x30, 29*8(sp)
    ld x31, 30*8(sp)

    addi sp, sp, 32*8

    // 从中断返回 (S-mode)
    sret

// 声明所属段
.section .bss.boot
// 16 字节对齐
.align 16
.global stack_top
stack_top:
    .space 4096
