#include "mod.h"
#include <stdint.h>
#include "../mem/mod.h"  
#define UCODE_VA PGSIZE
#define RISCV_UCONTEXT_SIGMASK_OFFSET 40
#define RISCV_UCONTEXT_MCONTEXT_OFFSET 176
#define RISCV_SIGNAL_GREGS 32
#define RISCV_SIGNAL_FRAME_UCONTEXT_PC 32
#define RISCV_SIGNAL_FRAME_WORDS 34

// in trampoline.S
extern char trampoline[];  // 内核和用户切换的代码
extern char user_vector[]; // 用户触发陷阱进入内核
extern char user_return[]; // 内核处理完毕返回用户

// in trap.S
extern char kernel_vector[]; // 内核态trap处理流程, 进入内核后应当切换中断处理入口

// in trap_kernel.c
extern char *interrupt_info[16]; // 中断错误信息
extern char *exception_info[16]; // 异常错误信息

static int map_user_sigtrampoline(proc_t *p)
{
    pte_t *pte = vm_getpte(p->pgtbl, SIGTRAMPOLINE, false);
    if (pte != NULL && (*pte & PTE_V))
        return 0;

    uint32 code[] = {
        0x08b00893, /* li a7, SYS_rt_sigreturn */
        0x00000073, /* ecall */
        0x0000006f, /* j . */
    };
    void *pa = pmem_alloc(false);
    if (pa == NULL)
        return -1;
    memset(pa, 0, PGSIZE);
    memcpy(pa, code, sizeof(code));
    if (vm_try_mappages(p->pgtbl, SIGTRAMPOLINE, (uint64)pa, PGSIZE, PTE_R | PTE_X | PTE_U) < 0) {
        pmem_free((uint64)pa, false);
        return -1;
    }
    return 0;
}

