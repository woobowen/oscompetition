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
            case 5: // S-mode timer interrupt (sstc extension)
                timer_interrupt_handler();
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
                tf->user_to_kern_epc += 4;
                syscall();
                tf = p->tf;
                break;
            }
            case 12:
            case 13:
            case 15:
            {
                uint64 fault_addr = r_stval();
                uint64 res = uvm_ustack_grow(p->pgtbl, p->ustack_npage, fault_addr);
                if (res == (uint64)-1) {
                    printf("[SEGV] pid=%d t=%d pc=%p stval=%p gp=%p tp=%p sp=%p ra=%p\n",
                           p->pid, trap_id, sepc, (void*)fault_addr, (void*)tf->gp, (void*)tf->tp, (void*)tf->sp, (void*)tf->ra);
                    proc_exit(-11);
                }
                break;
            }
            default:
            {
                printf("[SEGV] pid=%d t=%d pc=%p stval=%p gp=%p tp=%p sp=%p\n",
                       p->pid, trap_id, sepc, r_stval(), (void*)tf->gp, (void*)tf->tp, (void*)tf->sp);
                proc_exit(-11);
            }
        }
    }

    // 信号投递: 返回用户态前检查是否有待投递信号
    if (p->sig_pending != 0 && !p->sig_delivering) {
        for (int sig = 1; sig <= NSIG; sig++) {
            if (!(p->sig_pending & (1UL << (sig - 1))))
                continue;
            if (p->sig_handler[sig] <= 1)
                continue;

            p->sig_pending &= ~(1UL << (sig - 1));
            p->sig_delivering = 1;

            uint64 frame[32];
            frame[0]  = tf->user_to_kern_epc;
            frame[1]  = tf->ra;
            frame[2]  = tf->sp;
            frame[3]  = tf->gp;
            frame[4]  = tf->tp;
            frame[5]  = tf->t0;
            frame[6]  = tf->t1;
            frame[7]  = tf->t2;
            frame[8]  = tf->s0;
            frame[9]  = tf->s1;
            frame[10] = tf->a0;
            frame[11] = tf->a1;
            frame[12] = tf->a2;
            frame[13] = tf->a3;
            frame[14] = tf->a4;
            frame[15] = tf->a5;
            frame[16] = tf->a6;
            frame[17] = tf->a7;
            frame[18] = tf->s2;
            frame[19] = tf->s3;
            frame[20] = tf->s4;
            frame[21] = tf->s5;
            frame[22] = tf->s6;
            frame[23] = tf->s7;
            frame[24] = tf->s8;
            frame[25] = tf->s9;
            frame[26] = tf->s10;
            frame[27] = tf->s11;
            frame[28] = tf->t3;
            frame[29] = tf->t4;
            frame[30] = tf->t5;
            frame[31] = tf->t6;

            uint64 new_sp = (tf->sp - 256) & ~0xFUL;
            uvm_copyout(p->pgtbl, new_sp, (uint64)frame, sizeof(frame));

            tf->user_to_kern_epc = p->sig_handler[sig];
            tf->a0 = (uint64)sig;
            tf->sp = new_sp;
            tf->ra = p->sig_restorer;

            break;
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
    sstatus |= SSTATUS_FS_INITIAL; // 开启用户态 FPU，避免浮点指令触发非法指令异常
    w_sstatus(sstatus);

    // 跳转到 trampoline.S 的 user_return 处，返回用户态
    uint64 user_return_addr = (uint64)TRAMPOLINE + (uint64)(user_return - trampoline);
    ((void (*)(trapframe_t*, uint64))user_return_addr)((trapframe_t*)TRAPFRAME, user_satp);
}