/*
 * LoongArch process management.
 *
 * Provides a minimal process table, kernel-thread and user-process creation,
 * round-robin scheduler, and voluntary yield.
 */
#include "early_boot.h"
#include "proc.h"

/* ---- globals ---- */
static struct la_proc la_procs[LA_NPROC];
static struct la_cpu la_cpu;
static int la_next_pid;

/* ---- init ---- */
void la_proc_init(void)
{
    for (int i = 0; i < LA_NPROC; i++) {
        la_procs[i].state = LA_PROC_UNUSED;
        la_procs[i].pid   = 0;
    }
    la_cpu.current  = 0;
    la_next_pid     = 1;
    la_uart_puts("  proc: table initialized\n");
}

/* ---- current ---- */
struct la_proc *la_current_proc(void)
{
    return la_cpu.current;
}

/* ---- allocate a free proc slot ---- */
static struct la_proc *la_proc_alloc(void)
{
    for (int i = 0; i < LA_NPROC; i++) {
        if (la_procs[i].state != LA_PROC_UNUSED)
            continue;

        struct la_proc *p = &la_procs[i];
        p->pid    = la_next_pid++;
        p->kstack = 0;
        p->entry  = 0;
        p->tf     = 0;
        p->pgtbl  = 0;
        p->heap_top = 0;
        p->is_user  = 0;
        p->parent_pid = 0;
        p->exit_code  = 0;
        p->cwd_ino    = 0;       /* caller must set to root ino */

        /* zero fd table */
        for (int j = 0; j < LA_NFD; j++) {
            p->fds[j].ino = 0;
            p->fds[j].offset = 0;
            p->fds[j].type = LA_FD_UNUSED;
            p->fds[j].writable = 0;
        }

        /* zero context */
        uint64_t *r = (uint64_t *)&p->ctx;
        uint64_t *e = (uint64_t *)(&p->ctx + 1);
        while (r < e)
            *r++ = 0;

        /* zero name */
        for (int j = 0; j < 16; j++)
            p->name[j] = 0;

        return p;
    }
    return 0;
}

/* ---- release proc slot and kernel stack ---- */
static void la_proc_free(struct la_proc *p)
{
    if (p->kstack) {
        la_pmem_free((void *)p->kstack);
        p->kstack = 0;
    }
    /* TODO: free user page table */
    p->state = LA_PROC_UNUSED;
    p->pid   = 0;
    p->tf    = 0;
    p->pgtbl = 0;
    p->is_user = 0;
}

/*
 * Entry trampoline for every new kernel thread.
 * swtch jumps here (ctx.ra) the first time the thread is scheduled.
 */
static void __attribute__((used)) la_proc_bootstrap(void)
{
    struct la_proc *p = la_current_proc();
    if (p && p->entry) {
        void (*fn)(void) = (void (*)(void))p->entry;
        fn();
    }

    /* thread function returned — become zombie */
    struct la_proc *me = la_current_proc();
    if (me)
        me->state = LA_PROC_ZOMBIE;

    for (;;)
        la_proc_yield();
}

/*
 * Entry trampoline for user processes.
 * When the scheduler first picks up a user process, swtch lands here.
 * This sets up the user page table and calls la_user_return() to ertn
 * to user mode.
 */
static void __attribute__((used)) la_proc_user_bootstrap(void)
{
    struct la_proc *p = la_current_proc();
    if (!p) {
        la_uart_puts("  ub: p is NULL!\n");
        for (;;) {}
    }
    if (!p->tf) {
        la_uart_puts("  ub: tf is NULL!\n");
        for (;;) {}
    }

    /* Save kernel SP so the trap handler can find it on next user trap */
    la_trap_ksp = p->kstack + LA_KSTACK_SIZE;

    /* Set active page table for TLB refill handler */
    if (p->pgtbl)
        la_tlb_active_pgtbl = (uint64_t)p->pgtbl;

    /* Jump to user mode — never returns */
    la_proc_return(p->tf);

    for (;;) {}
}

/* ---- create a kernel thread ---- */
struct la_proc *la_proc_create_kthread(void (*entry)(void), const char *name)
{
    struct la_proc *p = la_proc_alloc();
    if (!p) {
        la_uart_puts("  proc: no free slots\n");
        return 0;
    }

    void *stack = la_pmem_alloc();
    if (!stack) {
        p->state = LA_PROC_UNUSED;
        la_uart_puts("  proc: no memory for stack\n");
        return 0;
    }

