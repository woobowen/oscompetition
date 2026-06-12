/*
 * LoongArch TLB management + refill handler.
 *
 * STRATEGY: Use the STLB (2048 entries: 256 sets × 8 ways) instead of
 * the MTLB (64 entries).  With 2048 entries we can cache all ~512 pages
 * of busybox without thrashing.
 *
 * How it works:
 *   - CSR_STLBPS.PS = 12  (4KB page size for STLB entries)
 *   - Use the `tlbfill` instruction instead of `tlbwr`.
 *   - `tlbfill` auto-selects a slot: if TLBIDX.PS == STLBPS.PS it
 *     goes to the STLB (8-way set-associative); otherwise MTLB.
 *   - The STLB search in `loongarch_tlb_search_cb` checks all 8 ways
 *     of the relevant set, so entries placed by `tlbfill` are found.
 *
 * During ISTLBR=1 (TLB refill path), `tlbfill` reads TLBREHI/TLBRELO0/1.
 * During ISTLBR=0 (pre-fill path),  `tlbfill` reads TLBEHI/TLBIDX/TLBELO0/1.
 */
#include "early_boot.h"

#define LA_PS_4KB  12

/* User page: V|D|PLV=3|MAT=1|G|P|W = 0x1DF
 *   V(1) D(1) PLV=3(0xC) MAT=1(0x10) G(0x40) P(0x80) W(0x100)
 *   = 0x1 | 0x2 | 0xC | 0x10 | 0x40 | 0x80 | 0x100 = 0x1DF
 * We now use MAT=1 (Coherent Cached) to avoid DMA/cache incoherence:
 *   0x1DF = V|D|PLV=3|G|P|W   (with MAT=1)
 */
#define LA_TLBELO_USER_RWX  0x1DFUL

/* ---- Globals (set by proc.c before entering user mode) ---- */
uint64_t la_tlb_active_pgtbl;   /* current user page table root */

/* ---- TLB refill counter (diagnostic) ---- */
int la_tlb_refill_count = 0;

/* ---- Fill a TLB pair using tlbfill (STLB path, ISTLBR=1) ----
 * During TLB refill, ISTLBR=1, so tlbfill reads TLBREHI/TLBRELO0/1.
 * The hardware auto-picks a slot in the STLB (since PS==STLBPS.PS=12).
 */
static void la_tlb_do_fill_istlbr(uint64_t vppn,
                                    uint64_t ppn_even, uint64_t perm_even,
                                    uint64_t ppn_odd,  uint64_t perm_odd)
{
    /* TLBREHI = VPPN + PS
     *   bits [47:13] = VPPN
     *   bits [31:24] or similar = PS   (check QEMU CSR_TLBREHI layout)
     *
     * Looking at QEMU source, CSR_TLBREHI layout:
     *   bits [47:13] = VPPN (for LA64)
     *   bits [31:24] = PS
     *
     * But actually from the LoongArch spec, TLBREHI is:
     *   [31:24] = PS (page size)
     *   [47:13] = VPPN (virtual page number / 2)
     *
     * We construct it as: (vppn << 13) | (ps << 24)
     */
    /* TLBREHI layout (CSR 0x8E):
     *   bits [5:0]   = PS  (page size)   ← NOT bit 24! (that's TLBIDX.PS)
     *   bits [47:13] = VPPN (virtual page number / 2)
     */
    uint64_t tlbrehi = ((vppn << 13) & ((1ULL << 48) - 1))
                      | (uint64_t)LA_PS_4KB;

    /* Even page (TLBRELO0) */
    if (perm_even)
        la_csr_write(((ppn_even << 12) & ~0xFFFUL) | perm_even, 0x8C); /* TLBRELO0 */
    else
        la_csr_write(0, 0x8C);

    /* Odd page (TLBRELO1) */
    if (perm_odd)
        la_csr_write(((ppn_odd << 12) & ~0xFFFUL) | perm_odd, 0x8D); /* TLBRELO1 */
    else
        la_csr_write(0, 0x8D);

    /* Write TLBREHI last (VPPN + PS) */
    la_csr_write(tlbrehi, 0x8E); /* TLBREHI */

    /* tlbfill: auto-select STLB slot and fill */
    asm volatile("tlbfill" ::: "memory");
    asm volatile("dbar 0" ::: "memory");
}

