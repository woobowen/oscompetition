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
extern int la_tlb_refill_count;  /* diagnostic counter */

uint64_t la_syscall_dispatch(struct la_trap_frame *tf);

/* Walk the user page table and print every VA whose leaf PTE maps to
 * the page containing target_pa.  More than one hit ⇒ physical-page
 * aliasing (two user VAs sharing one PA).  Dir entries are raw PAs. */
static void la_audit_pa(uint64_t *root, uint64_t target_pa)
{
    if (!root) return;
    uint64_t tpg = target_pa & ~0xFFFUL;
    int hits = 0;
    for (int i = 0; i < 512; i++) {
        uint64_t e0 = root[i];
        if (!e0) continue;
        uint64_t *mid = (uint64_t *)e0;
        for (int j = 0; j < 512; j++) {
            uint64_t e1 = mid[j];
            if (!e1) continue;
            uint64_t *leaf = (uint64_t *)e1;
            for (int k = 0; k < 512; k++) {
                uint64_t e2 = leaf[k];
                if (!(e2 & 1)) continue;
                if (((e2 & ~0xFFFUL)) == tpg) {
                    uint64_t va = ((uint64_t)i << 30) | ((uint64_t)j << 21)
                                | ((uint64_t)k << 12);
                    la_uart_puts("    va=");
                    la_uart_put_hex(va);
                    la_uart_puts(" pte=");
                    la_uart_put_hex(e2);
                    la_uart_puts("\n");
                    hits++;
                }
            }
        }
    }
    la_uart_puts("    (");
    la_uart_put_hex((uint64_t)hits);
    la_uart_puts(" hits)\n");
}