    p->kstack   = (uint64_t)stack;
    p->entry    = (uint64_t)entry;
    p->is_user  = 0;

    /* set up context so swtch jumps to la_proc_bootstrap on first run */
    p->ctx.ra = (uint64_t)la_proc_bootstrap;
    p->ctx.sp = (uint64_t)stack + LA_KSTACK_SIZE;

    if (name) {
        int i;
        for (i = 0; i < 15 && name[i]; i++)
            p->name[i] = name[i];
        p->name[i] = '\0';
    }

    p->state = LA_PROC_RUNNABLE;

    la_uart_puts("  proc: created '");
    la_uart_puts(p->name);
    la_uart_puts("' pid=");
    la_uart_put_hex(p->pid);
    la_uart_puts("\n");

    return p;
}

/* ---- create a user process ---- */
struct la_proc *la_proc_create_user(const char *name)
{
    struct la_proc *p = la_proc_alloc();
    if (!p) {
        la_uart_puts("  proc: no free slots for user\n");
        return 0;
    }

    void *stack = la_pmem_alloc();
    if (!stack) {
        p->state = LA_PROC_UNUSED;
        la_uart_puts("  proc: no memory for user stack\n");
        return 0;
    }

    p->kstack   = (uint64_t)stack;
    p->is_user  = 1;
    p->tf       = 0;
    p->pgtbl    = 0;
    p->heap_top = 0;

    /*
     * Set up context so swtch jumps to la_proc_user_bootstrap on first run.
     * The trap frame (tf) and page table (pgtbl) must be set by the caller
     * before the scheduler picks this process up.
     */
    p->ctx.ra = (uint64_t)la_proc_user_bootstrap;
    p->ctx.sp = (uint64_t)stack + LA_KSTACK_SIZE;

    if (name) {
        int i;
        for (i = 0; i < 15 && name[i]; i++)
            p->name[i] = name[i];
        p->name[i] = '\0';
    }

    p->state = LA_PROC_RUNNABLE;

    la_uart_puts("  proc: created user '");
    la_uart_puts(p->name);
    la_uart_puts("' pid=");
    la_uart_put_hex(p->pid);
    la_uart_puts("\n");

    return p;
}

/* ---- voluntary yield ---- */
void la_proc_yield(void)
{
    struct la_proc *p = la_cpu.current;
    if (!p)
        return;

    p->state = LA_PROC_RUNNABLE;
    la_swtch(&p->ctx, &la_cpu.scheduler_ctx);

    /*
     * The scheduler disables interrupts around swtch.
     * Re-enable them now so the caller (kernel thread) can
     * receive timer interrupts.
     */
    uint64_t crmd = la_csr_read(LA_CSR_CRMD);
    la_csr_write(crmd | LA_CRMD_IE, LA_CSR_CRMD);
}

/* ---- switch to scheduler context (for sys_exit etc.) ---- */
void la_sched_switch(struct la_context *old_ctx)
{
    la_swtch(old_ctx, &la_cpu.scheduler_ctx);
}

/* ---- sleep: mark current proc SLEEPING, switch to scheduler ---- */
void la_proc_sleep(void)
{
    struct la_proc *p = la_cpu.current;
    if (!p) return;
    p->state = LA_PROC_SLEEPING;
    la_swtch(&p->ctx, &la_cpu.scheduler_ctx);

    /* Re-enable interrupts after waking up */
    uint64_t crmd = la_csr_read(LA_CSR_CRMD);
    la_csr_write(crmd | LA_CRMD_IE, LA_CSR_CRMD);
}

/* ---- wakeup: wake all SLEEPING procs with matching pid ---- */
void la_proc_wakeup_pid(int pid)
{
    for (int i = 0; i < LA_NPROC; i++) {
        if (la_procs[i].pid == pid && la_procs[i].state == LA_PROC_SLEEPING)
            la_procs[i].state = LA_PROC_RUNNABLE;
    }
}

/* ---- look up a proc by PID ---- */
struct la_proc *la_proc_by_pid(int pid)
{
    for (int i = 0; i < LA_NPROC; i++) {
        if (la_procs[i].pid == pid && la_procs[i].state != LA_PROC_UNUSED)
            return &la_procs[i];
    }
    return 0;
}

/* ---- return the process table (for syscall iteration) ---- */
struct la_proc *la_proc_table(void)
{
    return la_procs;
}

