#include "src/kernel/loongarch/trap_layout.h"

# LoongArch kernel-to-user return.
#
# Strategy:
#   1. DMW0 (set up in kvm.c) provides identity mapping for kernel code
#      even when DA=0.  So we can safely set DA=0 while still in kernel mode.
#   2. Set DA=0 in CRMD (paging enabled).
#   3. Set PRMD for PLV3 return.
#   4. ertn — switches to user mode (PLV3), TLB entries are used for
#      user address translation.
#
# a0 = struct la_trap_frame *

.section .text.userret, "ax"
.globl la_user_return
.type la_user_return, @function
la_user_return:
        # ---- Set ERA (return PC) ----
        ld.d    $t0, $a0, LA_TF_ERA
        csrwr   $t0, LA_CSR_ERA

        # ---- Set PRMD for PLV3 return ----
        addi.d  $t0, $zero, 0x7        # PPLV=3 | PIE=1
        csrwr   $t0, LA_CSR_PRMD

        # (DA=0 already set by proc_return before calling us)

        # ---- Restore all GPRs from trap frame ----
        ld.d    $r1,  $a0, LA_TF_GPR_RA
        ld.d    $r2,  $a0, LA_TF_GPR_TP
        ld.d    $r3,  $a0, LA_TF_GPR_SP
        ld.d    $r5,  $a0, LA_TF_GPR_A1
        ld.d    $r6,  $a0, LA_TF_GPR_A2
        ld.d    $r7,  $a0, LA_TF_GPR_A3
        ld.d    $r8,  $a0, LA_TF_GPR_A4
        ld.d    $r9,  $a0, LA_TF_GPR_A5
        ld.d    $r10, $a0, LA_TF_GPR_A6
        ld.d    $r11, $a0, LA_TF_GPR_A7
        ld.d    $r12, $a0, LA_TF_GPR_T0
        ld.d    $r13, $a0, LA_TF_GPR_T1
        ld.d    $r14, $a0, LA_TF_GPR_T2
        ld.d    $r15, $a0, LA_TF_GPR_T3
        ld.d    $r16, $a0, LA_TF_GPR_T4
        ld.d    $r17, $a0, LA_TF_GPR_T5
        ld.d    $r18, $a0, LA_TF_GPR_T6
        ld.d    $r19, $a0, LA_TF_GPR_T7
        ld.d    $r20, $a0, LA_TF_GPR_T8
        ld.d    $r22, $a0, LA_TF_GPR_FP
        ld.d    $r23, $a0, LA_TF_GPR_S0
        ld.d    $r24, $a0, LA_TF_GPR_S1
        ld.d    $r25, $a0, LA_TF_GPR_S2
        ld.d    $r26, $a0, LA_TF_GPR_S3
        ld.d    $r27, $a0, LA_TF_GPR_S4
        ld.d    $r28, $a0, LA_TF_GPR_S5
        ld.d    $r29, $a0, LA_TF_GPR_S6
        ld.d    $r30, $a0, LA_TF_GPR_S7
        ld.d    $r31, $a0, LA_TF_GPR_S8

        # ---- Restore $a0 LAST ----
        ld.d    $r4,  $a0, LA_TF_GPR_A0

        # ---- Return to user mode ----
        # ertn: CRMD.PLV ← PRMD.PPLV = 3, CRMD.IE ← PRMD.PIE = 1
        # DA stays 0, PG stays enabled → TLB used for user addresses
        ertn
.size la_user_return, . - la_user_return
