/*
 * LoongArch user virtual memory management.
 *
 * Simple 3-level page table for user processes.
 * The kernel runs with DA=1 (identity mapping / paging off).
 * User mode uses page tables (DA=0, forced by ertn to PLV3).
 *
 * Page table structure (LA64, 4KB pages):
 *   Level 0 (root): 512 entries, index bits [38:30]
 *   Level 1 (mid):  512 entries, index bits [29:21]
 *   Level 2 (leaf): 512 entries, index bits [20:12]
 *   Page offset:    12 bits
 *
 * Virtual address: 9 + 9 + 9 + 12 = 39 bits (512 GB)
 */
#include "early_boot.h"
#include "proc.h"

/* ---- LoongArch page table constants ---- */

#define LA_PT_ENTRIES   512

/* PTE bits (LoongArch TLBELO / page-table entry format, from QEMU cpu-csr.h)
 *   [0]    V     – Valid
 *   [1]    D     – Dirty (writable)
 *   [3:2]  PLV   – Privilege level (2-bit field: 0=PLV0, 3=all PLVs)
 *   [5:4]  MAT   – Memory access type (0=SO, 1=coherent)
 *   [6]    G     – Global
 *   [7]    P     – Present
 *   [47:12] PPN  – Physical page number
 *   [61]   NR    – No Read  (64-bit PTE only)
 *   [62]   NX    – No Execute (64-bit PTE only)
 */
#define LA_PTE_V        (1UL << 0)
#define LA_PTE_D        (1UL << 1)
#define LA_PTE_PLV_SHIFT 2
#define LA_PTE_PLV_MASK  (3UL << LA_PTE_PLV_SHIFT)
#define LA_PTE_PLV_KERN  (0UL << LA_PTE_PLV_SHIFT)  /* PLV0 only */
#define LA_PTE_PLV_USER  (3UL << LA_PTE_PLV_SHIFT)  /* all PLVs  */
#define LA_PTE_G        (1UL << 6)
#define LA_PTE_P        (1UL << 7)   /* Present */
#define LA_PTE_NX       (1UL << 62)  /* No Execute */
#define LA_PTE_NR       (1UL << 61)  /* No Read */

/* Permission shorthand.
 * When HPTW is enabled, QEMU's pte_write() checks bit W (bit 8),
 * NOT bit D (bit 1).  We set both D and W for maximum compatibility. */
#define LA_PTE_MAT_CC   (1UL << 4)   /* Coherent Cached */
#define LA_PTE_W        (1UL << 8)   /* Write permission (HPTW mode) */
#define LA_PTE_U_RWX    (LA_PTE_V | LA_PTE_D | LA_PTE_PLV_USER | LA_PTE_MAT_CC | LA_PTE_P | LA_PTE_W)
#define LA_PTE_U_RX     (LA_PTE_V | LA_PTE_PLV_USER | LA_PTE_MAT_CC | LA_PTE_P)
#define LA_PTE_U_RW     (LA_PTE_V | LA_PTE_D | LA_PTE_PLV_USER | LA_PTE_MAT_CC | LA_PTE_P | LA_PTE_W | LA_PTE_NX)
#define LA_PTE_K_RW     (LA_PTE_V | LA_PTE_D | LA_PTE_PLV_KERN | LA_PTE_MAT_CC | LA_PTE_P | LA_PTE_W)

/* Page table index extraction from virtual address */
#define LA_VA_DIR2(va)  (((va) >> 30) & 0x1FF)
#define LA_VA_DIR1(va)  (((va) >> 21) & 0x1FF)
#define LA_VA_PT(va)    (((va) >> 12) & 0x1FF)

/* Build / extract PTE */
#define LA_MK_PTE(pa, flags) (((uint64_t)(pa) & ~0xFFFUL) | (flags))
#define LA_PTE_PPN(pte)      ((pte) & ~0xFFFUL)

