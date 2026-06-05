#include "early_boot.h"

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

void la_boot_main(void)
{
    la_uart_puts(LA_EARLY_BOOT_LOG);

    for (;;) {
    }
}
