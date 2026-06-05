#include "early_boot.h"
#include "trap.h"

uint64_t la_syscall_dispatch(struct la_trap_frame *tf);

void la_trap_dispatch(struct la_trap_frame *tf)
{
    uint64_t ecode = la_estat_ecode(tf->estat);

    la_uart_puts("la_trap: ecode=");
    la_uart_put_hex(ecode);
    la_uart_puts(" era=");
    la_uart_put_hex(tf->era);
    la_uart_puts(" badv=");
    la_uart_put_hex(tf->badv);
    la_uart_puts("\n");

    if (ecode == LA_ECODE_SYS) {
        uint64_t ret = la_syscall_dispatch(tf);
        la_uart_puts("la_syscall: ret=");
        la_uart_put_hex(ret);
        la_uart_puts("\n");
        tf->era += LA_SYSCALL_INSN_SIZE;
        return;
    }

    for (;;) {
    }
}
