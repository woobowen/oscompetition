#ifndef SEAOS_LOONGARCH_PROC_H
#define SEAOS_LOONGARCH_PROC_H

#include <stdint.h>

#include "trap.h"

struct la_user_entry {
    uint64_t entry;
    uint64_t sp;
    uint64_t argc;
    uint64_t argv;
};

void la_trap_frame_init_user(struct la_trap_frame *tf,
                             const struct la_user_entry *entry);
void la_proc_return(struct la_trap_frame *tf);
void la_proc_log_checkpoint(void);

#endif