/*
 * PWCL value for 3-level 4KB page tables.
 *
 *   PTbase  = 12  (bits [4:0])   page offset bits
 *   PTwidth = 9   (bits [9:5])   leaf table index width
 *   Dir1_base = 21 (bits [14:10]) mid table index start
 *   Dir1_width = 9 (bits [19:15]) mid table index width
 *   Dir2_base = 30 (bits [24:20]) root table index start
 *   Dir2_width = 9 (bits [29:25]) root table index width
 *   PTEwidth  = 0  (bits [31:30]) — we do software walks, HPTW unused.
 *                QEMU rejects value 1 ("128 bit"), so use 0.
 */
#define LA_PWCL_VAL  ((9UL << 25) | (30UL << 20) | \
                      (9UL << 15) | (21UL << 10) | (9UL << 5)  | 12UL)

/* ---- Globals (used by trap_entry.S) ---- */
uint64_t la_user_pgd;     /* current user page table root (phys addr) */
uint64_t la_trap_ksp;     /* kernel SP to use on user→kernel trap */

/* ---- Create an empty user page table ---- */
uint64_t *la_uvm_create(void)
{
    return (uint64_t *)la_pmem_alloc();
}

/* ---- Walk page table, creating intermediate tables if needed ----
 * CRITICAL: Directory PTEs (root/mid levels) store ONLY the physical
 * address of the next-level table — NO flag bits.  This is required
 * because QEMU's HW page table walker (loongarch_ptw) does NOT strip
 * flag bits from directory entries; it uses base | index<<3 directly.
 * If flags were present, the index would be ORed with the flags,
 * producing wrong physical addresses.
 *
 * Leaf PTEs (level 2) still use the full V|D|PLV|P|W format.
 */
static uint64_t *la_uvm_walk(uint64_t *root, uint64_t va, int alloc)
{
    /* Level 0: root table */
    uint64_t idx0 = LA_VA_DIR2(va);
    uint64_t pte0 = root[idx0];
    uint64_t *mid;

    if (pte0) {
        mid = (uint64_t *)pte0;
    } else {
        if (!alloc) return 0;
        mid = (uint64_t *)la_pmem_alloc();
        if (!mid) return 0;
        root[idx0] = (uint64_t)mid;  /* No flags — just the PA */
    }

    /* Level 1: mid table */
    uint64_t idx1 = LA_VA_DIR1(va);
    uint64_t pte1 = mid[idx1];
    uint64_t *leaf;

    if (pte1) {
        leaf = (uint64_t *)pte1;
    } else {
        if (!alloc) return 0;
        leaf = (uint64_t *)la_pmem_alloc();
        if (!leaf) return 0;
        mid[idx1] = (uint64_t)leaf;  /* No flags — just the PA */
    }

    /* Level 2: leaf table — return pointer to the PTE */
    return &leaf[LA_VA_PT(va)];
}

/* ---- Map a physical page at a virtual address ---- */
int la_uvm_map_page(uint64_t *root, uint64_t va, uint64_t pa, uint64_t perm)
{
    uint64_t *pte = la_uvm_walk(root, va, 1);
    if (!pte) return -1;
    if (*pte & LA_PTE_V) {
        la_uart_puts("  uvm_map: remap va=");
        la_uart_put_hex(va);
        la_uart_puts("\n");
        return -1;
    }
    *pte = LA_MK_PTE(pa, perm);
    /* A new mapping may share a TLB pair (even/odd) with an entry that a
     * previous refill wrote with this page marked invalid (V=0).  Drop that
     * pair so the next access refills both pages from the now-correct PTE. */
    la_tlb_inval_page(va);
    return 0;
}

/* ---- Allocate a new page and map it ---- */
uint64_t la_uvm_alloc_page(uint64_t *root, uint64_t va, uint64_t perm)
{
    void *pa = la_pmem_alloc();
    if (!pa) return 0;
    if (la_uvm_map_page(root, va, (uint64_t)pa, perm) < 0) {
        la_pmem_free(pa);
        return 0;
    }
    return (uint64_t)pa;
}

