#include "early_boot.h"
#include "trap.h"

void la_user_return_log_checkpoint(void)
{
    la_uart_puts("la_userret: era prmd gpr restore scaffold staged\n");
}
