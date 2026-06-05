# LoongArch early entry for the source-built kernel-la path.

.section .text.entry, "ax"
.globl _entry
.type _entry, @function
_entry:
        la.local $sp, la_boot_stack_top
        bl la_boot_main

1:
        b 1b
.size _entry, . - _entry