/* ---- Grow the current proc's user stack down to cover fault_addr ----
 *
 * exec pre-maps only 8 stack pages; real programs (libc-bench ~80 KB) need
 * more.  When a TLB refill / page fault lands in the unmapped stack region,
 * the trap handler calls this to extend the stack on demand.
 *
 *   fault_addr  – the address that faulted (any value in the missing page)
 *
 * Returns 0 and maps every page in [fault_page, stack_bottom) if fault_page
 * is within the legal stack window; returns -1 (no change) if the fault is
 * NOT a stack-region miss (caller then treats it as a fatal segv).  The
 * pages are zeroed by la_pmem_alloc, matching Linux stack semantics. */
int la_uvm_grow_stack(uint64_t *root, uint64_t fault_addr)
{
    struct la_proc *p = la_current_proc();
    if (!p || !root || p->stack_bottom == 0)
        return -1;

    uint64_t fault_page = fault_addr & ~((uint64_t)LA_PGSIZE - 1);
    uint64_t hard_floor = LA_USER_STACK
                        - (uint64_t)LA_MAX_STACK_PAGES * LA_PGSIZE;

    if (fault_page >= p->stack_bottom)   /* already mapped / not stack area */
        return -1;
    if (fault_page < hard_floor)          /* exceeded the growth cap */
        return -1;

    /* Map every page from the faulting one up to the current bottom.
     * Skip any that are already present (e.g. a mid-region page touched
     * out of order) so we never clobber an existing mapping. */
    for (uint64_t va = fault_page; va < p->stack_bottom; va += LA_PGSIZE) {
        uint64_t *pte = la_uvm_walk(root, va, 0);
        if (pte && (*pte & LA_PTE_V))
            continue;
        if (la_uvm_alloc_page(root, va, LA_PTE_U_RWX) == 0)
            return -1;   /* OOM: leave what we mapped; report failure */
    }

    p->stack_bottom = fault_page;
    return 0;
}

/* ---- Copy data into user virtual address space ---- */
void la_uvm_copy_in(uint64_t *root, uint64_t va, const void *src, uint32_t len)
{
    const char *s = (const char *)src;
    uint32_t off = 0;

    while (off < len) {
        uint64_t page_va = (va + off) & ~0xFFFUL;
        uint64_t page_off = (va + off) & 0xFFFUL;
        uint32_t chunk = LA_PGSIZE - page_off;
        if (chunk > len - off)
            chunk = len - off;

        uint64_t *pte = la_uvm_walk(root, page_va, 0);
        if (!pte || !(*pte & LA_PTE_V)) {
            la_uart_puts("  uvm_copy_in: unmapped va=");
            la_uart_put_hex(page_va);
            la_uart_puts("\n");
            return;
        }

        uint64_t pa = LA_PTE_PPN(*pte) + page_off;
        char *dst = (char *)pa;
        for (uint32_t i = 0; i < chunk; i++)
            dst[i] = s[off + i];

        off += chunk;
    }
}

/* ---- Initialize paging CSRs (call once at boot) ---- */
void la_uvm_paging_init(void)
{
    la_csr_write(LA_PWCL_VAL, LA_CSR_PWCL);
    /* Enable hardware page table walker (HPTW).
     * HPTW_EN is bit 24 of PWCH (CSR 0x19).
     * Without HPTW, QEMU never walks the page table on TLB miss — every
     * miss raises an exception that our software handler must fill.
     * With HPTW, QEMU walks the page table directly and fills the
     * cputlb without raising exceptions — eliminating the millions
     * of TLB refill exceptions for large binaries like busybox. */
    la_csr_write(1UL << 24, LA_CSR_PWCH);  /* HPTW_EN = 1 */
    {
        uint64_t pwch = la_csr_read(LA_CSR_PWCH);
        la_uart_puts("  uvm: PWCH=");
        la_uart_put_hex(pwch);
        la_uart_puts(" (HPTW_EN expect ");
        la_uart_put_hex(1UL << 24);
        la_uart_puts(")\n");
    }
    /* Verify */
    uint64_t pwcl = la_csr_read(LA_CSR_PWCL);
    la_uart_puts("  uvm: PWCL=");
    la_uart_put_hex(pwcl);
    la_uart_puts(" (expect ");
    la_uart_put_hex(LA_PWCL_VAL);
    la_uart_puts(")\n");
}

