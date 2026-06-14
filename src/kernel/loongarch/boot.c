/*
 * LoongArch kernel boot entry.
 *
 * Called from entry.S after SP and exception vector are set up.
 * Initialises subsystems, creates the first user process,
 * then enters the round-robin scheduler (never returns).
 */
#include "early_boot.h"
#include "proc.h"

/* ---- early UART I/O (polled, no interrupts) ---- */

void la_uart_putc(char c)
{
    volatile unsigned char *uart = (volatile unsigned char *)LA_UART_BASE;
    *uart = (unsigned char)c;
}

void la_uart_puts(const char *s)
{
    while (*s) {
        la_uart_putc(*s);
        s++;
    }
}

void la_uart_put_hex(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";

    la_uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        la_uart_putc(hex[(value >> shift) & 0xfU]);
    }
}

/* ---- main boot sequence ---- */

void la_boot_main(void)
{
    la_uart_puts("loongarch boot start\n");

    /* 1  Physical memory */
    la_uart_puts("[init] pmem\n");
    la_pmem_init();

    /* 2  Kernel virtual memory (stub — paging off for kernel) */
    la_uart_puts("[init] kvm\n");
    la_kvm_init();

    /* 3  Initialize paging CSRs for future user-mode use */
    la_uart_puts("[init] paging\n");
    la_uvm_paging_init();

    /* 3b TLB: configure STLB page size (must match TLBIDX.PS) */
    la_uart_puts("[init] tlb\n");
    la_tlb_init();

    /* 4  Stable timer */
    la_uart_puts("[init] timer\n");
    la_timer_init();

    /* 5  Process table */
    la_uart_puts("[init] proc\n");
    la_proc_init();

    /* 6  Enable global interrupts (CRMD.IE = 1) */
    la_uart_puts("[init] enabling interrupts\n");
    uint64_t crmd = la_csr_read(LA_CSR_CRMD);
    crmd |= LA_CRMD_IE;
    la_csr_write(crmd, LA_CSR_CRMD);

    la_uart_puts("[init] kernel ready\n");

    /* 7  VirtIO PCI block device */
    la_uart_puts("[init] virtio\n");
    la_virtio_init();

    /* 7b  Initialise buffer cache (before filesystem) */
    bio_init();

    /* 8  Mount filesystem (needed for exec) */
    la_uart_puts("[init] fs\n");
    if (la_fs_init() != 0)
        la_uart_puts("[init] fs: mount failed (no disk?)\n");
    else
        la_uart_puts("[init] fs: mounted\n");

    /* 8b  Initialise writable memory filesystem */
    memfs_init();

    /* 8c  Initialise loopback socket layer */
    la_socket_init();

    /* 9  Create first user process (initcode) */
    la_uart_puts("[init] creating first user process\n");
    la_proc_make_first();

    /* 10  Enter scheduler — never returns */
    la_uart_puts("[init] entering scheduler\n");
    la_scheduler();
}
