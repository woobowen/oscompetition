#ifndef SEAOS_LOONGARCH_PROC_H
#define SEAOS_LOONGARCH_PROC_H

#include <stdint.h>
#include "trap.h"

#define LA_NPROC        16
#define LA_KSTACK_SIZE  4096   /* one page per kernel stack */

/* Maximum user-stack size (pages).  exec pre-maps 8 pages; deeper stacks
 * (libc-bench needs ~80 KB ≈ 20 pages) are grown on demand up to this cap. */
#define LA_MAX_STACK_PAGES 512

/* ---- Process states ---- */
enum la_proc_state {
    LA_PROC_UNUSED = 0,
    LA_PROC_RUNNABLE,
    LA_PROC_RUNNING,
    LA_PROC_SLEEPING,
    LA_PROC_ZOMBIE,
};

/* ---- Kernel thread context (callee-saved regs, managed by swtch.S) ---- */
struct la_context {
    uint64_t ra;      /* $r1  */
    uint64_t sp;      /* $r3  */
    uint64_t fp;      /* $r22 */
    uint64_t s0;      /* $r23 */
    uint64_t s1;      /* $r24 */
    uint64_t s2;      /* $r25 */
    uint64_t s3;      /* $r26 */
    uint64_t s4;      /* $r27 */
    uint64_t s5;      /* $r28 */
    uint64_t s6;      /* $r29 */
    uint64_t s7;      /* $r30 */
    uint64_t s8;      /* $r31 */
};

/* ---- File descriptor table ---- */
#define LA_NFD 32

/* fd types */
#define LA_FD_UNUSED  0
#define LA_FD_CONSOLE 1   /* stdin/stdout/stderr → UART */
#define LA_FD_FILE    2   /* regular file on filesystem */

struct la_fd {
    uint32_t ino;            /* inode number (0 = unused) */
    uint32_t offset;         /* current read/write offset */
    int type;                /* LA_FD_UNUSED/CONSOLE/FILE */
    int writable;            /* 1 = write allowed */
};

/* Shared address-space state (heap + mmap cursors).
 * Normally embedded in each proc (p->mm = &p->__mm).
 * CLONE_VM threads point their mm at the leader's __mm so brk/mmap
 * allocators advance a single cursor across the whole thread group. */
struct la_mm {
    uint64_t heap_top;         /* user heap top (brk grows up from here) */
    uint64_t mmap_top;         /* mmap region (grows up, separate from heap) */
};

/* ---- Process control block ---- */
struct la_proc {
    int pid;
    enum la_proc_state state;
    char name[16];

    uint64_t kstack;           /* kernel stack page address */
    struct la_context ctx;     /* saved kernel context */

    /* User-mode state */
    struct la_trap_frame *tf;  /* user trap frame (separate page) */
    uint64_t *pgtbl;           /* user page table root */
    struct la_mm __mm;         /* inline address-space state (owned by leader) */
    struct la_mm *mm;          /* points at &__mm normally; for CLONE_VM threads
                                * points at the leader's __mm */
    uint64_t stack_bottom;     /* lowest mapped user-stack VA (grows down
                                * from LA_USER_STACK; 0 = not a user proc) */
    int is_user;               /* 1 = user process, 0 = kernel thread */
    int shared_vm;             /* 1 = CLONE_VM thread (shares pgtbl + mm; do NOT
                                * free pgtbl on reap — owned by the leader) */
    uint64_t clear_child_tid;  /* user VA of cleartid word (0 = none) */
    void  *wait_chan;          /* futex sleep channel (0 = pid-wakeup sleeper) */

    /* Process relationships */
    int parent_pid;            /* parent's PID (0 for first process) */
    int exit_code;             /* exit status for wait() */

    /* File descriptor table */
    struct la_fd fds[LA_NFD];

    /* Current working directory */
    uint32_t cwd_ino;          /* inode number of cwd */

    /* Kernel thread entry (used only for kthreads) */
    uint64_t entry;
};

/* ---- Per-CPU state (single CPU for now) ---- */
struct la_cpu {
    struct la_context scheduler_ctx;
    struct la_proc *current;
};

/* ---- Process management functions ---- */
void la_proc_init(void);
void la_scheduler(void) __attribute__((noreturn));
void la_proc_yield(void);
void la_sched_switch(struct la_context *old_ctx);

/* Terminate the current user process with `code` and switch to the
 * scheduler.  Clears ISTLBR so a TLB-refill path that swtches away does
 * not leave the bit set for the next process's trap.  Never returns. */
void la_proc_exit(int code) __attribute__((noreturn));
struct la_proc *la_proc_create_kthread(void (*entry)(void), const char *name);
struct la_proc *la_proc_create_user(const char *name);
struct la_proc *la_current_proc(void);

/* ---- Sleep / wakeup ---- */
void la_proc_sleep(void);
void la_proc_sleep_chan(void *chan);    /* futex channel-keyed sleep */
void la_proc_wakeup_pid(int pid);
void la_proc_wakeup_chan(void *chan);   /* futex channel-keyed wake */

/* ---- Process table accessor (for syscall.c) ---- */
struct la_proc *la_proc_by_pid(int pid);
struct la_proc *la_proc_table(void);  /* returns la_procs array */
void la_proc_free(struct la_proc *p); /* reap a zombie: free pgtbl + kstack, mark UNUSED */

/* ---- User process helpers ---- */
struct la_user_entry {
    uint64_t entry;
    uint64_t sp;
    uint64_t argc;
    uint64_t argv;
};

void la_trap_frame_init_user(struct la_trap_frame *tf,
                             const struct la_user_entry *entry);
void la_proc_return(struct la_trap_frame *tf);

/* ---- Context switch (swtch.S) ---- */
void la_swtch(struct la_context *old, struct la_context *new);

/* ---- UVM globals (defined in uvm_la.c, used by trap_entry.S) ---- */
extern uint64_t la_user_pgd;
extern uint64_t la_trap_ksp;

#endif