/* ---- Fill a TLB pair using tlbfill (STLB path, ISTLBR=0) ----
 * During pre-fill (exec time), ISTLBR=0, so tlbfill reads
 * TLBEHI/TLBIDX/TLBELO0/1.
 */
static void la_tlb_do_fill_normal(uint64_t vppn,
                                    uint64_t ppn_even, uint64_t perm_even,
                                    uint64_t ppn_odd,  uint64_t perm_odd)
{
    /* TLBEHI = VPPN (bits [47:13]) */
    uint64_t tlbehi = (vppn << 13) & ((1ULL << 48) - 1);
    la_csr_write(tlbehi, 0x11); /* TLBEHI */

    /* Even page (TLBELO0) */
    if (perm_even)
        la_csr_write(((ppn_even << 12) & ~0xFFFUL) | perm_even, 0x12);
    else
        la_csr_write(0, 0x12);

    /* Odd page (TLBELO1) */
    if (perm_odd)
        la_csr_write(((ppn_odd << 12) & ~0xFFFUL) | perm_odd, 0x13);
    else
        la_csr_write(0, 0x13);

    /* TLBIDX: set PS=12, INDEX=0 (overwritten by tlbfill) */
    la_csr_write((uint64_t)LA_PS_4KB << 24, 0x10); /* TLBIDX */

    /* tlbfill: auto-select STLB slot and fill */
    asm volatile("tlbfill" ::: "memory");
    asm volatile("dbar 0" ::: "memory");
}

/* ---- Helper: look up PTE for a VA, return ppn and perm (0 if not mapped) ----
 * Directory entries are raw PAs (no flags); leaf entries have V|D|PLV|P|W. */
static int la_pte_lookup(uint64_t *root, uint64_t va,
                          uint64_t *out_ppn, uint64_t *out_perm)
{
    uint64_t idx0 = (va >> 30) & 0x1FF;
    uint64_t pte0 = root[idx0];
    if (!pte0) return -1;
    uint64_t *mid = (uint64_t *)pte0;

    uint64_t idx1 = (va >> 21) & 0x1FF;
    uint64_t pte1 = mid[idx1];
    if (!pte1) return -1;
    uint64_t *leaf = (uint64_t *)pte1;

    uint64_t idx2 = (va >> 12) & 0x1FF;
    uint64_t pte2 = leaf[idx2];
    if (!(pte2 & 1)) return -1;

    *out_ppn  = (pte2 >> 12) & 0xFFFFFFFFFFFFFUL;
    /* Override perm: force G (global, bypass ASID) and P (present) */
    *out_perm = (pte2 & 0xFFUL & ~(0x40UL | 0x80UL)) | 0x40UL | 0x80UL;
    return 0;
}

/* ---- Walk 3-level page table and fill TLB for a VA ----
 * Called from TLB refill handler with ISTLBR=1.
 * Fills BOTH the even and odd pages of the TLB pair.
 */
int la_tlb_refill_one(uint64_t va)
{
    uint64_t *root = (uint64_t *)la_tlb_active_pgtbl;
    if (!root) return -1;

    /* Compute even/odd pair base addresses */
    uint64_t va_even = va & ~0x1000ULL;
    uint64_t va_odd  = va |  0x1000ULL;

    uint64_t ppn_even, perm_even, ppn_odd, perm_odd;
    int have_even = (la_pte_lookup(root, va_even, &ppn_even, &perm_even) == 0);
    int have_odd  = (la_pte_lookup(root, va_odd,  &ppn_odd,  &perm_odd)  == 0);

    if (!have_even && !have_odd)
        return -1;

    uint64_t vppn = va >> 13;

    la_tlb_do_fill_istlbr(vppn,
                           have_even ? ppn_even : 0, have_even ? perm_even : 0,
                           have_odd  ? ppn_odd  : 0, have_odd  ? perm_odd  : 0);
    return 0;
}

/* ---- Invalidate all TLB entries ---- */
void la_tlb_inval_all(void)
{
    asm volatile("invtlb 0, $r0, $r0" ::: "memory");
}

/* ---- Invalidate the TLB pair (even/odd) matching a single VA ----
 * LoongArch TLB entries are paired by VPPN (one entry holds the even and
 * odd 4KB page).  When a refill fills a pair for which only one page is
 * mapped, the other page's slot is written invalid (V=0).  If that other
 * page is mapped LATER (e.g. a fresh mmap whose neighbour was already
 * faulted in), the stale invalid slot shadows the new mapping: the access
 * hits the existing pair entry and traps as a general exception instead of
 * refilling.  Dropping the pair on every new map forces a correct refill.
 *
 * invtlb op 0x6 = invalidate entries matching VA=rk, both G=0 and G=1. */