/* ---- round-robin scheduler (never returns) ---- */
void la_scheduler(void)
{
    static int next_idx;

    for (;;) {
        /* Enable interrupts while searching / idling */
        uint64_t crmd = la_csr_read(LA_CSR_CRMD);
        la_csr_write(crmd | LA_CRMD_IE, LA_CSR_CRMD);

        /* round-robin: start searching from next_idx */
        struct la_proc *p = 0;
        for (int i = 0; i < LA_NPROC; i++) {
            int idx = (next_idx + i) % LA_NPROC;
            if (la_procs[idx].state == LA_PROC_RUNNABLE) {
                p = &la_procs[idx];
                next_idx = idx + 1;
                break;
            }
        }

        if (!p) {
            /* nothing to run — wait for interrupt */
            asm volatile("idle 0" ::: "memory");
            continue;
        }

        /* Disable interrupts around context switch */
        crmd = la_csr_read(LA_CSR_CRMD);
        la_csr_write(crmd & ~LA_CRMD_IE, LA_CSR_CRMD);

        p->state = LA_PROC_RUNNING;
        la_cpu.current = p;

        /*
         * For user processes: save kernel SP in la_trap_ksp before swtch.
         * When the user process traps back to kernel, it will use this SP.
         * Also set the active page table for the TLB refill handler.
         */
        if (p->is_user) {
            la_trap_ksp = p->kstack + LA_KSTACK_SIZE;
            if (p->pgtbl) {
                la_tlb_active_pgtbl = (uint64_t)p->pgtbl;
                /* Drop every global TLB entry.  Because all our mappings
                 * are global and untagged by ASID, entries belonging to a
                 * different process (or this one's pre-exec image) still
                 * alias the very same VPPNs the resumed process will use.
                 * Leaving them in causes non-deterministic wrong-PA loads.
                 * The process refills what it needs on demand (or via
                 * la_proc_return's fill on its first run). */
                la_tlb_inval_all();
            }
        }

        la_swtch(&la_cpu.scheduler_ctx, &p->ctx);

        /* --- back in scheduler --- */
        la_cpu.current = 0;

        if (p->state == LA_PROC_ZOMBIE) {
            /* Only reap zombies that have no living parent.
             * Zombies with a parent must stay until the parent
             * calls sys_wait() to collect the exit status. */
            if (p->parent_pid == 0 || !la_proc_by_pid(p->parent_pid))
                la_proc_free(p);
        }
    }
}

/* ---- legacy helpers (for future user-mode) ---- */

void la_trap_frame_init_user(struct la_trap_frame *tf,
                             const struct la_user_entry *entry)
{
    if (!tf || !entry)
        return;
    for (int i = 0; i < 32; i++)
        tf->gpr[i] = 0;
    tf->era = entry->entry;
    tf->gpr[LA_GPR_SP] = entry->sp;
    tf->gpr[LA_GPR_A0] = entry->argc;
    tf->gpr[LA_GPR_A1] = entry->argv;
}

void la_proc_return(struct la_trap_frame *tf)
{
    if (!tf) {
        la_uart_puts("la_proc_return: missing trap frame\n");
        for (;;) {}
    }

    /* Switch to user page table */
    struct la_proc *p = la_current_proc();
    if (p && p->pgtbl) {
        la_uvm_switch(p->pgtbl);

        /* ALWAYS invalidate the whole TLB before refilling.
         *
         * All our leaf PTEs are loaded with the G (global) bit and we use
         * NO per-address-space ASID, so TLB entries are NOT tagged by
         * process.  Entries left over from a previous image — e.g. the
         * parent's (initcode) stack at the SAME virtual address the new
         * image (busybox) reuses — survive across exec/fork and create
         * DUPLICATE entries for one VPPN.  A TLB lookup may then return
         * either the stale or the fresh entry non-deterministically, so a
         * user load (notably musl reading AT_RANDOM to derive mallocng's
         * ctx.secret) can hit the wrong physical page and corrupt heap
         * metadata, crashing busybox in get_meta()'s secret check.
         *
         * Invalidating here guarantees only this image's mappings remain. */
        la_tlb_inval_all();
        la_tlb_fill_all(p->pgtbl);
    }

    /* Save kernel SP for next user→kernel trap */
    if (p)
        la_trap_ksp = p->kstack + LA_KSTACK_SIZE;

    /* Switch DA=0 PG=1 — DMW0 provides kernel identity mapping. */
    la_csr_write(LA_CRMD_PG | LA_CRMD_IE, LA_CSR_CRMD);   /* PLV=0, IE=1, DA=0, PG=1 */

    la_user_return(tf);
    for (;;) {}
}
