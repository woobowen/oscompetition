/*
 * LoongArch process management.
 *
 * Provides a minimal process table, kernel-thread and user-process creation,
 * round-robin scheduler, and voluntary yield.
 */
#include "early_boot.h"
#include "proc.h"

/* ---- globals ---- */
#define LA_ASID_MASK 0x3ffUL

static struct la_proc la_procs[LA_NPROC];
static struct la_cpu la_cpu;
static int la_next_pid;
static uint64_t la_next_asid = 1;
static int la_asid_ever_used[LA_ASID_MASK + 1];

static int la_proc_asid_live(uint64_t asid)
{
    for (int i = 0; i < LA_NPROC; i++) {
        if (la_procs[i].state != LA_PROC_UNUSED &&
            la_procs[i].asid == asid)
            return 1;
    }
    return 0;
}

static uint64_t la_proc_alloc_asid(void)
{
    for (int tries = 0; tries < (int)LA_ASID_MASK; tries++) {
        uint64_t asid = la_next_asid;
        la_next_asid++;
        if (la_next_asid > LA_ASID_MASK)
            la_next_asid = 1;

        if (la_proc_asid_live(asid))
            continue;

        if (la_asid_ever_used[asid])
            la_tlb_inval_all();
        la_asid_ever_used[asid] = 1;
        return asid;
    }

    return 0;
}

int la_proc_assign_fresh_asid(struct la_proc *p)
{
    if (!p)
        return -1;

    uint64_t asid = la_proc_alloc_asid();
    if (asid == 0)
        return -1;

    p->asid = asid;
    return 0;
}

