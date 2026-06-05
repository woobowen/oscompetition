#ifndef SEAOS_LOONGARCH_TRAP_H
#define SEAOS_LOONGARCH_TRAP_H

#include <stdint.h>

#include "trap_layout.h"

#define LA_ESTAT_ECODE_SHIFT 16
#define LA_ESTAT_ECODE_MASK 0x3fULL
#define LA_SYSCALL_INSN_SIZE 4ULL

enum la_exception_code {
    LA_ECODE_INT = 0x0,
    LA_ECODE_ADEF = 0x8,
    LA_ECODE_ALE = 0x9,
    LA_ECODE_SYS = 0xb,
    LA_ECODE_BRK = 0xc,
    LA_ECODE_INE = 0xd,
    LA_ECODE_IPE = 0xe,
};

enum la_gpr_index {
    LA_GPR_ZERO = 0,
    LA_GPR_RA = 1,
    LA_GPR_TP = 2,
    LA_GPR_SP = 3,
    LA_GPR_A0 = 4,
    LA_GPR_A1 = 5,
    LA_GPR_A2 = 6,
    LA_GPR_A3 = 7,
    LA_GPR_A4 = 8,
    LA_GPR_A5 = 9,
    LA_GPR_A6 = 10,
    LA_GPR_A7 = 11,
    LA_GPR_T0 = 12,
    LA_GPR_T8 = 20,
    LA_GPR_FP = 22,
    LA_GPR_S0 = 23,
    LA_GPR_S8 = 31,
};

struct la_trap_frame {
    uint64_t gpr[32];
    uint64_t era;
    uint64_t badv;
    uint64_t estat;
};

static inline uint64_t la_estat_ecode(uint64_t estat)
{
    return (estat >> LA_ESTAT_ECODE_SHIFT) & LA_ESTAT_ECODE_MASK;
}

#endif
