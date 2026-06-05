# LoongArch general exception entry scaffold.

.section .text.trap, "ax"
.align 12
.globl la_exception_entry
.type la_exception_entry, @function
la_exception_entry:
        csrrd   $a0, 0x6        # CSR.ERA
        csrrd   $a1, 0x7        # CSR.BADV
        csrrd   $a2, 0x5        # CSR.ESTAT
        bl      la_trap_dispatch

1:
        b 1b
.size la_exception_entry, . - la_exception_entry
