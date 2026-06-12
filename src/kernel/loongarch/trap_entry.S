#include "src/kernel/loongarch/trap_layout.h"

# LoongArch general exception entry.
#
# Handles both kernel-mode traps (timer interrupts) and user-mode traps
# (syscalls, faults).  When coming from user mode we must switch to the
# process's kernel stack; when coming from kernel mode we use the current SP.

.section .text.trap, "ax"
.align 12
.globl la_exception_entry
.type la_exception_entry, @function
la_exception_entry:
        csrwr   $t0, LA_CSR_KS0
        csrwr   $t1, LA_CSR_KS1

        # ---- Check if TLB refill exception ----
        # TLB refill updates TLBRERA/TLBRPRMD but NOT ERA/PRMD/ESTAT.
        # If ISTLBR is set, copy TLBRPRMD→PRMD so the rest of the
        # handler can use PRMD.PPLV to distinguish user vs kernel.
        csrrd   $t0, LA_CSR_TLBRERA
        andi    $t0, $t0, 1             # ISTLBR bit
        beqz    $t0, .Lnot_tlb_refill
        csrrd   $t0, LA_CSR_TLBRPRMD
        csrwr   $t0, LA_CSR_PRMD
.Lnot_tlb_refill:

        # ---- Check where we came from ----
        csrrd   $t0, LA_CSR_PRMD
        andi    $t0, $t0, 0x3           # PRMD.PPLV
        beqz    $t0, .Lfrom_kernel      # PLV0 → kernel mode

.Lfrom_user:
        # Came from user mode (PLV3).
        # $sp is the user stack pointer — switch to kernel stack.
        move    $t0, $sp                # t0 = user SP (save temporarily)

        la.pcrel $t1, la_trap_ksp       # load saved kernel SP
        ld.d    $sp, $t1, 0

        addi.d  $sp, $sp, -LA_TF_SIZE   # allocate trap frame
        st.d    $t0, $sp, LA_TF_GPR_SP  # save user SP
        b       .Lsave_regs

.Lfrom_kernel:
        # Came from kernel mode — SP is already the kernel stack.
        addi.d  $t0, $sp, 0
        addi.d  $sp, $sp, -LA_TF_SIZE
        st.d    $t0, $sp, LA_TF_GPR_SP  # save pre-trap SP

.Lsave_regs:
        # ---- Save all GPRs ----
        st.d    $r1,  $sp, LA_TF_GPR_RA
        st.d    $r2,  $sp, LA_TF_GPR_TP
        st.d    $r4,  $sp, LA_TF_GPR_A0
        st.d    $r5,  $sp, LA_TF_GPR_A1
        st.d    $r6,  $sp, LA_TF_GPR_A2
        st.d    $r7,  $sp, LA_TF_GPR_A3
        st.d    $r8,  $sp, LA_TF_GPR_A4
        st.d    $r9,  $sp, LA_TF_GPR_A5
        st.d    $r10, $sp, LA_TF_GPR_A6
        st.d    $r11, $sp, LA_TF_GPR_A7
        
        csrrd   $t0, LA_CSR_KS0
        csrrd   $t1, LA_CSR_KS1
        st.d    $t0, $sp, LA_TF_GPR_T0
        st.d    $t1, $sp, LA_TF_GPR_T1

        st.d    $r14, $sp, LA_TF_GPR_T2
        st.d    $r15, $sp, LA_TF_GPR_T3
        st.d    $r16, $sp, LA_TF_GPR_T4
        st.d    $r17, $sp, LA_TF_GPR_T5
        st.d    $r18, $sp, LA_TF_GPR_T6
        st.d    $r19, $sp, LA_TF_GPR_T7
        st.d    $r20, $sp, LA_TF_GPR_T8
        st.d    $r22, $sp, LA_TF_GPR_FP
        st.d    $r23, $sp, LA_TF_GPR_S0
        st.d    $r24, $sp, LA_TF_GPR_S1
        st.d    $r25, $sp, LA_TF_GPR_S2
        st.d    $r26, $sp, LA_TF_GPR_S3
        st.d    $r27, $sp, LA_TF_GPR_S4
        st.d    $r28, $sp, LA_TF_GPR_S5
        st.d    $r29, $sp, LA_TF_GPR_S6
        st.d    $r30, $sp, LA_TF_GPR_S7
        st.d    $r31, $sp, LA_TF_GPR_S8

        # ---- Save CSRs ----
        csrrd   $r12, LA_CSR_ERA
        st.d    $r12, $sp, LA_TF_ERA
        csrrd   $r12, LA_CSR_BADV
        st.d    $r12, $sp, LA_TF_BADV
        csrrd   $r12, LA_CSR_ESTAT
        st.d    $r12, $sp, LA_TF_ESTAT

        # ---- Call C handler ----
        move    $a0, $sp
        bl      la_trap_dispatch

        # ---- Restore CSRs ----
        ld.d    $r12, $sp, LA_TF_ERA
        csrwr   $r12, LA_CSR_ERA

        # ---- Check if returning to user mode ----
        csrrd   $t0, LA_CSR_PRMD
        andi    $t0, $t0, 0x3           # PRMD.PPLV
        beqz    $t0, .Lrestore_kernel   # returning to PLV0 → kernel

