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
 * Exceptions: syscall (ECODE = 0x0B), others → panic.
 */
#include "early_boot.h"
#include "trap.h"
#include "proc.h"

/* Defined in tlb_la.c — walks the current user page table. */
int la_tlb_refill_one(uint64_t va);

uint64_t la_syscall_dispatch(struct la_trap_frame *tf);

void la_trap_dispatch(struct la_trap_frame *tf)
{
    /* ---- TLB refill check (ISTLBR in TLBRERA) ---- */
    {
        uint64_t tlbrero = la_csr_read(LA_CSR_TLBRERA);
        if (tlbrero & 1) {                      /* ISTLBR set */
            uint64_t badv = la_csr_read(LA_CSR_TLBRBADV);

            if (la_tlb_refill_one(badv) == 0) {
                /* Patch trap frame so the ertn path returns correctly.
                 * We cleared ISTLBR, so ertn will use the general-exception
                 * return path (ERA + PRMD from trap frame). */
                tf->era = tlbrero & ~1ULL;      /* faulting PC */
                la_csr_write(0, LA_CSR_TLBRERA); /* clear ISTLBR */
                return;
            }

            /* TLB refill failed — no mapping for this VA */
            la_uart_puts("trap: TLB refill FAIL badv=");
            la_uart_put_hex(badv);
            la_uart_puts("\n");
            for (;;) {}
        }
    }

    uint64_t ecode = la_estat_ecode(tf->estat);

    /* ---- interrupt (ecode == 0) ---- */
    if (ecode == LA_ECODE_INT) {
        if (tf->estat & LA_ESTAT_IS_TIMER) {
            la_timer_interrupt();
        } else {
            la_uart_puts("trap: unknown interrupt IS=");
            la_uart_put_hex(tf->estat & 0xFFFF);
            la_uart_puts("\n");
        }
        return;
    }

    /* ---- syscall (ecode == 0x0B) ---- */
    if (ecode == LA_ECODE_SYS) {
        uint64_t ret = la_syscall_dispatch(tf);
        tf->gpr[LA_GPR_A0] = ret;
        tf->era += LA_SYSCALL_INSN_SIZE;
        return;
    }

    /* ---- unhandled exception → panic ---- */
    la_uart_puts("trap: ecode=");
    la_uart_put_hex(ecode);
    la_uart_puts(" era=");
    la_uart_put_hex(tf->era);
    la_uart_puts(" badv=");
    la_uart_put_hex(tf->badv);
    la_uart_puts("\n");

    struct la_proc *p = la_current_proc();
    if (p) {
        la_uart_puts("  proc: ");
        la_uart_puts(p->name);
        la_uart_puts(" pid=");
        la_uart_put_hex(p->pid);
        la_uart_puts("\n");
    }

    for (;;) {
    }
}