/* ---- Set user page table (before ertn to user mode) ---- */
void la_uvm_switch(uint64_t *pgtbl)
{
    la_user_pgd = (uint64_t)pgtbl;
    /* Write PGDL and PGDH directly (PGD CSR 0x18 might not work) */
    la_csr_write((uint64_t)pgtbl, LA_CSR_PGDL);
    la_csr_write(0, LA_CSR_PGDH);
}

/* ================================================================
 *  User ↔ Kernel data copy helpers
 *  The kernel runs with DA=1 (identity mapping), so kernel addresses
 *  ARE physical addresses.  User VA→PA requires a page table walk.
 * ================================================================ */

/* Walk the page table to translate a user VA to PA.  Returns 0 on fault.
 * Directory entries are raw PAs (no flags); leaf entries have V|D|PLV|P|W. */
uint64_t la_uva_to_pa(uint64_t *root, uint64_t va)
{
    if (!root) return 0;
    uint64_t idx0 = (va >> 30) & 0x1FF;
    uint64_t e0 = root[idx0];
    if (!e0) return 0;
    uint64_t *mid = (uint64_t *)e0;

    uint64_t idx1 = (va >> 21) & 0x1FF;
    uint64_t e1 = mid[idx1];
    if (!e1) return 0;
    uint64_t *leaf = (uint64_t *)e1;

    uint64_t idx2 = (va >> 12) & 0x1FF;
    uint64_t e2 = leaf[idx2];
    if (!(e2 & LA_PTE_V)) return 0;

    return LA_PTE_PPN(e2) + (va & 0xFFFUL);
}

/* Copy bytes from user VA to kernel buffer.
 * Returns number of bytes copied. */
uint32_t la_copy_from_user(void *kdst, uint64_t usrc, uint32_t len)
{
    struct la_proc *p = la_current_proc();
    if (!p || !p->pgtbl) return 0;

    uint8_t *d = (uint8_t *)kdst;
    uint32_t done = 0;
    while (done < len) {
        uint64_t va = usrc + done;
        uint64_t page_off = va & 0xFFFUL;
        uint32_t chunk = LA_PGSIZE - page_off;
        if (chunk > len - done) chunk = len - done;

        uint64_t pa = la_uva_to_pa(p->pgtbl, va);
        if (!pa) break;

        const uint8_t *s = (const uint8_t *)pa;
        for (uint32_t i = 0; i < chunk; i++)
            d[done + i] = s[i];
        done += chunk;
    }
    return done;
}

/* Copy bytes from kernel buffer to user VA.
 * Returns number of bytes copied. */
uint32_t la_copy_to_user(uint64_t udst, const void *ksrc, uint32_t len)
{
    struct la_proc *p = la_current_proc();
    if (!p || !p->pgtbl) return 0;

    const uint8_t *s = (const uint8_t *)ksrc;
    uint32_t done = 0;
    while (done < len) {
        uint64_t va = udst + done;
        uint64_t page_off = va & 0xFFFUL;
        uint32_t chunk = LA_PGSIZE - page_off;
        if (chunk > len - done) chunk = len - done;

        uint64_t pa = la_uva_to_pa(p->pgtbl, va);
        if (!pa) break;

        uint8_t *d = (uint8_t *)pa;
        for (uint32_t i = 0; i < chunk; i++)
            d[i] = s[done + i];
        done += chunk;
    }
    return done;
}

/* Copy a null-terminated string from user VA.
 * Returns string length (not counting NUL), or -1 on fault. */
int la_copy_str_from_user(char *kdst, uint64_t usrc, uint32_t max)
{
    struct la_proc *p = la_current_proc();
    if (!p || !p->pgtbl) return -1;

    uint32_t i = 0;
    while (i < max) {
        uint64_t pa = la_uva_to_pa(p->pgtbl, usrc + i);
        if (!pa) { kdst[i] = 0; return -1; }
        char c = *(const char *)pa;
        kdst[i] = c;
        if (c == 0) return (int)i;
        i++;
    }
    kdst[max - 1] = 0;
    return (int)(max - 1);
}

