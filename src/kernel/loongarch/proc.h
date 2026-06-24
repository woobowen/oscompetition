#ifndef SEAOS_LOONGARCH_PROC_H
#define SEAOS_LOONGARCH_PROC_H

#include <stdint.h>
#include "trap.h"

#define LA_NPROC        128
#define LA_KSTACK_SIZE  4096   /* one page per kernel stack */

/* Scheduler time slice in timer ticks (100 Hz → 10 ticks = 100 ms). */
#define LA_TIME_SLICE   10

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
#define LA_NFD 128

/* fd types */
#define LA_FD_UNUSED  0
#define LA_FD_CONSOLE 1   /* stdin/stdout/stderr → UART */
#define LA_FD_FILE    2   /* regular file on ext4/SeaFS (read-only) */
#define LA_FD_PIPE    3   /* pipe (read end or write end) */
#define LA_FD_MEMFS   4   /* writable file on memfs */
#define LA_FD_SOCKET  5   /* loopback socket (TCP / UDP) */
#define LA_FD_DEV     6   /* simple character devices: /dev/null, /dev/zero */

/* ---- Pipe ---- */
#define LA_PIPE_SIZE   4096
#define LA_NPIPE       32

struct la_pipe {
    char data[LA_PIPE_SIZE];   /* circular buffer */
    uint32_t nread;            /* total bytes read (monotonic; mod PIPE_SIZE) */
    uint32_t nwrite;           /* total bytes written (monotonic) */
    int readopen;              /* refcount of open read-end fds */
    int writeopen;             /* refcount of open write-end fds */
    int used;                  /* 1 = allocated from the pool */
};

struct la_fd {
    uint32_t ino;            /* inode number (0 = unused) */
    uint64_t offset;         /* current read/write offset */
    int type;                /* LA_FD_UNUSED/CONSOLE/FILE/PIPE */
    int writable;            /* 1 = write allowed */
    int cloexec;             /* FD_CLOEXEC state */
    int nonblock;            /* O_NONBLOCK state */
    struct la_pipe *pipe;    /* pipe object (valid when type == LA_FD_PIPE) */
    int sock_idx;            /* socket index (valid when type == LA_FD_SOCKET) */
};

/* Shared address-space state (heap + mmap cursors).
 * Normally embedded in each proc (p->mm = &p->__mm).
 * CLONE_VM threads point their mm at the leader's __mm so brk/mmap
 * allocators advance a single cursor across the whole thread group. */
struct la_mm {
    uint64_t brk_base;         /* lowest valid program break for this image */
    uint64_t heap_top;         /* user heap top (brk grows up from here) */
    uint64_t mmap_top;         /* mmap region (grows up, separate from heap) */
};

/* Internal signal action structure.
 * When handler == 0: SIG_DFL (default action).
 * When handler == 1: SIG_IGN (ignore).
 * restorer is the user-space trampoline that calls rt_sigreturn. */
#define LA_NSIG   64
#define LA_SIG_DFL 0
#define LA_SIG_IGN 1

/* Signal numbers (Linux generic ABI) */
#define LA_SIGHUP    1
#define LA_SIGINT    2
#define LA_SIGQUIT   3
#define LA_SIGILL    4
#define LA_SIGTRAP   5
#define LA_SIGABRT   6
#define LA_SIGBUS    7
#define LA_SIGFPE    8
#define LA_SIGKILL   9
#define LA_SIGUSR1  10
#define LA_SIGSEGV  11
#define LA_SIGUSR2  12
#define LA_SIGPIPE  13
#define LA_SIGALRM  14
#define LA_SIGTERM  15
#define LA_SIGCHLD  17
#define LA_SIGCONT  18
#define LA_SIGSTOP  19

struct la_sigaction {
    uint64_t handler;     /* signal handler address (SIG_DFL/SIG_IGN or function) */
    uint64_t flags;       /* SA_SIGINFO etc. */
    uint64_t restorer;    /* user-space sigreturn trampoline address */
    uint64_t mask;        /* additional signals blocked during handler */
};

/* Signal frame saved on the user stack when a signal is delivered.
 * Must be 16-byte aligned on LoongArch. */
struct la_sigframe {
    /* saved registers */
    uint64_t gpr[32];
    uint64_t era;
    uint64_t old_sig_mask;
    /* delivery metadata */
    uint64_t sig;          /* signal number */
    uint8_t siginfo[128];  /* minimal Linux siginfo_t */
    uint8_t ucontext[256]; /* minimal ucontext_t storage for SA_SIGINFO */
    uint32_t tramp[2];     /* li.w a7, SYS_rt_sigreturn; syscall 0 */
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
    uint64_t asid;             /* hardware TLB ASID; shared by CLONE_VM threads */
    struct la_mm __mm;         /* inline address-space state (owned by leader) */
    struct la_mm *mm;          /* points at &__mm normally; for CLONE_VM threads
                                * points at the leader's __mm */
    uint64_t stack_bottom;     /* lowest mapped user-stack VA (grows down
                                * from LA_USER_STACK; 0 = not a user proc) */
    int is_user;               /* 1 = user process, 0 = kernel thread */
    int shared_vm;             /* 1 = CLONE_VM thread (shares pgtbl + mm; do NOT
                                * free pgtbl on reap — owned by the leader) */
    int ticks;                 /* remaining timer ticks in this time slice */
    int sched_priority;        /* RT priority (0 = normal, 1–99 = SCHED_FIFO) */
    uint64_t clear_child_tid;  /* user VA of cleartid word (0 = none) */
    int    trace_sys;           /* 1 = trace syscalls for this proc */
    void  *wait_chan;          /* futex sleep channel (0 = pid-wakeup sleeper) */
    uint64_t sleep_deadline_ticks;
    int sleep_timed_out;

    /* Signal handling */
    uint64_t sig_pending;      /* bitmap of pending signals */
    uint64_t sig_mask;         /* bitmap of blocked signals */
    struct la_sigaction sig_actions[LA_NSIG];

    /* Process relationships */
    int parent_pid;            /* parent's PID (0 for first process) */
    int exit_code;             /* exit status for wait() */
    uint64_t rlimit_nofile_cur;
    uint64_t rlimit_nofile_max;

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

/* ---- Signal delivery (called from trap.c before returning to user) ---- */
int la_signal_pending(struct la_trap_frame *tf);
void la_signal_deliver(struct la_trap_frame *tf);

/* ---- Sleep / wakeup ---- */
void la_proc_sleep(void);
void la_proc_sleep_chan(void *chan);    /* futex channel-keyed sleep */
int  la_proc_sleep_chan_until(void *chan, uint64_t deadline_ticks);
void la_proc_wakeup_pid(int pid);
void la_proc_wakeup_chan(void *chan);   /* futex channel-keyed wake */

/* ---- Process table accessor (for syscall.c) ---- */
struct la_proc *la_proc_by_pid(int pid);
struct la_proc *la_proc_table(void);  /* returns la_procs array */
void la_proc_free(struct la_proc *p); /* reap a zombie: free pgtbl + kstack, mark UNUSED */
void la_proc_activate_user_pgtbl(struct la_proc *p);

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
