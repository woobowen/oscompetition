#include "trap.h"

#define LA_ENOSYS 38

uint64_t la_syscall_dispatch(struct la_trap_frame *tf)
{
    uint64_t ret = (uint64_t)(-LA_ENOSYS);

    if (tf != 0) {
        tf->gpr[LA_GPR_A0] = ret;
    }

    return ret;
}
