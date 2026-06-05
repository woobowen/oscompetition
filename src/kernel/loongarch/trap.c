#include "early_boot.h"
#include "trap.h"

uint64_t la_syscall_dispatch(struct la_trap_frame *tf);

void la_trap_dispatch(uint64_t era, uint64_t badv, uint64_t estat)
{
    uint64_t ecode = la_estat_ecode(estat);

    la_uart_puts("la_trap: ecode=");
    la_uart_put_hex(ecode);
    la_uart_puts(" era=");
    la_uart_put_hex(era);
    la_uart_puts(" badv=");
    la_uart_put_hex(badv);
    la_uart_puts("\n");

    if (ecode == LA_ECODE_SYS) {
        struct la_trap_frame tf = {
            .era = era,
            .badv = badv,
            .estat = estat,
        };
        uint64_t ret = la_syscall_dispatch(&tf);
        la_uart_puts("la_syscall: ret=");
        la_uart_put_hex(ret);
        la_uart_puts("\n");
    }

    for (;;) {
    }
}
