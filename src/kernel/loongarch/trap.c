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

    /* Diagnostic: for page-fault-like exceptions (PIL=0x1, PIS=0x2,
     * PIF=0x3, PME=0x4, ADEF=0x8, ADEM=0x9), dump the PTE at badv
     * so we can see whether the page-table mapping is corrupted or the
     * TLB/HPTW delivered a stale translation.
     *
     * ALSO dump the PTE at ERA — for ADEF, ERA itself may be the
     * problematic address. */
    if ((ecode >= 0x1 && ecode <= 0x4) || ecode == 0x8 || ecode == 0x9) {
        struct la_proc *cur = fault_proc;
        if (cur && cur->pgtbl) {
            /* Dump PTE for ERA (useful for ADEF) */
            {
                uint64_t eva = tf->era;
                uint64_t eidx0 = (eva >> 30) & 0x1FF;
                uint64_t eidx1 = (eva >> 21) & 0x1FF;
                uint64_t eidx2 = (eva >> 12) & 0x1FF;
                uint64_t ee0 = cur->pgtbl[eidx0];
                uint64_t epte = 0;
                if (ee0) {
                    uint64_t *emid = (uint64_t *)ee0;
                    uint64_t ee1 = emid[eidx1];
                    if (ee1) {
                        uint64_t *eleaf = (uint64_t *)ee1;
                        epte = eleaf[eidx2];
                    }
                }
                la_uart_puts("  diag: ERA_PTE=");
                la_uart_put_hex(epte);
                la_uart_puts(" at era=");
                la_uart_put_hex(eva);
                la_uart_puts(epte ? " (mapped)" : " (UNMAPPED)");
                la_uart_puts("\n");
            }

            uint64_t dump_pa = la_uva_to_pa(cur->pgtbl, tf->badv);
            if (dump_pa) {
                /* Walk PTE explicitly to get perm bits (la_uva_to_pa
                 * only returns the PA). */
                uint64_t idx0 = (tf->badv >> 30) & 0x1FF;
                uint64_t idx1 = (tf->badv >> 21) & 0x1FF;
                uint64_t idx2 = (tf->badv >> 12) & 0x1FF;
                uint64_t e0 = cur->pgtbl[idx0];
                uint64_t pte = 0;
                if (e0) {
                    uint64_t *mid = (uint64_t *)e0;
                    uint64_t e1 = mid[idx1];
                    if (e1) {
                        uint64_t *leaf = (uint64_t *)e1;
                        pte = leaf[idx2];
                    }
                }
                la_uart_puts("  diag: PTE=");
                la_uart_put_hex(pte);
                la_uart_puts(" PGDL=");
                la_uart_put_hex(la_csr_read(LA_CSR_PGDL));
                la_uart_puts(" active=");
                la_uart_put_hex(la_tlb_active_pgtbl);
                la_uart_puts(" pgtbl=");
                la_uart_put_hex((uint64_t)cur->pgtbl);
                la_uart_puts("\n");
            } else {
                la_uart_puts("  diag: PTE unmapped "
                             "(no page table entry)\n");
            }
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
        if (cur && cur->is_user && la_signal_pending(tf))
            la_signal_deliver(tf);
    }
}
