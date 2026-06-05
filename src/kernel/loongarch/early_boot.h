#ifndef SEAOS_LOONGARCH_EARLY_BOOT_H
#define SEAOS_LOONGARCH_EARLY_BOOT_H

#define LA_KERNEL_ENTRY 0x200000ULL
#define LA_TEXT_OFFSET 0x1000ULL
#define LA_UART_BASE 0x1fe001e0U

#define LA_EARLY_BOOT_LOG \
    "loongarch boot start\n" \
    "la_entry: qemu virt early console online\n" \
    "la_boot_main: arch scaffold active\n" \
    "la_boot_main: next trap syscall userret virtio-pci ext4\n"

#endif
