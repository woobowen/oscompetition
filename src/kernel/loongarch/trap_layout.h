#ifndef SEAOS_LOONGARCH_TRAP_LAYOUT_H
#define SEAOS_LOONGARCH_TRAP_LAYOUT_H

#define LA_CSR_ECFG 0x4
#define LA_CSR_PRMD 0x1
#define LA_CSR_ESTAT 0x5
#define LA_CSR_ERA 0x6
#define LA_CSR_BADV 0x7
#define LA_CSR_EENTRY 0xc

#define LA_PRMD_PPLV_MASK 0x3
#define LA_PRMD_PIE 0x4
#define LA_USER_PLV 0x3

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

#endif
