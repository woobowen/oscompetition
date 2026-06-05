#include "early_boot.h"

static void la_uart_putc(char c)
{
    volatile unsigned char *uart = (volatile unsigned char *)LA_UART_BASE;
    *uart = (unsigned char)c;
}

static void la_uart_puts(const char *s)
{
    while (*s) {
        la_uart_putc(*s);
        s++;
    }
}

void la_boot_main(void)
{
    la_uart_puts(LA_EARLY_BOOT_LOG);

    for (;;) {
    }
}
