/*
 * LoongArch trap dispatch.
 *
 * Routes exceptions and interrupts to the appropriate handler.
 *
 * NOTE: TLB refill exceptions go to TLBRENTRY (which we set to
 * la_exception_entry).  They update TLBRERA/TLBRPRMD but NOT
 * ERA/PRMD/ESTAT.  trap_entry.S copies TLBRPRMD→PRMD so the
 * stack-switch logic works, and we detect ISTLBR here to handle
 * the refill before falling through to general-exception logic.
 *
 * Interrupts: timer (IS[11]).
 * Exceptions: syscall (ECODE = 0x0B), others → kill user proc / halt.
 */
#include "early_boot.h"
#include "trap.h"
#include "proc.h"

/* Defined in tlb_la.c — walks the current user page table. */
int la_tlb_refill_one(uint64_t va);
extern int la_tlb_refill_count;  /* diagnostic counter */

uint64_t la_syscall_dispatch(struct la_trap_frame *tf);

void la_trap_dispatch(struct la_trap_frame *tf)
{
    /* ---- TLB refill check (ISTLBR in TLBRERA) ---- */
    {
        uint64_t tlbrero = la_csr_read(LA_CSR_TLBRERA);
        if (tlbrero & 1) {                      /* ISTLBR set */
            uint64_t badv = la_csr_read(LA_CSR_TLBRBADV);

            if (la_tlb_refill_one(badv) == 0) {
                la_tlb_refill_count++;
                /* Do NOT clear ISTLBR!  Let ertn take the TLB refill
                 * return path (ISTLBR=1), which automatically sets
                 * CRMD.DA=0, CRMD.PG=1 — re-enabling paging.
                 *
                 * If we clear ISTLBR, ertn takes the general return
                 * path which does NOT set DA=0/PG=1, leaving the CPU
                 * in DA mode where TLB entries are ignored and all
                 * user addresses cause ADEF.
                 *
                 * TLBRERA already holds the faulting PC; ertn with
                 * ISTLBR=1 reads TLBRPRMD directly (not PRMD), so
                 * the trap frame ERA/PRMD writes are harmless.
                 */
                return;
            }

            /* Refill found no mapping.  Before declaring a fatal fault,
             * try to grow the user stack down to the faulting address:
             * exec pre-maps only 8 stack pages, and deep-stack programs
             * (libc-bench ~80 KB) legitimately fault below that.  On
             * success we refill the TLB for badv and return with ISTLBR
             * STILL set, so ertn re-runs the faulting instruction over
             * the now-present mapping (we must NOT clear ISTLBR here). */
            {
                struct la_proc *p = la_current_proc();
                if (p && p->is_user && p->pgtbl &&
                    la_uvm_grow_stack(p->pgtbl, badv) == 0 &&
                    la_tlb_refill_one(badv) == 0) {
                    la_tlb_refill_count++;
                    return;
                }
            }

            /* TLB refill genuinely failed: no mapping AND not a growable
             * stack miss.  A user process segfaulted — terminate just
             * that process so the kernel survives and the parent's wait4
             * collects the status.  la_proc_exit clears ISTLBR — critical,
             * because we arrived on the ISTLBR path and switch away WITHOUT
             * ertn (ertn is the only hardware clearer).  Only a kernel-mode
             * fault truly halts. */
            {
                uint64_t tlbrero_pc = tlbrero & ~1ULL;
                la_uart_puts("trap: TLB refill FAIL badv=");
                la_uart_put_hex(badv);
                la_uart_puts(" pc=");
                la_uart_put_hex(tlbrero_pc);
                la_uart_puts("\n");
                struct la_proc *p = la_current_proc();
                if (p && p->is_user) {
                    /* Dump key registers: $ra ($r1), $tp ($r2), $sp ($r3) */
                    la_uart_puts("  regs: ra=");
                    la_uart_put_hex(p->tf->gpr[1]);
                    la_uart_puts(" tp=");
                    la_uart_put_hex(p->tf->gpr[2]);
                    la_uart_puts(" sp=");
                    la_uart_put_hex(p->tf->gpr[3]);
                    la_uart_puts("\n");
                    la_uart_puts("trap: kill user proc (segv) badv=");
                    la_uart_put_hex(badv);
                    la_uart_puts(" pid=");
                    la_uart_put_hex(p->pid);
                    la_uart_puts(" parent=");
                    la_uart_put_hex(p->parent_pid);
                    la_uart_puts("\n");
                    la_proc_exit(-11);   /* noreturn */
                }
                la_uart_puts("trap: TLB refill FAIL in kernel — HALT\n");
                for (;;) {}
            }
        }
    }

    uint64_t entry_prmd = la_csr_read(LA_CSR_PRMD);
    int from_user = (entry_prmd & 0x3) != 0;
    uint64_t ecode = la_estat_ecode(tf->estat);

    /* ---- interrupt (ecode == 0) ---- */
    if (ecode == LA_ECODE_INT) {
        if (tf->estat & LA_ESTAT_IS_TIMER) {
            la_timer_interrupt();

            /* ---- timer preemption ----
             * Decrement the current user process's time-slice counter.
             * When it expires, voluntarily yield the CPU so the round-robin
             * scheduler can pick another runnable process.  The trap frame
             * stays on this process's private kernel stack; la_proc_yield()
             * saves sp in p->ctx, and when the process is resumed the frame
             * is restored and ertn returns to the interrupted instruction. */
            {
                struct la_proc *p = la_current_proc();
                if (p && p->is_user) {
                    p->ticks--;
                    if (p->ticks <= 0)
                        la_proc_yield();
                }
            }
        } else {
            la_uart_puts("trap: unknown interrupt IS=");
            la_uart_put_hex(tf->estat & 0xFFFF);
            la_uart_puts("\n");
        }
        goto check_signal;
    }

    /* ---- syscall (ecode == 0x0B) ---- */
    if (ecode == LA_ECODE_SYS) {
        uint64_t ret = la_syscall_dispatch(tf);
        tf->gpr[LA_GPR_A0] = ret;
        tf->era += LA_SYSCALL_INSN_SIZE;
        goto check_signal;
    }

    /* Floating-point disabled.  The LoongArch lp64d ABI permits user programs
     * to execute FP instructions; enable FPU for the current user context and
     * retry the faulting instruction.  The kernel does not use FP registers,
     * so this does not clobber kernel state. */
    if (ecode == LA_ECODE_FPD) {
        la_csr_write(la_csr_read(LA_CSR_EUEN) | LA_EUEN_FPE, LA_CSR_EUEN);
        return;
    }

    /* HPTW may report a normal page-invalid exception for freshly mapped
     * pages in our software page table (notably mmap/brk pages).  If the
     * current page table says the address is present, fill the TLB through
     * the normal CSR path and retry the faulting instruction. */
    if (ecode >= 0x1 && ecode <= 0x3) {
        struct la_proc *p = la_current_proc();
        if (p && p->is_user && p->pgtbl) {
            if (la_uva_to_pa(p->pgtbl, tf->badv) != 0 &&
                la_tlb_refill_one(tf->badv) == 0) {
                la_tlb_refill_count++;
                return;
            }
            if (la_uvm_grow_stack(p->pgtbl, tf->badv) == 0 &&
                la_tlb_refill_one(tf->badv) == 0) {
                la_tlb_refill_count++;
                return;
            }
        }
    }

    /* ---- unhandled exception → kill user proc / halt ----
     * Minimal report only: a verbose dump here blows the 4 KB kernel
     * stack (one page) and corrupts the neighbouring physical page,
     * which itself cascades into ADEF/INE in unrelated processes. */
    struct la_proc *fault_proc = la_current_proc();
    la_uart_puts("trap: ecode=");
    la_uart_put_hex(ecode);
    la_uart_puts(" era=");
    la_uart_put_hex(tf->era);
    la_uart_puts(" badv=");
    la_uart_put_hex(tf->badv);
    la_uart_puts(" name=");
    if (fault_proc) la_uart_puts(fault_proc->name);
    la_uart_puts("\n");
    la_uart_puts("  csr: CRMD=");
    la_uart_put_hex(la_csr_read(LA_CSR_CRMD));
    la_uart_puts(" PRMD=");
    la_uart_put_hex(la_csr_read(LA_CSR_PRMD));
    la_uart_puts(" TLBRERA=");
    la_uart_put_hex(la_csr_read(LA_CSR_TLBRERA));
    la_uart_puts(" TLBRBADV=");
    la_uart_put_hex(la_csr_read(LA_CSR_TLBRBADV));
    la_uart_puts(" TLBRPRMD=");
    la_uart_put_hex(la_csr_read(LA_CSR_TLBRPRMD));
    la_uart_puts("\n");
    if (ecode == 0x8) {
        la_uart_puts("  regs: ra=");
        la_uart_put_hex(tf->gpr[LA_GPR_RA]);
        la_uart_puts(" sp=");
        la_uart_put_hex(tf->gpr[LA_GPR_SP]);
        la_uart_puts(" a0=");
        la_uart_put_hex(tf->gpr[LA_GPR_A0]);
        la_uart_puts(" a1=");
        la_uart_put_hex(tf->gpr[LA_GPR_A1]);
        la_uart_puts(" a2=");
        la_uart_put_hex(tf->gpr[LA_GPR_A2]);
        la_uart_puts(" a3=");
        la_uart_put_hex(tf->gpr[LA_GPR_A3]);
        la_uart_puts("\n");
    }

    /* For page-fault-like exceptions, show whether ERA and BADV are actually
     * mapped in the current user page table.  This keeps the failure visible
     * while giving enough state to distinguish stale-TLB bugs from real user
     * faults. */
    if ((ecode >= 0x1 && ecode <= 0x4) || ecode == 0x8 || ecode == 0x9) {
        struct la_proc *cur = fault_proc;
        if (cur && cur->pgtbl) {
            uint64_t era_pa = la_uva_to_pa(cur->pgtbl, tf->era);
            uint64_t badv_pa = la_uva_to_pa(cur->pgtbl, tf->badv);
            uint64_t badv_pte = 0;
            if (tf->badv < (1ULL << 39)) {
                uint64_t idx0 = (tf->badv >> 30) & 0x1FF;
                uint64_t idx1 = (tf->badv >> 21) & 0x1FF;
                uint64_t idx2 = (tf->badv >> 12) & 0x1FF;
                uint64_t e0 = cur->pgtbl[idx0];
                if (e0) {
                    uint64_t *mid = (uint64_t *)e0;
                    uint64_t e1 = mid[idx1];
                    if (e1) {
                        uint64_t *leaf = (uint64_t *)e1;
                        badv_pte = leaf[idx2];
                    }
                }
            }
            la_uart_puts("  diag: ERA_PA=");
            la_uart_put_hex(era_pa);
            la_uart_puts(" BADV_PA=");
            la_uart_put_hex(badv_pa);
            la_uart_puts(" BADV_PTE=");
            la_uart_put_hex(badv_pte);
            la_uart_puts(" PGDL=");
            la_uart_put_hex(la_csr_read(LA_CSR_PGDL));
            la_uart_puts(" active=");
            la_uart_put_hex(la_tlb_active_pgtbl);
            la_uart_puts(" pgtbl=");
            la_uart_put_hex((uint64_t)cur->pgtbl);
            la_uart_puts("\n");
        }
    }

    struct la_proc *p = la_current_proc();
    if (p && p->is_user) {
        la_uart_puts("trap: kill user proc (fault) pid=");
        la_uart_put_hex(p->pid);
        la_uart_puts(" parent=");
        la_uart_put_hex(p->parent_pid);
        la_uart_puts("\n");
        la_proc_exit(-11);   /* noreturn */
    }
    for (;;) {}              /* kernel-mode unhandled exception → halt */

check_signal:
    /* After handling a syscall or timer interrupt, check whether a
     * signal needs to be delivered to the current process before
     * returning to user mode.  If one is pending and not blocked,
     * la_signal_deliver rewrites the trap frame to jump to the
     * signal handler; once the handler returns (via rt_sigreturn),
     * the original context is restored. */
    {
        struct la_proc *cur = la_current_proc();
        if (cur && cur->is_user) {
            if (la_signal_pending(tf))
                la_signal_deliver(tf);

            /* trap_entry.S chooses the final restore path from PRMD.PPLV.
             * Syscalls such as wait/futex/sigtimedwait can context-switch
             * inside the kernel; while they are asleep, the scheduler may
             * take kernel-mode timer interrupts, leaving PRMD.PPLV as PLV0.
             * Restore the user-return PRMD at the last C-side exit point and
             * close the tiny window for another nested kernel interrupt before
             * assembly executes ertn.  ertn will re-enable user interrupts
             * from PRMD.PIE. */
            if (from_user) {
                la_csr_write(LA_USER_PLV | LA_PRMD_PIE, LA_CSR_PRMD);
                la_csr_write(la_csr_read(LA_CSR_CRMD) & ~LA_CRMD_IE,
                             LA_CSR_CRMD);
            }
        }
    }
}