static void fatal_signal_exit(int sig)
{
    proc_exit_group(-sig);
}

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    // 进入内核后切回 kernel_vector
    w_stvec((uint64)kernel_vector);

    proc_t *p = myproc(); // 获取当前进程
    trapframe_t *tf = p->tf;

    if (p->group_exit_pending)
        proc_exit_group_if_requested();

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
                uint64 res = uvm_mmap_handle_fault(p->pgtbl, fault_addr);
                if (res == (uint64)-1)
                    res = uvm_ustack_grow(p->pgtbl, p->ustack_npage, fault_addr);
                if (res == (uint64)-1) {
                    if (!p->sig_delivering && p->sig_handler[SIGSEGV] > 1) {
                        p->sig_pending |= (1UL << (SIGSEGV - 1));
                        p->sig_code[SIGSEGV] = SEGV_MAPERR;
                        p->sig_sender_pid[SIGSEGV] = 0;
                        break;
                    }
                    printf("[SEGV] pid=%d t=%d pc=%p stval=%p gp=%p tp=%p sp=%p ra=%p\n",
                           p->pid, trap_id, sepc, (void*)fault_addr, (void*)tf->gp, (void*)tf->tp, (void*)tf->sp, (void*)tf->ra);
                    fatal_signal_exit(SIGSEGV);
                }
                break;
            }
            default:
            {
                printf("[SEGV] pid=%d t=%d pc=%p stval=%p gp=%p tp=%p sp=%p\n",
                       p->pid, trap_id, sepc, r_stval(), (void*)tf->gp, (void*)tf->tp, (void*)tf->sp);
                fatal_signal_exit(SIGSEGV);
            }
        }
    }

    if (p->group_exit_pending)
        proc_exit_group_if_requested();

    // 信号投递: 返回用户态前检查是否有待投递信号
    if ((p->sig_pending & ~p->sig_mask) != 0 && !p->sig_delivering) {
        for (int sig = 1; sig <= NSIG; sig++) {
            uint64 sig_bit = 1UL << (sig - 1);
            if (!(p->sig_pending & sig_bit) || (p->sig_mask & sig_bit))
                continue;
            if (p->sig_handler[sig] == 1) {
                p->sig_pending &= ~(1UL << (sig - 1));
                p->sig_code[sig] = 0;
                p->sig_sender_pid[sig] = 0;
                continue;
            }
            if (p->sig_handler[sig] == 0) {
                if (sig == SIGINT || sig == SIGTERM || sig == SIGKILL || sig == SIGHUP ||
                    sig == SIGABRT || sig == SIGSEGV || sig == SIGBUS)
                    fatal_signal_exit(sig);
                p->sig_pending &= ~(1UL << (sig - 1));
                p->sig_code[sig] = 0;
                p->sig_sender_pid[sig] = 0;
                continue;
            }

            int sig_code = p->sig_code[sig];
            int sender_pid = p->sig_sender_pid[sig];
            p->sig_pending &= ~(1UL << (sig - 1));
            p->sig_code[sig] = 0;
            p->sig_sender_pid[sig] = 0;
            p->sig_delivering = 1;

            uint64 frame[RISCV_SIGNAL_FRAME_WORDS];
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
            uint64 ucontext_pc = frame[0];
            int restart_syscall = tf->a0 == (uint64)(-EINTR) &&
                p->last_syscall_restartable &&
                p->last_syscall_num == (int)tf->a7 &&
                (p->sig_flags[sig] & SA_RESTART) != 0 &&
                ucontext_pc >= 4;
            if (restart_syscall) {
                ucontext_pc -= 4;
                frame[0] = ucontext_pc;
                frame[10] = p->last_syscall_args[0];
                frame[11] = p->last_syscall_args[1];
                frame[12] = p->last_syscall_args[2];
                frame[13] = p->last_syscall_args[3];
                frame[14] = p->last_syscall_args[4];
                frame[15] = p->last_syscall_args[5];
            }
            frame[RISCV_SIGNAL_FRAME_UCONTEXT_PC] = ucontext_pc;
            frame[33] = 0;

            uint8 siginfo[128];
            uint8 ucontext[960];
            memset(siginfo, 0, sizeof(siginfo));
            memset(ucontext, 0, sizeof(ucontext));
            int *si_fields = (int *)siginfo;
            si_fields[0] = sig;
            si_fields[2] = sig_code;
            si_fields[4] = sender_pid;
            /* musl riscv64 ucontext_t places mcontext after
             * uc_flags, uc_link, uc_stack and uc_sigmask. */
            *(uint64 *)(ucontext + RISCV_UCONTEXT_SIGMASK_OFFSET) = p->sig_mask;
            uint64 *gregs = (uint64 *)(ucontext + RISCV_UCONTEXT_MCONTEXT_OFFSET);
            for (int i = 0; i < RISCV_SIGNAL_GREGS; i++)
                gregs[i] = frame[i];
            gregs[0] = ucontext_pc;

            uint64 new_sp = (tf->sp - sizeof(frame) - sizeof(siginfo) - sizeof(ucontext)) & ~0xFUL;
            uint64 siginfo_sp = new_sp + sizeof(frame);
            uint64 ucontext_sp = siginfo_sp + sizeof(siginfo);
            uvm_copyout(p->pgtbl, new_sp, (uint64)frame, sizeof(frame));
            uvm_copyout(p->pgtbl, siginfo_sp, (uint64)siginfo, sizeof(siginfo));
            uvm_copyout(p->pgtbl, ucontext_sp, (uint64)ucontext, sizeof(ucontext));

            uint64 restorer = p->sig_restorer;
            if (restorer == 0) {
                if (map_user_sigtrampoline(p) < 0) {
                    printf("signal: pid=%d map sigtrampoline failed\n", p->pid);
                    proc_exit(-12);
                    break;
                }
                restorer = SIGTRAMPOLINE;
            }

            tf->user_to_kern_epc = p->sig_handler[sig];
            tf->a0 = (uint64)sig;
            tf->a1 = (p->sig_flags[sig] & SA_SIGINFO) ? siginfo_sp : 0;
            tf->a2 = (p->sig_flags[sig] & SA_SIGINFO) ? ucontext_sp : 0;
            tf->sp = new_sp;
            tf->ra = restorer;

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