.Lreturn_user:
        # Returning to user mode (PLV3).
        # NOTE: Skip PGDL write — it's already set by la_uvm_switch().
        # Writing PGDL in QEMU may flush the TLB, destroying entries
        # we just filled in the TLB refill handler.

        # Save kernel SP for the next user→kernel trap.
        # SP currently points to the trap frame; kernel SP is above it.
        addi.d  $t0, $sp, LA_TF_SIZE
        la.pcrel $t1, la_trap_ksp
        st.d    $t0, $t1, 0

.Lrestore_kernel:
        # ---- Restore all GPRs ----
        ld.d    $r1,  $sp, LA_TF_GPR_RA
        ld.d    $r2,  $sp, LA_TF_GPR_TP
        ld.d    $r4,  $sp, LA_TF_GPR_A0
        ld.d    $r5,  $sp, LA_TF_GPR_A1
        ld.d    $r6,  $sp, LA_TF_GPR_A2
        ld.d    $r7,  $sp, LA_TF_GPR_A3
        ld.d    $r8,  $sp, LA_TF_GPR_A4
        ld.d    $r9,  $sp, LA_TF_GPR_A5
        ld.d    $r10, $sp, LA_TF_GPR_A6
        ld.d    $r11, $sp, LA_TF_GPR_A7
        ld.d    $r12, $sp, LA_TF_GPR_T0
        ld.d    $r13, $sp, LA_TF_GPR_T1
        ld.d    $r14, $sp, LA_TF_GPR_T2
        ld.d    $r15, $sp, LA_TF_GPR_T3
        ld.d    $r16, $sp, LA_TF_GPR_T4
        ld.d    $r17, $sp, LA_TF_GPR_T5
        ld.d    $r18, $sp, LA_TF_GPR_T6
        ld.d    $r19, $sp, LA_TF_GPR_T7
        ld.d    $r20, $sp, LA_TF_GPR_T8
        ld.d    $r22, $sp, LA_TF_GPR_FP
        ld.d    $r23, $sp, LA_TF_GPR_S0
        ld.d    $r24, $sp, LA_TF_GPR_S1
        ld.d    $r25, $sp, LA_TF_GPR_S2
        ld.d    $r26, $sp, LA_TF_GPR_S3
        ld.d    $r27, $sp, LA_TF_GPR_S4
        ld.d    $r28, $sp, LA_TF_GPR_S5
        ld.d    $r29, $sp, LA_TF_GPR_S6
        ld.d    $r30, $sp, LA_TF_GPR_S7
        ld.d    $r31, $sp, LA_TF_GPR_S8
        ld.d    $sp,  $sp, LA_TF_GPR_SP
        ertn
.size la_exception_entry, . - la_exception_entry
