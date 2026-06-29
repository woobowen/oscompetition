/*
 * LoongArch kernel virtual memory setup.
 *
 * Sets up DMW (Direct Mapping Windows) so that kernel code remains
 * accessible even after DA=0 (paging enabled).  This allows us to
 * switch DA=0 right before ertn to user mode without needing full
 * kernel page tables.
 *
 * DMW0 maps virtual-segment 0 (addresses with bits[63:60]=0) to
 * physical memory as an identity mapping, accessible from PLV0 only.
 * User-mode (PLV3) addresses go through the TLB instead.
 */
#include "early_boot.h"

/* DMW CSR addresses */
#define LA_CSR_DMW0  0x180
#define LA_CSR_DMW1  0x181

void la_kvm_init(void)
{
    /*
     * DMW0: identity mapping for VSEG=0, PLV0 access only.
     *
     * Format: [0]=PLV0, [1]=PLV1, [2]=PLV2, [3]=PLV3,
     *         [5:4]=MAT, [63:60]=VSEG
     *
     * MAT=1 (coherent), PLV0=1, VSEG=0
     */
    uint64_t dmw0 = (1UL << 0)   /* PLV0 access */
                  | (1UL << 4);   /* MAT = coherent */
    la_csr_write(dmw0, LA_CSR_DMW0);

    /* DMW1 maps high-RAM kernel virtual addresses (VSEG=9) to physical
     * high memory.  Keep it PLV0-only; user mappings still go through TLB. */
    uint64_t dmw1 = (1UL << 0)
                  | (1UL << 4)
                  | (9UL << 60);
    la_csr_write(dmw1, LA_CSR_DMW1);

    la_uart_puts("  kvm: DMW0 low + DMW1 high mapped for PLV0\n");
}