void la_tlb_inval_page(uint64_t va)
{
    asm volatile("invtlb 0x6, $r0, %0" :: "r"(va) : "memory");
}

/* ---- One-time TLB init ---- */
void la_tlb_init(void)
{
    /* Enable STLB with 4KB page size (PS=12).
     * CSR_STLBPS bits [3:0] = PS.
     * When TLBIDX.PS == STLBPS.PS, tlbfill writes to STLB (2048 entries)
     * instead of MTLB (64 entries).  This gives us 256 sets × 8 ways,
     * enough for busybox's ~512 pages without thrashing.
     */
    la_csr_write(LA_PS_4KB, 0x1E);  /* STLBPS = 12 */
    {
        uint64_t stlbps = la_csr_read(0x1E);
        la_uart_puts("  tlb: STLBPS=");
        la_uart_put_hex(stlbps);
        la_uart_puts(" (PS=12 for STLB)\n");
    }
}

/* ---- Pre-fill TLB for all mapped user pages (pair-aware) ---- */
int la_tlb_fill_all(uint64_t *pgtbl)
{
    int count = 0;

    for (int i = 0; i < 512; i++) {
        uint64_t pte0 = pgtbl[i];
        if (!pte0) continue;
        uint64_t *mid = (uint64_t *)pte0;

        for (int j = 0; j < 512; j++) {
            uint64_t pte1 = mid[j];
            if (!pte1) continue;
            uint64_t *leaf = (uint64_t *)pte1;

            for (int k = 0; k < 512; k += 2) {
                uint64_t pte2_even = leaf[k];
                uint64_t pte2_odd  = leaf[k + 1];

                int have_even = (pte2_even & 1) != 0;
                int have_odd  = (pte2_odd & 1) != 0;

                if (!have_even && !have_odd) continue;

                uint64_t va = ((uint64_t)i << 30)
                            | ((uint64_t)j << 21)
                            | ((uint64_t)k << 12);
                uint64_t vppn = va >> 13;

                uint64_t ppn_e = 0, perm_e = 0, ppn_o = 0, perm_o = 0;
                if (have_even) {
                    ppn_e  = (pte2_even >> 12) & 0xFFFFFFFFFFFFFUL;
                    perm_e = (pte2_even & 0xFFUL & ~(0x40UL | 0x80UL)) | 0x40UL | 0x80UL;
                }
                if (have_odd) {
                    ppn_o  = (pte2_odd >> 12) & 0xFFFFFFFFFFFFFUL;
                    perm_o = (pte2_odd & 0xFFUL & ~(0x40UL | 0x80UL)) | 0x40UL | 0x80UL;
                }

                la_tlb_do_fill_normal(vppn, ppn_e, perm_e, ppn_o, perm_o);
                count++;
            }
        }
    }

    return count;
}

/* ---- Dump TLB entries (diagnostic) ---- */
void la_tlb_dump(int max_entries)
{
    la_uart_puts("TLB dump (first ");
    la_uart_put_hex(max_entries);
    la_uart_puts(" entries):\n");

    for (int i = 0; i < max_entries && i < 2112; i++) {
        la_csr_write(i, 0x10);  /* TLBIDX = index */
        asm volatile("tlbrd" ::: "memory");  /* Read TLB entry */

        uint64_t idx = la_csr_read(0x10);    /* TLBIDX */
        if (idx & (1ULL << 63)) continue;    /* NE bit: not valid */

        uint64_t ehi  = la_csr_read(0x11);   /* TLBEHI */
        uint64_t elo0 = la_csr_read(0x12);   /* TLBELO0 */
        uint64_t elo1 = la_csr_read(0x13);   /* TLBELO1 */

        la_uart_puts("  [");
        la_uart_put_hex(i);
        la_uart_puts("] vppn=");
        la_uart_put_hex(ehi);
        la_uart_puts(" lo0=");
        la_uart_put_hex(elo0);
        la_uart_puts(" lo1=");
        la_uart_put_hex(elo1);
        la_uart_puts("\n");
    }
}
