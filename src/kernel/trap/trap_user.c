#include "mod.h"
#include <stdint.h>
#include "../mem/mod.h"  
#define UCODE_VA PGSIZE

// in trampoline.S
extern char trampoline[];  // 内核和用户切换的代码
extern char user_vector[]; // 用户触发陷阱进入内核
extern char user_return[]; // 内核处理完毕返回用户

// in trap.S
extern char kernel_vector[]; // 内核态trap处理流程, 进入内核后应当切换中断处理入口

// in trap_kernel.c
extern char *interrupt_info[16]; // 中断错误信息
extern char *exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    // 进入内核后切回 kernel_vector
    w_stvec((uint64)kernel_vector);
    
    proc_t *p = myproc(); // 获取当前进程
    trapframe_t *tf = p->tf;

    // 读取关键寄存器
    uint64 sepc = r_sepc();
    uint64 scause = r_scause();

    tf->user_to_kern_epc = sepc;

    //开始处理 trap
    int trap_id = scause & 0x3FF;  // 取低10位（RISC-V标准）

    if (scause & 0x8000000000000000UL) {
        // 1. 中断处理
        switch (trap_id) {
            case 1: // S-mode软件中断
                timer_interrupt_handler();
                // MLFQ: 仅当时间片用完/需要抢占时才让出CPU
                if (proc_on_tick())
                    proc_yield();
                break;
            case 9: // S-mode外设中断
                external_interrupt_handler();
                break;
            default:
                panic("trap_user_handler: unknown interrupt");
        }
    } else {
        // 2. 异常处理
        switch (trap_id) {
            case 8: // Environment call from U-mode (ecall)
            {
                syscall();
                tf->user_to_kern_epc += 4; // 系统调用返回时,PC=sepc + 4（类似中断）
                break;
            }
            case 13:
            case 15:
            {
                uint64 old_ustack_npage = p->ustack_npage;
                uint64 new_ustack_npage = uvm_ustack_grow(p->pgtbl, old_ustack_npage, r_stval());
                (void)new_ustack_npage;
                break;
            }
            //! 其余异常类型暂时不处理，直接报错并输出信息
            default:
                printf("!!! PANIC INFO !!!\n");
                printf("scause = %p (trap_id = %d)\n", scause, trap_id);
                printf("sepc   = %p\n", sepc);
                printf("stval  = %p\n", r_stval());
                panic("trap_user_handler: unknown exception");
        }
    }

    // 返回用户态
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    proc_t *p = myproc();         
    trapframe_t *tf = p->tf; 
    uint64 user_satp = MAKE_SATP(p->pgtbl);  // 获取用户页表的 satp 值

    // 在把 stvec 切到 user_vector 之前关闭 S 态中断
    intr_off(); // 关闭中断

    // 保存内核侧必要的信息到 trapframe，trampoline 会依赖这些字段来恢复内核环境
    tf->user_to_kern_satp = r_satp();                     
    tf->user_to_kern_sp = p->kstack + 2 * PGSIZE;           
    tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    tf->user_to_kern_hartid = mycpuid();      

    // 将 trapframe 地址写入 sscratch，trampoline/user_vector 依赖此值来保存/恢复寄存器
    w_sscratch((uint64)TRAPFRAME);

    // 将 S-mode 的 trap 入口再设置回 user_vector（trampoline）
    uint64 user_vector_addr = (uint64)TRAMPOLINE+ (uint64)(user_vector - trampoline);
    w_stvec(user_vector_addr);

    // 设置返回用户态时的 sepc 和 sstatus（使 sret 返回到 U-mode）
    // 防护性检查：确保要写入的 sepc 在合法的用户地址范围内，避免非法地址导致 sret 跳转到内核并触发页错误
    if (tf->user_to_kern_epc < USER_BASE || tf->user_to_kern_epc >= TRAMPOLINE) {
        printf("trap_user_return: bad user_to_kern_epc %p, replacing with UCODE_VA\n", (void*)tf->user_to_kern_epc);
        tf->user_to_kern_epc = UCODE_VA;
    }
    w_sepc(tf->user_to_kern_epc);
    uint64 sstatus = r_sstatus();
    sstatus &= ~SSTATUS_SPP; // 清 SPP，表示 sret 将返回到 U-mode
    sstatus |= SSTATUS_SPIE; // 置 SPIE，使 sret 返回后 U-mode 中断可用
    w_sstatus(sstatus);

    // 跳转到 trampoline.S 的 user_return 处，返回用户态
    uint64 user_return_addr = (uint64)TRAMPOLINE + (uint64)(user_return - trampoline);
    ((void (*)(trapframe_t*, uint64))user_return_addr)((trapframe_t*)TRAPFRAME, user_satp);
}