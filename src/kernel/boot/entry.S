# kernel.ld 将_entry作为整个OS的起点放置到0x80200000处
# qemu会自动跳转到0x80200000处并开始执行
# 注意: OpenSBI 进入 S-mode 时 hartid 在 a0，dtb 指针在 a1

.section .text
.global _entry
_entry:
        # 先把 hartid 暂存在 tp，避免后续被函数调用污染
        mv tp, a0

        # CPU_stack 定义于start.c中
        # sp = CPU_stack + ((hartid + 1) * 4096)
        la sp, CPU_stack
        li t1, 4096
        addi t2, tp, 1
        mul t1, t1, t2
        add sp, sp, t1

        # very-early uart probe: write a few bytes to UART MMIO (0x10000000)
        li t1, 0x10000000
        li t2, '['
        sb t2, 0(t1)
        li t2, 'E'
        sb t2, 0(t1)
        li t2, ']'
        sb t2, 0(t1)
        li t2, '\n'
        sb t2, 0(t1)

        # 恢复 hartid 作为 boot_start 的第一个参数
        mv a0, tp
        call boot_start
spin:
        j spin
