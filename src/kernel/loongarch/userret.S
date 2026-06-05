#include "src/kernel/loongarch/trap_layout.h"

# LoongArch kernel-to-user return scaffold.
# a0 = struct la_trap_frame *

.section .text.userret, "ax"
.globl la_user_return
.type la_user_return, @function
la_user_return:
        ld.d    $t0, $a0, LA_TF_ERA
        csrwr   $t0, LA_CSR_ERA

        addi.d  $t0, $zero, (LA_USER_PLV | LA_PRMD_PIE)
        csrwr   $t0, LA_CSR_PRMD

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
        ld.d    $r4,  $a0, LA_TF_GPR_A0
        ertn
.size la_user_return, . - la_user_return
