#include "src/kernel/loongarch/trap_layout.h"

# LoongArch general exception entry scaffold.

.section .text.trap, "ax"
.align 12
.globl la_exception_entry
.type la_exception_entry, @function
la_exception_entry:
        addi.d  $sp, $sp, -LA_TF_SIZE

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
        st.d    $r12, $sp, LA_TF_GPR_T0
        st.d    $r13, $sp, LA_TF_GPR_T1
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

        addi.d  $r12, $sp, LA_TF_SIZE
        st.d    $r12, $sp, LA_TF_GPR_SP

        csrrd   $r12, LA_CSR_ERA
        st.d    $r12, $sp, LA_TF_ERA
        csrrd   $r12, LA_CSR_BADV
        st.d    $r12, $sp, LA_TF_BADV
        csrrd   $r12, LA_CSR_ESTAT
        st.d    $r12, $sp, LA_TF_ESTAT

        move    $a0, $sp
        bl      la_trap_dispatch

        ld.d    $r12, $sp, LA_TF_ERA
        csrwr   $r12, LA_CSR_ERA

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