/* ---- init ---- */
void la_proc_init(void)
{
    for (int i = 0; i < LA_NPROC; i++) {
        la_procs[i].state = LA_PROC_UNUSED;
        la_procs[i].pid   = 0;
    }
    for (int i = 0; i <= (int)LA_ASID_MASK; i++)
        la_asid_ever_used[i] = 0;
    la_cpu.current  = 0;
    la_next_pid     = 1;
    la_next_asid    = 1;
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
        uint64_t asid = la_proc_alloc_asid();
        if (asid == 0) {
            la_uart_puts("  proc: no free ASID\n");
            return 0;
        }
        p->pid    = la_next_pid++;
        p->asid   = asid;
        p->kstack = 0;
        p->entry  = 0;
        p->tf     = 0;
        p->pgtbl  = 0;
        p->__mm.brk_base = 0;
        p->__mm.heap_top = 0;
        p->__mm.mmap_top = 0;
        p->mm     = &p->__mm;
        p->stack_bottom = 0;
        p->is_user  = 0;
        p->shared_vm = 0;
        p->ticks    = LA_TIME_SLICE;
        p->sched_policy = 0;
        p->sched_priority = 0;
        p->uid = 0;
        p->euid = 0;
        p->gid = 0;
        p->egid = 0;
        p->cap_effective = ~0ULL;
        p->cap_permitted = ~0ULL;
        p->cap_inheritable = 0;
        p->clear_child_tid = 0;
        p->wait_chan = 0;
        p->sleep_deadline_ticks = 0;
        p->sleep_timed_out = 0;
        p->sig_pending = 0;
        p->sig_mask    = 0;
        p->itimer_expire = 0;
        p->itimer_interval = 0;
        for (int s = 0; s < LA_NSIG; s++) {
            p->sig_actions[s].handler  = LA_SIG_DFL;
            p->sig_actions[s].flags    = 0;
            p->sig_actions[s].restorer = 0;
            p->sig_actions[s].mask     = 0;
        }
        p->parent_pid = 0;
        p->vfork_parent_pid = 0;
        p->exit_code  = 0;
        p->term_signal = 0;
        p->core_dumped = 0;
        p->rlimit_core_cur = ~0ULL;
        p->rlimit_core_max = ~0ULL;
        p->rlimit_nofile_cur = LA_NFD;
        p->rlimit_nofile_max = LA_NFD;
        p->cwd_ino    = 0;       /* caller must set to root ino */

        /* zero fd table */
        for (int j = 0; j < LA_NFD; j++) {
            p->fds[j].ino = 0;
            p->fds[j].offset = 0;
            p->fds[j].type = LA_FD_UNUSED;
            p->fds[j].writable = 0;
            p->fds[j].cloexec = 0;
            p->fds[j].nonblock = 0;
            p->fds[j].path_only = 0;
            p->fds[j].pipe = 0;
            p->fds[j].sock_idx = 0;
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

/* ---- release proc slot, kernel stack, and user page table ----
 *
 * Fully reclaims a zombie's resources: the user page table and every page it
 * maps (la_uvm_free_pgtbl), the kernel stack, and the trap frame page.
 * Called by the scheduler for parentless zombies AND by sys_wait when a parent
 * collects a child.  Safe to call on a ZOMBIE that has already swtch'd away:
 * its kstack is no longer in use and the kernel runs off DMW0 identity mapping
 * (not the user page table), so freeing pgtbl cannot fault the kernel.
 * la_uvm_free_pgtbl performs the deep TLB invalidation needed before freed
 * page-table pages can be reused.
 *
 * CLONE_VM threads (shared_vm==1) share the leader's pgtbl; only the leader
 * (shared_vm==0) owns and frees it.  If the leader is freed while a sibling
 * still references its pgtbl, ownership transfers to the first found sibling.
 * This is the backstop against the exact 0x137c "stale PGDL / freed pgtbl"
 * cascade if a thread somehow outlives the leader. */
void la_proc_free(struct la_proc *p)
{
    /* ---- trap frame page ----
     * In every live code path (initcode bootstrap, sys_fork,
     * la_do_exec_syscall) the trap frame is a separately-allocated page,
     * NOT embedded in the kernel stack.  The old la_do_exec kstack-tf
     * path is dead code with no callers.  Freeing p->tf is therefore safe
     * and fixes a per-process page leak that frequent clone/join cycles
     * would rapidly re-exhaust. */
    if (p->tf) {
        la_pmem_free((void *)p->tf);
        p->tf = 0;
    }

    /* ---- page table ----
     * shared_vm threads never own the pgtbl.  The owner (shared_vm==0) frees
     * it unless a sibling still needs it — then ownership transfers. */
    if (p->shared_vm == 0 && p->pgtbl) {
        /* Check whether any other live proc shares this root.
         * If so, transfer ownership to the first found sibling
         * so the page table is not freed out from under it. */
        struct la_proc *procs = la_proc_table();
        int shared_with_sibling = 0;
        for (int i = 0; i < LA_NPROC; i++) {
            if (&procs[i] == p) continue;
            if (procs[i].state == LA_PROC_UNUSED) continue;
            if (procs[i].state == LA_PROC_ZOMBIE) continue;
            if (procs[i].pgtbl == p->pgtbl) {
                shared_with_sibling = 1;
                /* Transfer ownership: the first live sibling
                 * becomes the new owner of the shared root. */
                procs[i].shared_vm = 0;
                break;
            }
        }
        if (!shared_with_sibling)
            la_uvm_free_pgtbl(p->pgtbl);
        p->pgtbl = 0;
    } else if (p->shared_vm == 1) {
        /* Thread: skip pgtbl free — the leader (or owner) owns it. */
        p->pgtbl = 0;
    }

    if (p->kstack) {
        la_pmem_free((void *)p->kstack);
        p->kstack = 0;
    }
    p->state   = LA_PROC_UNUSED;
    p->pid     = 0;
    p->asid    = 0;
    p->is_user = 0;
    p->shared_vm = 0;
    p->clear_child_tid = 0;
    p->sched_policy = 0;
    p->sched_priority = 0;
    p->wait_chan = 0;
    p->term_signal = 0;
    p->core_dumped = 0;
}

static int la_signal_dumps_core(int sig)
{
    switch (sig) {
    case LA_SIGQUIT:
    case LA_SIGILL:
    case LA_SIGTRAP:
    case LA_SIGABRT:
    case LA_SIGBUS:
    case LA_SIGFPE:
    case LA_SIGSEGV:
    case 24:  /* SIGXCPU */
    case 25:  /* SIGXFSZ */
    case 31:  /* SIGSYS */
        return 1;
    default:
        return 0;
    }
}

void la_proc_note_signal_exit(struct la_proc *p, int sig)
{
    if (!p)
        return;
    p->term_signal = sig;
    p->core_dumped = (p->rlimit_core_cur != 0 && la_signal_dumps_core(sig));
}

void la_proc_activate_user_pgtbl(struct la_proc *p)
{
    if (!p || !p->pgtbl)
        return;

    if (p->asid == 0)
        p->asid = la_proc_alloc_asid();
    if (p->asid == 0)
        return;

    la_csr_write(p->asid & LA_ASID_MASK, LA_CSR_ASID);
    la_tlb_active_pgtbl = (uint64_t)p->pgtbl;
    la_uvm_switch(p->pgtbl);

    /* The LoongArch lp64d userland uses the hardware FPU.  EUEN defaults to
     * disabled after boot; leave FPU enabled while this kernel does not use FP
     * registers itself, otherwise dynamic musl/unixbench programs trap with
     * FPD (ecode 0xf) on their first FP instruction. */
    la_csr_write(la_csr_read(LA_CSR_EUEN) | LA_EUEN_FPE, LA_CSR_EUEN);
}

static void la_proc_restore_user_return_state(struct la_proc *p)
{
    if (!p || !p->is_user)
        return;

    /* A process can context-switch away while still inside a syscall
     * implementation (wait/futex/sigtimedwait/timer preemption).  While it is
     * asleep, the scheduler may take kernel-mode timer interrupts, which leave
     * PRMD describing a PLV0 return.  trap_entry.S uses PRMD.PPLV to choose
     * the final restore path before ertn, so restore the user return state
     * whenever such a process resumes inside the kernel. */
    la_csr_write(LA_USER_PLV | LA_PRMD_PIE, LA_CSR_PRMD);
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

    /* Set ASID, hardware PGD, and the software refill root. */
    la_proc_activate_user_pgtbl(p);

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
    p->__mm.brk_base = 0;
    p->__mm.heap_top = 0;
    p->__mm.mmap_top = 0;
    p->mm       = &p->__mm;
    p->stack_bottom = 0;

    /*
     * Set up context so swtch jumps to la_proc_user_bootstrap on first run.
     * The trap frame (tf) and page table (pgtbl) must be set by the caller
     * before the scheduler picks this process up.  Keep the process
     * non-runnable until the caller publishes it after full initialization;
     * otherwise timer preemption during fork/clone can schedule a child with
     * tf == NULL.
     */
    p->ctx.ra = (uint64_t)la_proc_user_bootstrap;
    p->ctx.sp = (uint64_t)stack + LA_KSTACK_SIZE;

    if (name) {
        int i;
        for (i = 0; i < 15 && name[i]; i++)
            p->name[i] = name[i];
        p->name[i] = '\0';
    }

    p->state = LA_PROC_SLEEPING;

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
    la_proc_restore_user_return_state(p);
}

/* ---- switch to scheduler context (for sys_exit etc.) ---- */
void la_sched_switch(struct la_context *old_ctx)
{
    la_swtch(old_ctx, &la_cpu.scheduler_ctx);
}

/* ---- terminate the current user process (never returns) ----
 *
 * Used by sys_exit / sys_exit_group AND by trap handlers that must kill a
 * user process on an unrecoverable fault (stack-overflow segv, ADEF, etc.).
 * Two things make this safe to call from inside the ISTLBR (TLB-refill)
 * trap path:
 *
 *   1. Clear the ISTLBR bit (bit 0 of TLBRERA).  trap_entry.S routes EVERY
 *      trap through a TLBRERA&1 check; if ISTLBR stays set after we swtch
 *      away (ertn is the only thing that clears it in hardware, and we are
 *      NOT returning via ertn here), the NEXT process to trap is misrouted
 *      down the TLB-refill path and the system cascades into failure.  On
 *      the general-exception path ISTLBR is already 0, so this is a no-op.
 *
 *   2. Switch to the scheduler via la_sched_switch (never returns).  The
 *      scheduler will reap this zombie once its parent waits (or immediately
 *      if it has no living parent). */
void __attribute__((noreturn)) la_proc_exit(int code)
{
    struct la_proc *me = la_current_proc();
    if (!me || !me->is_user) {
        la_uart_puts("la_proc_exit: no current user proc — HALT\n");
        for (;;) {}
    }

    la_csr_write(la_csr_read(LA_CSR_TLBRERA) & ~1ULL, LA_CSR_TLBRERA);  /* clear ISTLBR */

    /* Clear the child tid and wake pthread_join waiters.  This must happen
     * regardless of the exit path — sys_exit, signal delivery (la_signal_deliver),
     * or la_cancel_thread_signal.  sys_exit already zeroes clear_child_tid
     * before calling us, so this is a no-op in that path. */
    if (me->clear_child_tid) {
        uint32_t zero = 0;
        la_copy_to_user(me->clear_child_tid, &zero, sizeof(zero));
        la_proc_wakeup_chan((void *)me->clear_child_tid);
        me->clear_child_tid = 0;
    }

    /* Wake any futex waiters on this thread's wait channel */
    if (me->wait_chan)
        la_proc_wakeup_chan(me->wait_chan);

    /* Reparent orphan children to init (pid 1).  If we don't do this,
     * every shell→command fork cycle leaves behind zombies whose parent_pid
     * points to a dead process.  Those zombies are never reaped and
     * accumulate in the process table, making the scheduler scan slower
     * each tick until the timer effectively stalls. */
    {
        struct la_proc *procs = la_proc_table();
        for (int i = 0; i < LA_NPROC; i++) {
            if (procs[i].state != LA_PROC_UNUSED &&
                procs[i].parent_pid == me->pid) {
                procs[i].parent_pid = 1;
                /* If the orphan is already a zombie, wake init so it can
                 * reap it on its next wait cycle. */
                if (procs[i].state == LA_PROC_ZOMBIE)
                    la_proc_wakeup_pid(1);
            }
        }
    }

    me->exit_code = (int)(unsigned)code;
    if (me->vfork_parent_pid > 0) {
        la_proc_wakeup_pid(me->vfork_parent_pid);
        me->vfork_parent_pid = 0;
    }
    me->state     = LA_PROC_ZOMBIE;
    if (me->parent_pid > 0)
        la_proc_wakeup_pid(me->parent_pid);
    la_sched_switch(&me->ctx);   /* does not return */
    for (;;) {}
}

/* ---- sleep: mark current proc SLEEPING, switch to scheduler ----
 * The sleeper is woken by la_proc_wakeup_pid(parent_pid) when a child exits.
 * wait_chan is cleared to 0 so a futex wakeup_chan never spuriously wakes
 * a wait4-sleeper. */
void la_proc_sleep(void)
{
    struct la_proc *p = la_cpu.current;
    if (!p) return;
    p->wait_chan = 0;          /* guard: never match a futex wakeup_chan */
    p->state = LA_PROC_SLEEPING;
    la_swtch(&p->ctx, &la_cpu.scheduler_ctx);

    /* Re-enable interrupts after waking up */
    uint64_t crmd = la_csr_read(LA_CSR_CRMD);
    la_csr_write(crmd | LA_CRMD_IE, LA_CSR_CRMD);
    la_proc_restore_user_return_state(p);
}

/* ---- sleep_chan: futex channel-keyed sleep ----
 * Sets wait_chan BEFORE state=SLEEPING so that under the non-preemptive
 * single-CPU scheduler the check-then-sleep in FUTEX_WAIT is observationally
 * atomic: no other proc can run between the val-compare and the swtch, and
 * wakeup_chan only ever flips futex sleepers (never pid-sleepers). */
void la_proc_sleep_chan(void *chan)
{
    struct la_proc *p = la_cpu.current;
    if (!p) return;
    p->wait_chan = chan;       /* order matters: set channel, then sleep */
    p->sleep_deadline_ticks = 0;
    p->sleep_timed_out = 0;
    p->state = LA_PROC_SLEEPING;
    la_swtch(&p->ctx, &la_cpu.scheduler_ctx);

    /* Re-enable interrupts after waking up */
    uint64_t crmd = la_csr_read(LA_CSR_CRMD);
    la_csr_write(crmd | LA_CRMD_IE, LA_CSR_CRMD);
    la_proc_restore_user_return_state(p);
}

int la_proc_sleep_chan_until(void *chan, uint64_t deadline_ticks)
{
    struct la_proc *p = la_cpu.current;
    if (!p) return 0;
    p->wait_chan = chan;
    p->sleep_deadline_ticks = deadline_ticks;
    p->sleep_timed_out = 0;
    p->state = LA_PROC_SLEEPING;
    la_swtch(&p->ctx, &la_cpu.scheduler_ctx);

    uint64_t crmd = la_csr_read(LA_CSR_CRMD);
    la_csr_write(crmd | LA_CRMD_IE, LA_CSR_CRMD);
    la_proc_restore_user_return_state(p);

    int timed_out = p->sleep_timed_out;
    p->sleep_deadline_ticks = 0;
    p->sleep_timed_out = 0;
    return timed_out;
}

/* ---- wakeup: wake all SLEEPING procs with matching pid ----
 * Only wakes pid-keyed sleepers (wait_chan==0), never futex sleepers. */
void la_proc_wakeup_pid(int pid)
{
    for (int i = 0; i < LA_NPROC; i++) {
        if (la_procs[i].pid == pid
            && la_procs[i].state == LA_PROC_SLEEPING
            && la_procs[i].wait_chan == 0)   /* guard: never wake a futex sleeper */
            la_procs[i].state = LA_PROC_RUNNABLE;
    }
}

/* ---- wakeup_chan: wake all SLEEPING procs waiting on a futex channel ---- */
void la_proc_wakeup_chan(void *chan)
{
    for (int i = 0; i < LA_NPROC; i++) {
        if (la_procs[i].state == LA_PROC_SLEEPING
            && la_procs[i].wait_chan == chan) {
            la_procs[i].state = LA_PROC_RUNNABLE;
            la_procs[i].wait_chan = 0;   /* clear channel after wake */
            la_procs[i].sleep_deadline_ticks = 0;
            la_procs[i].sleep_timed_out = 0;
        }
    }
}

void la_proc_check_itimers(uint64_t now_ticks)
{
    for (int i = 0; i < LA_NPROC; i++) {
        struct la_proc *p = &la_procs[i];
        if (p->state == LA_PROC_UNUSED || p->state == LA_PROC_ZOMBIE)
            continue;
        if (p->itimer_expire == 0 || now_ticks < p->itimer_expire)
            continue;

        p->sig_pending |= (1UL << LA_SIGALRM);
        if (p->itimer_interval != 0)
            p->itimer_expire = now_ticks + p->itimer_interval;
        else
            p->itimer_expire = 0;

        if (p->state == LA_PROC_SLEEPING) {
            p->state = LA_PROC_RUNNABLE;
            p->wait_chan = 0;
            p->sleep_deadline_ticks = 0;
            p->sleep_timed_out = 0;
        }
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

        uint64_t now = la_timer_get_ticks();
        for (int i = 0; i < LA_NPROC; i++) {
            if (la_procs[i].state == LA_PROC_SLEEPING &&
                la_procs[i].sleep_deadline_ticks &&
                now >= la_procs[i].sleep_deadline_ticks) {
                la_procs[i].state = LA_PROC_RUNNABLE;
                la_procs[i].wait_chan = 0;
                la_procs[i].sleep_deadline_ticks = 0;
                la_procs[i].sleep_timed_out = 1;
            }
        }

        /* Priority-aware scheduling:
         * - First pass: find the RUNNABLE process with highest sched_priority.
         * - Within the same priority tier, round-robin from next_idx.
         * - Priority 0 = normal (SCHED_OTHER), 1–99 = SCHED_FIFO. */
        struct la_proc *p = 0;
        int best_prio = -1;
        int best_idx  = 0;
        int found_any = 0;

        for (int i = 0; i < LA_NPROC; i++) {
            int idx = (next_idx + i) % LA_NPROC;
            if (la_procs[idx].state != LA_PROC_RUNNABLE)
                continue;
            found_any = 1;
            int prio = la_procs[idx].sched_priority;
            if (prio > best_prio) {
                best_prio = prio;
                best_idx  = idx;
            }
        }

        if (found_any)
            p = &la_procs[best_idx];
        next_idx = best_idx + 1;

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

        /* Refill the time slice for user processes.
         * Kernel threads yield voluntarily and don't consume ticks. */
        if (p->is_user)
            p->ticks = LA_TIME_SLICE;

        /*
         * For user processes: save kernel SP in la_trap_ksp before swtch.
         * When the user process traps back to kernel, it will use this SP.
         * Also set the active page table for the TLB refill handler.
         */
        if (p->is_user) {
            la_trap_ksp = p->kstack + LA_KSTACK_SIZE;
            if (p->pgtbl) {
                /* Point BOTH address-space roots at this proc's table:
                 *   - la_tlb_active_pgtbl drives the SOFTWARE TLB refill
                 *     handler (tlb_la.c);
                 *   - la_uvm_switch writes the PGDL CSR, which the HARDWARE
                 *     page-table walker (HPTW) uses on a TLB miss.
                 * We MUST call la_uvm_switch here, not only on the first-run
                 * path (la_proc_return).  A process that slept (e.g. in
                 * wait4) is resumed by THIS scheduler path, not by
                 * la_proc_return; if PGDL is left stale, HPTW walks the LAST
                 * table that la_uvm_switch set — frequently a child's table
                 * that has since been freed by la_proc_free (Step 19 reaping).
                 * The freed page is then reused/zeroed, so the resumed
                 * process's first fetch resolves through a bogus root and
                 * faults (INE/ADEF).  This was the root cause of the
                 * initcode 0x137c INE cascade: clone(CLONE_VM) killed a
                 * libc-bench worker; reaping freed its pgtbl root, and the
                 * stale PGDL then killed pid4, pid2, and initcode in turn.
                 *
                 * Do not flush/refill the whole TLB on every resume.
                 * Independent live address spaces have unique ASIDs, and
                 * CLONE_VM threads share their leader's ASID.  Repeated
                 * whole-address tlbfill without first removing old entries
                 * can create duplicate translations for the same ASID/VPPN;
                 * after mmap and munmap churn, QEMU may hit a stale duplicate
                 * and write through the wrong physical page.  PGDL/ASID
                 * activation is enough here; new or missing translations are
                 * handled by HPTW or by the single-page refill path. */
                la_proc_activate_user_pgtbl(p);
            }
        }

        la_swtch(&la_cpu.scheduler_ctx, &p->ctx);

        /* --- back in scheduler --- */
        la_cpu.current = 0;

        if (p->state == LA_PROC_ZOMBIE) {
            /* CLONE_VM threads do not own the shared page table.  Once a
             * thread has run sys_exit, clear_child_tid has already been
             * zeroed and futex waiters have been woken, so the scheduler can
             * reclaim its proc slot immediately.  wait4 deliberately skips
             * shared_vm threads; leaving them as zombies exhausts LA_NPROC in
             * pthread-heavy tests. */
            if (p->shared_vm) {
                la_proc_free(p);
                continue;
            }
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
        la_proc_activate_user_pgtbl(p);

        /* ALWAYS invalidate the whole TLB before first user entry.
         *
         * This path is used for a process's first user entry.  It is also the
         * right place to clear stale entries if a proc slot/ASID was reused:
         * after this point translations are rebuilt lazily from the current
         * page table by HPTW or by the single-page refill handler. */
        la_tlb_inval_all();
    }

    /* Save kernel SP for next user→kernel trap */
    if (p)
        la_trap_ksp = p->kstack + LA_KSTACK_SIZE;

    /* Switch DA=0 PG=1 — DMW0 provides kernel identity mapping. */
    la_csr_write(LA_CRMD_PG | LA_CRMD_IE, LA_CSR_CRMD);   /* PLV=0, IE=1, DA=0, PG=1 */

    la_user_return(tf);
    for (;;) {}
}

/* ---- Signal delivery ----
 *
 * Called from trap.c BEFORE returning to user mode (after syscall
 * dispatch or interrupt handling).  Checks for pending non-blocked
 * signals and, if one exists, saves the current register state as a
 * signal frame on the user stack and redirects execution to the
 * registered handler (or performs the default action).
 *
 * Returns 1 if a signal was delivered (caller must re-enter the signal
 * check via ertn to the handler), 0 if nothing is pending.  */

int la_signal_pending(struct la_trap_frame *tf)
{
    struct la_proc *p = la_current_proc();
    if (!p || !p->is_user) return 0;

    uint64_t pending = p->sig_pending & ~p->sig_mask;

    /* Keep SIGCHLD as a wait/sigtimedwait event only.  The current minimal
     * signal-frame path is sufficient for simple user handlers, but shell
     * job-control style SIGCHLD delivery is not stable yet; delivering it
     * asynchronously can corrupt the return path. */
    pending &= ~(1UL << LA_SIGCHLD);

    /* SIGKILL and SIGSTOP are always delivered */
    pending |= (p->sig_pending & (1UL << LA_SIGKILL));
    pending |= (p->sig_pending & (1UL << LA_SIGSTOP));

    if (pending == 0) return 0;
    return 1;
}

#define LA_SA_NODEFER 0x40000000UL
#define LA_UC_SIGMASK_OFF 40
#define LA_UC_MC_PC_OFF 176

static uint64_t la_sigset_internal_to_user_local(uint64_t internal)
{
    uint64_t user_set = 0;
    for (int sig = 1; sig < LA_NSIG; sig++) {
        if (internal & (1UL << sig))
            user_set |= (1UL << (sig - 1));
    }
    return user_set;
}

void la_signal_deliver(struct la_trap_frame *tf)
{
    struct la_proc *p = la_current_proc();
    if (!p || !p->is_user || !p->pgtbl) return;

    uint64_t pending = p->sig_pending & ~p->sig_mask;
    pending &= ~(1UL << LA_SIGCHLD);
    /* Always deliver SIGKILL and SIGSTOP */
    pending |= (p->sig_pending & (1UL << LA_SIGKILL));
    pending |= (p->sig_pending & (1UL << LA_SIGSTOP));

    if (pending == 0) return;

    /* Find the lowest-numbered pending signal */
    int sig = 0;
    for (int s = 1; s < LA_NSIG; s++) {
        if (pending & (1UL << s)) { sig = s; break; }
    }
    if (sig == 0) return;

    p->sig_pending &= ~(1UL << sig);

    /* Default actions */
    if (p->sig_actions[sig].handler == LA_SIG_DFL) {
        /* Signals ignored by default on Linux:
         *   SIGCHLD(17), SIGURG(23), SIGWINCH(28), SIGCONT(18).
         * RT signals (32–63) are used by libc internally (SIGCANCEL,
         * SIGSETXID, etc.) — ignoring by default lets the libc install
         * its own handler before the signal can kill the process. */
        if (sig == LA_SIGCHLD || sig == LA_SIGCONT
            || sig == 23 || sig == 28 || sig >= 32) {
            return;  /* default: ignore */
        }
        /* Default: terminate */
        la_uart_puts("  signal: default kill sig=");
        la_uart_put_hex(sig);
        la_uart_puts("\n");
        la_proc_note_signal_exit(p, sig);
        la_proc_exit(-sig);
        /* not reached */
    }

    if (p->sig_actions[sig].handler == LA_SIG_IGN) {
        return;  /* explicitly ignored */
    }

    /* ---- Deliver the signal: build sigframe on user stack ---- */

    uint64_t old_mask = p->sig_mask;
    struct la_sigaction act = p->sig_actions[sig];

    /* Allocate space below the current user SP.
     * The frame must be 16-byte aligned (LoongArch ABI). */
    uint64_t old_sp = tf->gpr[LA_GPR_SP];
    uint64_t sf_size = (sizeof(struct la_sigframe) + 15) & ~15UL;
    uint64_t new_sp = old_sp - sf_size;

    /* Build the sigframe in a kernel buffer, then copy to user stack */
    struct la_sigframe sf;
    for (int i = 0; i < 32; i++) sf.gpr[i] = tf->gpr[i];
    sf.era     = tf->era;   /* save original PC */
    sf.old_sig_mask = old_mask;
    sf.sig     = (uint64_t)sig;
    for (int i = 0; i < 128; i++) sf.siginfo[i] = 0;
    for (int i = 0; i < 256; i++) sf.ucontext[i] = 0;
    ((int *)sf.siginfo)[0] = sig;  /* si_signo */
    ((int *)sf.siginfo)[1] = 0;    /* si_errno */
    ((int *)sf.siginfo)[2] = 0;    /* si_code = SI_USER */
    *(uint64_t *)&sf.ucontext[LA_UC_SIGMASK_OFF] =
        la_sigset_internal_to_user_local(old_mask);
    *(uint64_t *)&sf.ucontext[LA_UC_MC_PC_OFF] = tf->era;
    sf.tramp[0] = 0x03822c0bU;  /* li.w $a7, SYS_rt_sigreturn */
    sf.tramp[1] = 0x002b0000U;  /* syscall 0 */

    la_copy_to_user(new_sp, &sf, sizeof(sf));
    uint64_t usiginfo = new_sp + (uint64_t)((char *)sf.siginfo - (char *)&sf);
    uint64_t ucontext = new_sp + (uint64_t)((char *)sf.ucontext - (char *)&sf);
    uint64_t utramp = new_sp + (uint64_t)((char *)sf.tramp - (char *)&sf);

    /* Set up the child's context to run the signal handler:
     *   a0 = signal number (first argument to handler)
     *   a1 = siginfo pointer
     *   a2 = ucontext pointer
     *   ra = restorer address (user-space trampoline → rt_sigreturn)
     *   era = handler address
     *   sp = new stack pointer (bottom of sigframe) */
    tf->gpr[LA_GPR_SP] = new_sp;
    tf->gpr[LA_GPR_A0] = (uint64_t)sig;
    tf->gpr[LA_GPR_A1] = usiginfo;
    tf->gpr[LA_GPR_A2] = ucontext;
    tf->gpr[LA_GPR_RA] =
        (act.restorer && act.restorer != ~0ULL) ? act.restorer : utramp;
    tf->era = act.handler;
    p->sig_mask = old_mask | act.mask;
    if ((act.flags & LA_SA_NODEFER) == 0)
        p->sig_mask |= (1UL << sig);
    p->sig_mask &= ~(1UL << LA_SIGKILL);
    p->sig_mask &= ~(1UL << LA_SIGSTOP);
    /* The dispatcher will add 4 to era, so subtract 4 here so that the
     * net result is era = handler address (the dispatcher's +4 is undone
     * by entering the handler at exactly the right address).  Actually,
     * for signal delivery we want era = handler EXACTLY — not handler+4.
     * The trap.c path does tf->era += 4, so... hmm, signal delivery is
     * called BEFORE the dispatcher advances era?  Let me verify.
     *
     * Actually, signal delivery is called from trap.c AFTER the TLB-refill
     * check but the syscall path (ecode==SYS) already handles the era
     * advance.  For signals delivered to a process that just made a
     * syscall, the dispatcher has not yet returned — the signal check
     * happens inside la_trap_dispatch, after syscall handling?  No —
     * it happens as part of trap.c's exit path.
     *
     * The current flow: trap_dispatch → [syscall / timer / refill] → return.
     * Signal check must be inserted BETWEEN the dispatch and the return.
     * For the syscall path: dispatcher sets a0 = ret, era += 4, returns.
     * signal check runs AFTER this, so era is already advanced.
     * If we set era = handler, we override the already-advanced era.
     * So we DO want era = handler (not handler+4), and we must NOT
     * have the dispatcher advance it again.  Since signal delivery
     * runs after the dispatcher, we just set era directly.
     *
     * But wait — for the ISTLBR (TLB refill) path, the trap handler
     * does NOT advance era (it just returns with ISTLBR set so ertn
     * re-executes the faulting instruction).  Signal delivery after
     * a TLB refill would set era = handler, which is correct.
     *
     * For the general-exception path: the dispatch returns, era was
     * NOT advanced (non-syscall exception).  Signal delivery sets
     * era = handler directly.  Also correct.
     *
     * For the syscall path: era was already advanced by the dispatcher.
     * We override it with handler.  Correct — handler runs, returns
     * to restorer → rt_sigreturn restores the ORIGINAL context which
     * was saved in the sigframe. */
}
