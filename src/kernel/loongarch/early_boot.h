#ifndef SEAOS_LOONGARCH_EARLY_BOOT_H
#define SEAOS_LOONGARCH_EARLY_BOOT_H

#include <stdint.h>

#define LA_KERNEL_ENTRY 0x200000ULL
#define LA_TEXT_OFFSET 0x1000ULL
#define LA_UART_BASE 0x1fe001e0U

#define LA_EARLY_BOOT_LOG \
    "loongarch boot start\n" \
    "la_entry: qemu virt early console online\n" \
    "la_boot_main: arch scaffold active\n" \
    "la_trap: full trapframe syscall return scaffold staged\n" \
    "la_userret: era prmd gpr restore scaffold staged\n" \
    "la_proc: trapframe init userret bridge staged\n" \
    "la_boot_main: next context switch virtio-pci ext4\n"

void la_uart_putc(char c);
void la_uart_puts(const char *s);
void la_uart_put_hex(uint64_t value);

#endif
