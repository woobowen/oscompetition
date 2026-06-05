#include "early_boot.h"
#include "proc.h"

static void la_trap_frame_clear(struct la_trap_frame *tf)
{
    for (int i = 0; i < 32; i++) {
        tf->gpr[i] = 0;
    }

    tf->era = 0;
    tf->badv = 0;
    tf->estat = 0;
}

void la_trap_frame_init_user(struct la_trap_frame *tf,
                             const struct la_user_entry *entry)
{
    if (tf == 0 || entry == 0) {
        return;
    }

    la_trap_frame_clear(tf);
    tf->era = entry->entry;
    tf->gpr[LA_GPR_SP] = entry->sp;
    tf->gpr[LA_GPR_A0] = entry->argc;
    tf->gpr[LA_GPR_A1] = entry->argv;
}

void la_proc_return(struct la_trap_frame *tf)
{
    if (tf == 0) {
        la_uart_puts("la_proc_return: missing trap frame\n");
        for (;;) {
        }
    }

    la_user_return(tf);

    for (;;) {
    }
}

void la_proc_log_checkpoint(void)
{
    la_uart_puts("la_proc: trapframe init userret bridge staged\n");
}