void la_trap_dispatch(struct la_trap_frame *tf)
{
    /* ---- TLB refill check (ISTLBR in TLBRERA) ---- */
    {
        uint64_t tlbrero = la_csr_read(LA_CSR_TLBRERA);
        if (tlbrero & 1) {                      /* ISTLBR set */
            uint64_t badv = la_csr_read(LA_CSR_TLBRBADV);

            /* Diagnostic: print first refill details to check HPTW state */
            if (la_tlb_refill_count < 5) {
                uint64_t pwch = la_csr_read(LA_CSR_PWCH);
                uint64_t pgdl = la_csr_read(LA_CSR_PGDL);
                uint64_t crmd = la_csr_read(LA_CSR_CRMD);
                la_uart_puts("  tlb-refill#");
                la_uart_put_hex(la_tlb_refill_count);
                la_uart_puts(" badv=");
                la_uart_put_hex(badv);
                la_uart_puts(" pwch=");
                la_uart_put_hex(pwch);
                la_uart_puts(" pgdl=");
                la_uart_put_hex(pgdl);
                la_uart_puts(" crmd=");
                la_uart_put_hex(crmd);
                la_uart_puts("\n");
            }

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

            /* TLB refill failed — no mapping for this VA */
            {
                uint64_t tlbrero = la_csr_read(LA_CSR_TLBRERA);
                uint64_t tlbrero_pc = tlbrero & ~1ULL;
                la_uart_puts("trap: TLB refill FAIL badv=");
                la_uart_put_hex(badv);
                la_uart_puts(" pc=");
                la_uart_put_hex(tlbrero_pc);
                la_uart_puts("\n");
                /* Try to read the faulting instruction */
                struct la_proc *pp = la_current_proc();
                if (pp && pp->pgtbl) {
                    uint64_t pa = la_uva_to_pa(pp->pgtbl, tlbrero_pc & ~3ULL);
                    if (pa) {
                        uint32_t *w = (uint32_t *)pa;
                        la_uart_puts("  insn@pc=");
                        la_uart_put_hex(w[0]);
                        la_uart_puts(" nearby=");
                        la_uart_put_hex(w[0]);
                        la_uart_puts(" ");
                        la_uart_put_hex(w[1]);
                        la_uart_puts(" ");
                        la_uart_put_hex(w[2]);
                        la_uart_puts(" ");
                        la_uart_put_hex(w[3]);
                        la_uart_puts("\n");
                    }
                }
                /* Dump ACTUAL trap frame registers (from kernel stack, not pp->tf).
                 * trap_entry.S saves all GPRs into the kernel stack trap frame
                 * (the tf parameter).  pp->tf is stale — it was only set during
                 * exec and never updated by the TLB refill / syscall paths. */
                {
                    la_uart_puts("  regs: sp=");
                    la_uart_put_hex(tf->gpr[LA_GPR_SP]);
                    la_uart_puts(" tp=");
                    la_uart_put_hex(tf->gpr[2]);  /* $tp = $r2 */
                    la_uart_puts(" a0=");
                    la_uart_put_hex(tf->gpr[LA_GPR_A0]);
                    la_uart_puts(" a1=");
                    la_uart_put_hex(tf->gpr[LA_GPR_A1]);
                    la_uart_puts("\n  regs: ra=");
                    la_uart_put_hex(tf->gpr[1]);  /* $ra = $r1 */
                    la_uart_puts(" s0=");
                    la_uart_put_hex(tf->gpr[23]); /* $s0 = $r23 */
                    la_uart_puts(" s3=");
                    la_uart_put_hex(tf->gpr[26]); /* $s3 = $r26 */
                    la_uart_puts(" t1=");
                    la_uart_put_hex(tf->gpr[13]); /* $t1 = $r13 */
                    la_uart_puts(" t5=");
                    la_uart_put_hex(tf->gpr[17]); /* $t5 = $r17 */
                    la_uart_puts("\n");
                    /* Dump memory around the ORIGINAL chunk pointer.
                     * $a0 was clobbered by get_meta (now holds meta->mem);
                     * $s0 ($r23) still holds the input pointer P. */
                    if (pp && pp->pgtbl) {
                        uint64_t pv = tf->gpr[23]; /* $s0 = original P */
                        if (pv) {
                            la_uart_puts("  P=");
                            la_uart_put_hex(pv);
                            /* chunk header (8 bytes) at P-8 — musl stores
                             * sizeclass/offset info here.  Read byte-by-byte
                             * to avoid alignment issues. */
                            uint64_t ph = la_uva_to_pa(pp->pgtbl, pv - 8);
                            if (ph) {
                                uint8_t *hb = (uint8_t *)ph;
                                uint64_t hv = 0;
                                for (int b = 0; b < 8; b++)
                                    hv |= (uint64_t)hb[b] << (b * 8);
                                la_uart_puts(" hdr@s0-8=");
                                la_uart_put_hex(hv);
                            }
                            /* meta-pointer candidates musl may load */
                            uint64_t p16 = la_uva_to_pa(pp->pgtbl, pv - 16);
                            if (p16) {
                                la_uart_puts(" q@s0-16=");
                                la_uart_put_hex(*(uint64_t *)p16);
                            }
                            uint64_t p0 = la_uva_to_pa(pp->pgtbl, pv);
                            if (p0) {
                                la_uart_puts(" q@s0+0=");
                                la_uart_put_hex(*(uint64_t *)p0);
                            }
                            la_uart_puts("\n");
                            /* first qword of the page (another group's meta) */
                            uint64_t page_base = pv & ~0xFFFUL;
                            uint64_t pa3 = la_uva_to_pa(pp->pgtbl, page_base);
                            if (pa3) {
                                la_uart_puts("  page@");
                                la_uart_put_hex(page_base);
                                la_uart_puts("=");
                                la_uart_put_hex(*(uint64_t *)pa3);
                                la_uart_puts("\n");
                            }
                            /* Read the ACTUAL TLB entry for the chunk's page
                             * and compare its PA to the page-table walk above.
                             * A mismatch means the CPU is using a stale/wrong
                             * TLB entry — the root cause of the crash. */
                            {
                                uint64_t vppn = page_base >> 13;
                                la_csr_write(vppn << 13, 0x11);   /* TLBEHI = VPPN */
                                asm volatile("tlbsrch" ::: "memory");
                                uint64_t sidx = la_csr_read(0x10); /* TLBIDX */
                                la_uart_puts("  tlb@");
                                la_uart_put_hex(page_base);
                                if (sidx & (1ULL << 63)) {
                                    la_uart_puts(" NOTFOUND pt_pa=");
                                    la_uart_put_hex(pa3);
                                } else {
                                    asm volatile("tlbrd" ::: "memory");
                                    uint64_t lo0 = la_csr_read(0x12);
                                    uint64_t lo1 = la_csr_read(0x13);
                                    la_uart_puts(" lo0=");
                                    la_uart_put_hex(lo0);
                                    la_uart_puts(" lo1=");
                                    la_uart_put_hex(lo1);
                                    la_uart_puts(" pt_pa=");
                                    la_uart_put_hex(pa3);
                                }
                                la_uart_puts("\n");
                            }
                        }
                        /* Dump meta structure at $s3 if valid */
                        uint64_t s3 = tf->gpr[26]; /* $s3 */
                        if (s3 && pp->pgtbl) {
                            uint64_t pa_s3 = la_uva_to_pa(pp->pgtbl, s3);
                            if (pa_s3) {
                                la_uart_puts("  meta@");
                                la_uart_put_hex(s3);
                                la_uart_puts("=");
                                la_uart_put_hex(*(uint64_t *)pa_s3);
                                la_uart_puts(" mem=");
                                la_uart_put_hex(*(uint64_t *)(pa_s3 + 16));
                                la_uart_puts(" avail=");
                                la_uart_put_hex(*(uint32_t *)(pa_s3 + 24));
                                la_uart_puts(" freed=");
                                la_uart_put_hex(*(uint32_t *)(pa_s3 + 28));
                                la_uart_puts("\n");
                            }
                            /* meta_area lives at the page base of meta
                             * (s3 & ~0xfff); its +0 field is the security
                             * "check" cookie that realloc compares against
                             * ctx.secret.  If this is 0/garbage, the store
                             * was lost or the page is aliased. */
                            uint64_t ma = s3 & ~0xFFFUL;
                            uint64_t pa_ma = la_uva_to_pa(pp->pgtbl, ma);
                            if (pa_ma) {
                                la_uart_puts("  metaarea@");
                                la_uart_put_hex(ma);
                                la_uart_puts(" pa=");
                                la_uart_put_hex(pa_ma);
                                la_uart_puts(" check=");
                                la_uart_put_hex(*(uint64_t *)pa_ma);
                                la_uart_puts("\n");
                            }
                            /* Is the brk meta_area PA the SAME page as this
                             * proc's kernel stack?  kstack is one pmem page
                             * (LA_KSTACK_SIZE=4096); if pmem double-allocated
                             * it, the kernel's trap frame / C-handler stack
                             * (which holds saved user SP and stack-derived
                             * locals) would overwrite the user's brk page.
                             * Also dump the first 8 qwords of the brk page to
                             * recognise the corruption pattern (trap frame?
                             * all-zero apart from check? etc.). */
                            la_uart_puts("  kstack=");
                            la_uart_put_hex((uint64_t)pp->kstack);
                            la_uart_puts(" ktop=");
                            la_uart_put_hex((uint64_t)pp->kstack + LA_KSTACK_SIZE);
                            if (pa_ma && (((uint64_t)pp->kstack & ~0xFFFUL)
                                          == (pa_ma & ~0xFFFUL)))
                                la_uart_puts(" <<<KSTACK==BRK>>>");
                            la_uart_puts("\n");
                            if (pa_ma) {
                                la_uart_puts("  brkpg:");
                                for (int q = 0; q < 8; q++) {
                                    la_uart_puts(" ");
                                    la_uart_put_hex(*((uint64_t *)pa_ma + q));
                                }
                                la_uart_puts("\n");
                            }
                            /* Alias probe: does the brk meta_area page
                             * share a PA with the user stack page?
                             * stack page = (sp & ~0xfff). */
                            {
                                uint64_t stk_va = tf->gpr[LA_GPR_SP] & ~0xFFFUL;
                                uint64_t pa_stk = la_uva_to_pa(pp->pgtbl, stk_va);
                                la_uart_puts("  alias: brk_pa=");
                                la_uart_put_hex(pa_ma);
                                la_uart_puts(" stk_va=");
                                la_uart_put_hex(stk_va);
                                la_uart_puts(" stk_pa=");
                                la_uart_put_hex(pa_stk);
                                if (pa_ma && pa_stk && pa_ma == pa_stk)
                                    la_uart_puts(" <<<ALIASED>>>");
                                la_uart_puts("\n");
                            }
                            /* Definitive aliasing test: list ALL user VAs
                             * whose leaf PTE maps to the brk meta_area PA.
                             * More than one hit ⇒ two VAs share one PA. */
                            la_uart_puts("  audit brk_pa ");
                            la_uart_put_hex(pa_ma);
                            la_uart_puts(":\n");
                            la_audit_pa(pp->pgtbl, pa_ma);
                            /* ctx.secret global lives at 0x1201fcab0
                             * (disasm: pcalau12i 81; ld.d -1360).  Read it
                             * via the page-table walk and audit its page. */
                            {
                                uint64_t sec_va = 0x1201fcab0ULL;
                                uint64_t sec_pa = la_uva_to_pa(pp->pgtbl, sec_va);
                                la_uart_puts("  secret@");
                                la_uart_put_hex(sec_va);
                                la_uart_puts(" pa=");
                                la_uart_put_hex(sec_pa);
                                if (sec_pa) {
                                    la_uart_puts(" val=");
                                    la_uart_put_hex(*(uint64_t *)sec_pa);
                                }
                                la_uart_puts("\n");
                                la_uart_puts("  audit bss_pa ");
                                la_uart_put_hex(sec_pa);
                                la_uart_puts(":\n");
                                la_audit_pa(pp->pgtbl, sec_pa);
                            }
                        }
                    }
                }
            }
            /* Refill genuinely failed (no mapping AND not a growable stack
             * miss).  A user process segfaulted: terminate just that
             * process so the kernel survives and the parent's wait4
             * collects the status.  la_proc_exit clears ISTLBR — critical,
             * because we arrived on the ISTLBR path and switch away WITHOUT
             * ertn (ertn is the only hardware clearer).  Only if there is no
             * current user process (kernel-mode fault) do we truly halt. */
            {
                struct la_proc *p = la_current_proc();
                if (p && p->is_user) {
                    la_uart_puts("trap: kill user proc (segv) badv=");
                    la_uart_put_hex(badv);
                    la_uart_puts("\n");
                    la_proc_exit(-11);   /* noreturn */
                }
            }
            la_uart_puts("trap: TLB refill FAIL in kernel — HALT\n");
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
    la_uart_puts(" refills=");
    la_uart_put_hex(la_tlb_refill_count);
    la_uart_puts(" estat=");
    la_uart_put_hex(tf->estat);
    la_uart_puts("\n");

    /* Extra diagnostics for ADEF / ADEM */
    if (ecode == 8 || ecode == 9) {
        uint64_t crmd = la_csr_read(LA_CSR_CRMD);
        la_uart_puts("  crmd=");
        la_uart_put_hex(crmd);
        uint64_t tlbrero = la_csr_read(LA_CSR_TLBRERA);
        la_uart_puts(" tlbrero=");
        la_uart_put_hex(tlbrero);
        uint64_t tlbrbadv = la_csr_read(LA_CSR_TLBRBADV);
        la_uart_puts(" tlbrbadv=");
        la_uart_put_hex(tlbrbadv);
        la_uart_puts("\n");

        /* Try to read instruction at era */
        struct la_proc *pp = la_current_proc();
        if (pp && pp->pgtbl) {
            uint64_t pa = la_uva_to_pa(pp->pgtbl, tf->era & ~0x7ULL);
            if (pa) {
                uint64_t *w = (uint64_t *)pa;
                int off = (tf->era >> 3) & 1;
                la_uart_puts("  insn@era=");
                la_uart_put_hex(w[off]);
                /* Also show nearby instructions */
                la_uart_puts(" nearby=");
                la_uart_put_hex(w[0]);
                la_uart_puts(" ");
                la_uart_put_hex(w[1]);
                la_uart_puts(" ");
                la_uart_put_hex(w[2]);
                la_uart_puts(" ");
                la_uart_put_hex(w[3]);
                la_uart_puts("\n");
            } else {
                la_uart_puts("  insn@era: NO MAPPING\n");
            }
            /* Check badv mapping */
            uint64_t pa2 = la_uva_to_pa(pp->pgtbl, tf->badv & ~0x7ULL);
            la_uart_puts("  badv_pa=");
            la_uart_put_hex(pa2);
            la_uart_puts("\n");
        }
    }

    struct la_proc *p = la_current_proc();
    if (p) {
        la_uart_puts("  proc: ");
        la_uart_puts(p->name);
        la_uart_puts(" pid=");
        la_uart_put_hex(p->pid);
        la_uart_puts("\n");
    }

    /* Unrecoverable exception.  If the victim is a user process, kill just
     * that process (la_proc_exit clears ISTLBR — a harmless no-op here, since
     * we are on the general-exception path where ISTLBR == 0).  Otherwise this
     * is a kernel-mode fault and we truly halt. */
    if (p && p->is_user) {
        la_uart_puts("trap: kill user proc (fault) era=");
        la_uart_put_hex(tf->era);
        la_uart_puts("\n");
        la_proc_exit(-11);   /* noreturn */
    }
    for (;;) {}
}