/* ================================================================
 *  Deep-copy a user page table (for fork).
 *  Returns 0 on success, -1 on allocation failure.
 * ================================================================ */
int la_uvm_copy_pgtbl(uint64_t *src, uint64_t *dst)
{
    for (int i = 0; i < LA_PT_ENTRIES; i++) {
        if (!src[i]) continue;
        uint64_t *src_mid = (uint64_t *)src[i];

        /* Allocate mid-level table */
        uint64_t *dst_mid = (uint64_t *)la_pmem_alloc();
        if (!dst_mid) return -1;
        for (int z = 0; z < LA_PT_ENTRIES; z++) dst_mid[z] = 0;
        dst[i] = (uint64_t)dst_mid;  /* No flags — raw PA */

        for (int j = 0; j < LA_PT_ENTRIES; j++) {
            if (!src_mid[j]) continue;
            uint64_t *src_leaf = (uint64_t *)src_mid[j];

            /* Allocate leaf-level table */
            uint64_t *dst_leaf = (uint64_t *)la_pmem_alloc();
            if (!dst_leaf) return -1;
            for (int z = 0; z < LA_PT_ENTRIES; z++) dst_leaf[z] = 0;
            dst_mid[j] = (uint64_t)dst_leaf;  /* No flags — raw PA */

            for (int k = 0; k < LA_PT_ENTRIES; k++) {
                if (!(src_leaf[k] & LA_PTE_V)) continue;

                /* Allocate new data page and copy */
                void *new_page = la_pmem_alloc();
                if (!new_page) return -1;

                uint64_t old_pa = LA_PTE_PPN(src_leaf[k]);
                uint64_t perm   = src_leaf[k] & 0xFFFUL;

                /* Copy page data */
                const uint8_t *s = (const uint8_t *)old_pa;
                uint8_t *d = (uint8_t *)new_page;
                for (int b = 0; b < (int)LA_PGSIZE; b++)
                    d[b] = s[b];

                dst_leaf[k] = LA_MK_PTE((uint64_t)new_page, perm);
            }
        }
    }
    return 0;
}

/* ---- Free an entire user page table and every page it maps ----
 *
 * Walks the 3-level table exactly like la_uvm_copy_pgtbl: root → mid → leaf.
 * Directory entries are raw PAs (no flags); a leaf entry with V set is a data
 * page whose PPN is the data page PA.  We free every mapped data page, then
 * each leaf table, each mid table, and finally the root.
 *
 * SAFE because this kernel's fork DEEP-COPIES the page table (no shared/COW
 * pages) — every page under `root` is privately owned by exactly one process.
 * The caller must guarantee `root` is not the active page table of any running
 * context and that stale TLB entries are dropped before the freed pages are
 * reused (the scheduler invalidates the whole TLB before entering the next
 * user process; exec invalidates before installing the new image). */
void la_uvm_free_pgtbl(uint64_t *root)
{
    if (!root) return;

    for (int i = 0; i < LA_PT_ENTRIES; i++) {
        uint64_t e0 = root[i];
        if (!e0) continue;
        uint64_t *mid = (uint64_t *)e0;

        for (int j = 0; j < LA_PT_ENTRIES; j++) {
            uint64_t e1 = mid[j];
            if (!e1) continue;
            uint64_t *leaf = (uint64_t *)e1;

            for (int k = 0; k < LA_PT_ENTRIES; k++) {
                uint64_t e2 = leaf[k];
                if (e2 & LA_PTE_V)
                    la_pmem_free((void *)LA_PTE_PPN(e2));   /* data page */
            }
            la_pmem_free(leaf);   /* leaf table page */
        }
        la_pmem_free(mid);   /* mid table page */
    }
    la_pmem_free(root);   /* root table page */
}
