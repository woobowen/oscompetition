# LoongArch early entry for the source-built kernel-la path.

#include "src/kernel/loongarch/trap_layout.h"

.section .text.entry, "ax"
.globl _entry
.type _entry, @function
_entry:
        la.local $sp, la_boot_stack_top

        # General exceptions use CSR.EENTRY when CSR.ECFG.VS is zero.
        move    $t0, $zero
        csrwr   $t0, 0x4
        la.local $t0, la_exception_entry
        csrwr   $t0, 0xc

        # TLB refill exceptions use a tiny wrapper that marks TLBRERA.ISTLBR
        # before entering the shared exception path.
        la.local $t0, la_tlb_refill_entry
        csrwr   $t0, LA_CSR_TLBRENTRY

        bl la_boot_main

1:
        b 1b
.size _entry, . - _entry
