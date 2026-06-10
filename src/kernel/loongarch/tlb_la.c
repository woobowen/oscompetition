/*
 * LoongArch TLB management + refill handler.
 *
 * Key insight from QEMU source:
 *   - tlbwr with NE=0 writes TLBEHI/TLBELO0/TLBELO1 to TLB[slot=INDEX]
 *   - tlbfill picks a random slot (ignores INDEX)
 *   - PS (page size) in TLBIDX[29:24] and STLBPS must match for STLB
 *   - TLB refill exceptions go to CSR_TLBRENTRY (separate from EENTRY)
 *
 * Our approach: set up a TLB refill handler that walks the user page
 * table on-demand and fills the TLB entry.  This avoids needing to
 * pre-fill all possible entries.
 */
#include "early_boot.h"

#define LA_PS_4KB  12

/* User page: V|D|PLV=3|G|P = 0xCF  (G bypasses ASID matching) */
#define LA_TLBELO_USER_RWX  0xCFUL

/* Kernel intermediate page: V|PLV=0|P = 0x89 */
#define LA_TLBELO_KERN      0x89UL

/* ---- Globals (set by proc.c before entering user mode) ---- */
uint64_t la_tlb_active_pgtbl;   /* current user page table root */

/* ---- Fill one TLB entry via tlbfill (random slot) ---- */
static void la_tlb_do_fill(uint64_t vppn, int is_odd,
                            uint64_t ppn, uint64_t perm, int ps)
{
    uint64_t tlbehi = (vppn << 13) & ((1ULL << 48) - 1);
    la_csr_write(tlbehi, LA_CSR_TLBEHI);

    uint64_t entry = ((ppn << 12) & ~0xFFFUL) | perm;
    if (is_odd) {
        la_csr_write(0, LA_CSR_TLBELO0);
        la_csr_write(entry, LA_CSR_TLBELO1);
    } else {
        la_csr_write(entry, LA_CSR_TLBELO0);
        la_csr_write(0, LA_CSR_TLBELO1);
    }

    la_csr_write((uint64_t)ps << 24, LA_CSR_TLBIDX);
    asm volatile("tlbfill" ::: "memory");
    asm volatile("dbar 0" ::: "memory");
}

/* ---- Walk 3-level page table and fill TLB for a VA ----
 * Called from la_trap_dispatch on TLB refill exceptions.
 * Uses la_tlb_active_pgtbl (set by proc.c before entering user mode).
 */
int la_tlb_refill_one(uint64_t va)
{
    uint64_t *root = (uint64_t *)la_tlb_active_pgtbl;
    if (!root) return -1;

    uint64_t idx0 = (va >> 30) & 0x1FF;
    uint64_t pte0 = root[idx0];
    if (!(pte0 & 1)) return -1;
    uint64_t *mid = (uint64_t *)(pte0 & ~0xFFFUL);

    uint64_t idx1 = (va >> 21) & 0x1FF;
    uint64_t pte1 = mid[idx1];
    if (!(pte1 & 1)) return -1;
    uint64_t *leaf = (uint64_t *)(pte1 & ~0xFFFUL);

    uint64_t idx2 = (va >> 12) & 0x1FF;
    uint64_t pte2 = leaf[idx2];
    if (!(pte2 & 1)) return -1;

    uint64_t ppn  = (pte2 >> 12) & 0xFFFFFFFFFFFFFUL;
    uint64_t perm = pte2 & 0xFFFUL;

    /* Override perm with G+P for TLB (bypass ASID matching) */
    perm = (perm & ~(0x40UL | 0x80UL)) | 0x40UL | 0x80UL; /* set G and P */

    uint64_t vppn   = va >> 13;
    int      is_odd = (va >> 12) & 1;

    la_tlb_do_fill(vppn, is_odd, ppn, perm, LA_PS_4KB);
    return 0;
}

/* ---- TLB refill handler (called from assembly) ---- */
void la_tlb_refill_handler(void)
{
    /* Read the faulting VA from TLBRBADV */
    uint64_t badv = la_csr_read(LA_CSR_TLBRBADV);

    /* Try to fill the TLB entry */
    if (la_tlb_refill_one(badv) == 0) {
        /* Success — clear ISTLBR and ertn (TLB refill path sets DA=0) */
        la_csr_write(0, LA_CSR_TLBRERA);  /* clear ISTLBR */
        asm volatile("ertn" ::: "memory");
    }

    /* Failed — print diagnostic and halt */
    la_uart_puts("TLB refill FAIL: badv=");
    la_uart_put_hex(badv);
    la_uart_puts("\n");
    for (;;) {}
}

/* ---- Invalidate all TLB entries ---- */
void la_tlb_inval_all(void)
{
    asm volatile("invtlb 0, $r0, $r0" ::: "memory");
}

/* ---- One-time TLB init ---- */
void la_tlb_init(void)
{
    /* Configure STLB for 4 KB pages */
    la_csr_write(LA_PS_4KB, LA_CSR_STLBPS);
}

/* ---- Pre-fill TLB for specific pages (optional) ---- */
int la_tlb_fill_all(uint64_t *pgtbl)
{
    int count = 0;

    for (int i = 0; i < 512; i++) {
        uint64_t pte0 = pgtbl[i];
        if (!(pte0 & 1)) continue;
        uint64_t *mid = (uint64_t *)(pte0 & ~0xFFFUL);

        for (int j = 0; j < 512; j++) {
            uint64_t pte1 = mid[j];
            if (!(pte1 & 1)) continue;
            uint64_t *leaf = (uint64_t *)(pte1 & ~0xFFFUL);

            for (int k = 0; k < 512; k++) {
                uint64_t pte2 = leaf[k];
                if (!(pte2 & 1)) continue;

                uint64_t ppn  = (pte2 >> 12) & 0xFFFFFFFFFFFFFUL;
                uint64_t perm = pte2 & 0xFFFUL;
                perm = (perm & ~(0x40UL | 0x80UL)) | 0x40UL | 0x80UL;

                uint64_t va = ((uint64_t)i << 30)
                            | ((uint64_t)j << 21)
                            | ((uint64_t)k << 12);
                uint64_t vppn   = va >> 13;
                int      is_odd = (va >> 12) & 1;

                la_tlb_do_fill(vppn, is_odd, ppn, perm, LA_PS_4KB);
                count++;
            }
        }
    }

    return count;
}
