#include "src/kernel/loongarch/trap_layout.h"

/*
 * LoongArch kernel context switch.
 *
 * void la_swtch(struct la_context *old, struct la_context *new);
 *   a0 = old context, a1 = new context
 *
 * Saves callee-saved registers (ra, sp, fp, s0-s8) into *old,
 * restores them from *new, then jumps to new.ra.
 */
.section .text.swtch, "ax"
.globl la_swtch
.type la_swtch, @function
la_swtch:
        /* ---- save callee-saved registers to old ---- */
        st.d    $r1,  $a0, LA_CTX_RA        /* ra  */
        st.d    $r3,  $a0, LA_CTX_SP        /* sp  */
        st.d    $r22, $a0, LA_CTX_FP        /* fp  */
        st.d    $r23, $a0, LA_CTX_S0        /* s0  */
        st.d    $r24, $a0, LA_CTX_S1        /* s1  */
        st.d    $r25, $a0, LA_CTX_S2        /* s2  */
        st.d    $r26, $a0, LA_CTX_S3        /* s3  */
        st.d    $r27, $a0, LA_CTX_S4        /* s4  */
        st.d    $r28, $a0, LA_CTX_S5        /* s5  */
        st.d    $r29, $a0, LA_CTX_S6        /* s6  */
        st.d    $r30, $a0, LA_CTX_S7        /* s7  */
        st.d    $r31, $a0, LA_CTX_S8        /* s8  */

        /* ---- load callee-saved registers from new ---- */
        ld.d    $r1,  $a1, LA_CTX_RA
        ld.d    $r3,  $a1, LA_CTX_SP
        ld.d    $r22, $a1, LA_CTX_FP
        ld.d    $r23, $a1, LA_CTX_S0
        ld.d    $r24, $a1, LA_CTX_S1
        ld.d    $r25, $a1, LA_CTX_S2
        ld.d    $r26, $a1, LA_CTX_S3
        ld.d    $r27, $a1, LA_CTX_S4
        ld.d    $r28, $a1, LA_CTX_S5
        ld.d    $r29, $a1, LA_CTX_S6
        ld.d    $r30, $a1, LA_CTX_S7
        ld.d    $r31, $a1, LA_CTX_S8

        /* return to new context's ra */
        jr      $ra
.size la_swtch, . - la_swtch
