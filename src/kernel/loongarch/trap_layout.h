#ifndef SEAOS_LOONGARCH_TRAP_LAYOUT_H
#define SEAOS_LOONGARCH_TRAP_LAYOUT_H

/*
 * NOTE: This header is included from both C and assembly (.S) files.
 *       All constant definitions must be plain integer literals (no U/ULL).
 */

/* ---- Control / Status Registers ---- */
#define LA_CSR_CRMD   0x0
#define LA_CSR_PRMD   0x1
#define LA_CSR_ECFG   0x4
#define LA_CSR_ESTAT  0x5
#define LA_CSR_ERA    0x6
#define LA_CSR_BADV   0x7
#define LA_CSR_EENTRY 0xc

/* Timer CSRs */
#define LA_CSR_TCFG   0x41
#define LA_CSR_TVAL   0x42
#define LA_CSR_TICLR  0x44

/* Paging CSRs (LoongArch Architecture Reference Manual) */
#define LA_CSR_TLBIDX  0x10
#define LA_CSR_TLBEHI  0x11
#define LA_CSR_TLBELO0 0x12
#define LA_CSR_TLBELO1 0x13
#define LA_CSR_ASID    0x18
#define LA_CSR_PGDL    0x19
#define LA_CSR_PGDH    0x1A
#define LA_CSR_PGD     0x1B
#define LA_CSR_PWCL    0x1C
#define LA_CSR_PWCH    0x1D
#define LA_CSR_STLBPS  0x1E
#define LA_CSR_RVACFG  0x1F

/* TLB Refill exception CSRs */
#define LA_CSR_TLBRENTRY 0x88
#define LA_CSR_TLBRBADV  0x89
#define LA_CSR_TLBRERA   0x8a
#define LA_CSR_TLBRSAVE  0x8b
#define LA_CSR_TLBRELO0  0x8c
#define LA_CSR_TLBRELO1  0x8d
#define LA_CSR_TLBREHI   0x8e
#define LA_CSR_TLBRPRMD  0x8f

/* TLBRERA bits */
#define LA_TLBRERA_ISTLBR  1

/* TLBRPRMD bits (same layout as PRMD: PPLV[1:0], PIE[2]) */
#define LA_TLBRPRMD_PPLV_SHIFT 0
#define LA_TLBRPRMD_PIE        4

/* ---- CRMD bits ---- */
#define LA_CRMD_PLV_SHIFT 0
#define LA_CRMD_PLV_MASK  0x3
#define LA_CRMD_IE        (1 << 2)
#define LA_CRMD_DA        (1 << 3)
#define LA_CRMD_PG        (1 << 4)

/* ---- PRMD bits ---- */
#define LA_PRMD_PPLV_MASK 0x3
#define LA_PRMD_PIE       0x4
#define LA_USER_PLV       0x3

/* ---- TCFG bits (QEMU LoongArch layout) ---- */
#define LA_TCFG_EN         (1 << 0)     /* bit 0:  Enable */
#define LA_TCFG_PERIODIC   (1 << 1)     /* bit 1:  Periodic mode */
#define LA_TCFG_INIT_SHIFT 2            /* INIT_VAL occupies bits [47:2] */

/* ---- ECFG timer interrupt enable ---- */
#define LA_ECFG_TIMER_EN   (1 << 11)   /* bit 11: Timer (IS[11]) */

/* ---- ESTAT IS bits (bits [15:0]) ---- */
#define LA_ESTAT_IS_TIMER  (1 << 11)   /* stable timer pending */

/* ---- Trap-frame layout (byte offsets) ---- */
#define LA_TF_GPR_BASE 0
#define LA_TF_GPR_ZERO 0
#define LA_TF_GPR_RA 8
#define LA_TF_GPR_TP 16
#define LA_TF_GPR_SP 24
#define LA_TF_GPR_A0 32
#define LA_TF_GPR_A1 40
#define LA_TF_GPR_A2 48
#define LA_TF_GPR_A3 56
#define LA_TF_GPR_A4 64
#define LA_TF_GPR_A5 72
#define LA_TF_GPR_A6 80
#define LA_TF_GPR_A7 88
#define LA_TF_GPR_T0 96
#define LA_TF_GPR_T1 104
#define LA_TF_GPR_T2 112
#define LA_TF_GPR_T3 120
#define LA_TF_GPR_T4 128
#define LA_TF_GPR_T5 136
#define LA_TF_GPR_T6 144
#define LA_TF_GPR_T7 152
#define LA_TF_GPR_T8 160
#define LA_TF_GPR_FP 176
#define LA_TF_GPR_S0 184
#define LA_TF_GPR_S1 192
#define LA_TF_GPR_S2 200
#define LA_TF_GPR_S3 208
#define LA_TF_GPR_S4 216
#define LA_TF_GPR_S5 224
#define LA_TF_GPR_S6 232
#define LA_TF_GPR_S7 240
#define LA_TF_GPR_S8 248
#define LA_TF_ERA 256
#define LA_TF_BADV 264
#define LA_TF_ESTAT 272
#define LA_TF_SIZE 288

/* ---- Kernel context layout (for swtch.S) ---- */
#define LA_CTX_RA   0
#define LA_CTX_SP   8
#define LA_CTX_FP   16
#define LA_CTX_S0   24
#define LA_CTX_S1   32
#define LA_CTX_S2   40
#define LA_CTX_S3   48
#define LA_CTX_S4   56
#define LA_CTX_S5   64
#define LA_CTX_S6   72
#define LA_CTX_S7   80
#define LA_CTX_S8   88
#define LA_CTX_SIZE 96

#endif
