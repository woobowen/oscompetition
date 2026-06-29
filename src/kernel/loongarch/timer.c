/*
 * LoongArch stable-timer driver.
 *
 * QEMU LoongArch virt provides a 100 MHz constant-frequency timer.
 *
 * TCFG layout (from QEMU source cpu-csr.h):
 *   bit 0     EN       – enable
 *   bit 1     PERIODIC – periodic mode
 *   bits 47:2 INIT_VAL – countdown period (shifted left by 2)
 *
 * TVAL (CSR 0x42) is READ-ONLY: remaining ticks until next interrupt.
 * TICLR (CSR 0x44): write 1 to clear the timer interrupt pending bit.
 */
#include "early_boot.h"
#include "proc.h"

static uint64_t la_ticks;

/* ---- one-time hardware setup ---- */
void la_timer_init(void)
{
    /* Disable timer first */
    la_csr_write(0, LA_CSR_TCFG);

    /*
     * Build TCFG value:
     *   EN | PERIODIC | (interval << 2)
     * INIT_VAL occupies bits [47:2], so the raw interval
     * must be shifted left by 2.
     */
    uint64_t tcfg = LA_TCFG_EN
                  | LA_TCFG_PERIODIC
                  | ((uint64_t)LA_TIMER_INTERVAL << LA_TCFG_INIT_SHIFT);
    la_csr_write(tcfg, LA_CSR_TCFG);

    /* Enable timer local interrupt in ECFG (bit 11) */
    uint64_t ecfg = la_csr_read(LA_CSR_ECFG);
    ecfg |= LA_ECFG_TIMER_EN;
    la_csr_write(ecfg, LA_CSR_ECFG);

    la_uart_puts("  timer: periodic 100 Hz enabled\n");
}

/* ---- called from trap_dispatch on every timer tick ---- */
extern int la_tlb_refill_count;

void la_timer_interrupt(void)
{
    /* Acknowledge: write 1 to TICLR to clear the pending bit */
    la_csr_write(1, LA_CSR_TICLR);

    la_ticks++;
}

uint64_t la_timer_get_ticks(void)
{
    return la_ticks;
}

uint64_t la_timer_get_counter(void)
{
    uint64_t counter;
    uint64_t counter_id;
    asm volatile("rdtime.d %0, %1" : "=r"(counter), "=r"(counter_id));
    (void)counter_id;
    return counter;
}
