#include "mod.h"

// trap/timer.c: 已存在，用于读取全局 tick
extern uint64 timer_get_ticks();

// Lab-11: Markov(S/M/L × sleep/expire/higher) 统计更新

#define MKV_BURST_S 0
#define MKV_BURST_M 1
#define MKV_BURST_L 2

#define MKV_R_SLEEP 0
#define MKV_R_EXPIRE 1
#define MKV_R_HIGHER 2
#define PROC_PGTBL_RECLAIM_ON_OOM 512

#define ROBUST_LIST_HEAD_SIZE 24
#define ROBUST_LIST_LIMIT 2048
#define FUTEX_OWNER_DIED 0x40000000u
#define FUTEX_WAITERS 0x80000000u
#define FUTEX_TID_MASK 0x3fffffffu

static int mkv_burst_class(uint64 run_ticks)
{
    // 分档阈值：
    // S: 1
    // M: 2~4
    // L: >=5
    if (run_ticks <= 1) return MKV_BURST_S;
    if (run_ticks <= 4) return MKV_BURST_M;
    return MKV_BURST_L;
}

// 要求：调用者已持有 p->lk
// 用 sched_cpu_ticks 的差值，保证多核下统计可靠
static void mkv_on_stop_locked(proc_t *p, int yield_reason, uint64 now)
{
    if (p->sched_run_start_tick == 0)
        return;

    (void)now;

    uint64 run_ticks = 0;
    if (p->sched_cpu_ticks >= p->mkv_run_start_cpu_ticks)
        run_ticks = p->sched_cpu_ticks - p->mkv_run_start_cpu_ticks;
    if (run_ticks == 0)
        run_ticks = 1;

    int burst = mkv_burst_class(run_ticks);

    int reason = MKV_R_SLEEP;
    if (yield_reason == MLFQ_YIELD_EXPIRE)
        reason = MKV_R_EXPIRE;
    else if (yield_reason == MLFQ_YIELD_HIGHER)
        reason = MKV_R_HIGHER;

    int state = burst * 3 + reason;
    if (state < 0 || state >= MLFQ_MKV_STATES)
        return;

    // 记录最近一次实际观测
    p->mkv_last_act_state = (uint8)state;

    // 预测命中统计：将“最近一次预测”与本次实际观测对比
    if (p->mkv_pred_valid) {
        if (p->mkv_last_pred_state == (uint8)state)
            p->mkv_pred_hit++;
        p->mkv_pred_valid = 0; // 消费掉一次预测，避免重复命中计数
    }

    if (p->mkv_has_prev) {
        int prev = (int)p->mkv_prev_state;
        if (prev >= 0 && prev < MLFQ_MKV_STATES)
            p->mkv_trans[prev][state]++;
    } else {
        p->mkv_has_prev = 1;
    }

    p->mkv_prev_state = (uint8)state;
}

// 这个文件通过make build生成, 是proczero对应的ELF文件
// NOTE: kernel embeds initcode bytes from this generated header.
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t *old, context_t *new);

// in trap/trap_user.c
extern void trap_user_return();

/* ------------本地变量----------- */

// 进程结构体数组 + 第一个用户进程的指针
static proc_t proc_list[N_PROC];
static proc_t *proczero;

// 全局pid + 保护它的锁
static int global_pid;
static spinlock_t pid_lk;

#define SLEEPCHAN_HINT_SLOTS 256
typedef struct sleepchan_hint {
    void *chan;
    uint32 count;
} sleepchan_hint_t;

static sleepchan_hint_t sleepchan_hints[SLEEPCHAN_HINT_SLOTS];
static uint32 sleepchan_overflow;
static spinlock_t sleepchan_lk;
static spinlock_t shared_vm_lk;

static uint64 sleepchan_hash(void *chan)
{
    return (((uint64)chan) >> 4) % SLEEPCHAN_HINT_SLOTS;
}

static void sleepchan_inc(void *chan)
{
    if (chan == NULL)
        return;

    spinlock_acquire(&sleepchan_lk);
    uint64 start = sleepchan_hash(chan);
    for (uint64 off = 0; off < SLEEPCHAN_HINT_SLOTS; off++) {
        uint64 i = (start + off) % SLEEPCHAN_HINT_SLOTS;
        if (sleepchan_hints[i].chan == chan || sleepchan_hints[i].chan == NULL) {
            sleepchan_hints[i].chan = chan;
            sleepchan_hints[i].count++;
            spinlock_release(&sleepchan_lk);
            return;
        }
    }
    sleepchan_overflow++;
    spinlock_release(&sleepchan_lk);
}

static void sleepchan_dec(void *chan)
{
    if (chan == NULL)
        return;

    spinlock_acquire(&sleepchan_lk);
    uint64 start = sleepchan_hash(chan);
    for (uint64 off = 0; off < SLEEPCHAN_HINT_SLOTS; off++) {
        uint64 i = (start + off) % SLEEPCHAN_HINT_SLOTS;
        if (sleepchan_hints[i].chan == chan) {
            if (sleepchan_hints[i].count > 0)
                sleepchan_hints[i].count--;
            if (sleepchan_hints[i].count == 0)
                sleepchan_hints[i].chan = NULL;
            spinlock_release(&sleepchan_lk);
            return;
        }
    }
    if (sleepchan_overflow > 0)
        sleepchan_overflow--;
    spinlock_release(&sleepchan_lk);
}

static int sleepchan_has_waiter(void *chan)
{
    if (chan == NULL)
        return 0;

    spinlock_acquire(&sleepchan_lk);
    uint64 start = sleepchan_hash(chan);
    for (uint64 off = 0; off < SLEEPCHAN_HINT_SLOTS; off++) {
        uint64 i = (start + off) % SLEEPCHAN_HINT_SLOTS;
        if (sleepchan_hints[i].chan == chan && sleepchan_hints[i].count > 0) {
            spinlock_release(&sleepchan_lk);
            return 1;
        }
    }
    int has = sleepchan_overflow > 0;
    spinlock_release(&sleepchan_lk);
    return has;
}

static int proc_user_read(pgtbl_t pgtbl, uint64 src, void *dst, uint32 len)
{
    uint32 copied = 0;
    while (copied < len) {
        pte_t *pte = vm_getpte(pgtbl, src, false);
        if (pte == NULL || !(*pte & PTE_V))
            return -1;

        uint64 offset = src % PGSIZE;
        uint64 copy_len = PGSIZE - offset;
        if (copy_len > len - copied)
            copy_len = len - copied;

        memmove((uint8 *)dst + copied, (void *)(PTE_TO_PA(*pte) + offset), copy_len);
        src += copy_len;
        copied += (uint32)copy_len;
    }
    return 0;
}

static int proc_user_write(pgtbl_t pgtbl, uint64 dst, const void *src, uint32 len)
{
    uint32 copied = 0;
    while (copied < len) {
        pte_t *pte = vm_getpte(pgtbl, dst, false);
        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_W))
            return -1;

        uint64 offset = dst % PGSIZE;
        uint64 copy_len = PGSIZE - offset;
        if (copy_len > len - copied)
            copy_len = len - copied;

        memmove((void *)(PTE_TO_PA(*pte) + offset), (const uint8 *)src + copied, copy_len);
        dst += copy_len;
        copied += (uint32)copy_len;
    }
    return 0;
}

static int proc_user_read_u64(pgtbl_t pgtbl, uint64 addr, uint64 *out)
{
    return proc_user_read(pgtbl, addr, out, sizeof(*out));
}

static int proc_user_read_u32(pgtbl_t pgtbl, uint64 addr, uint32 *out)
{
    return proc_user_read(pgtbl, addr, out, sizeof(*out));
}

static int proc_user_write_u32(pgtbl_t pgtbl, uint64 addr, uint32 val)
{
    return proc_user_write(pgtbl, addr, &val, sizeof(val));
}

static void proc_robust_mark_futex(proc_t *p, uint64 list_entry, int64 futex_offset)
{
    if (p == NULL || p->pgtbl == NULL || list_entry == 0)
        return;

    uint64 futex_addr = list_entry + (uint64)futex_offset;
    uint32 old = 0;
    if (proc_user_read_u32(p->pgtbl, futex_addr, &old) < 0)
        return;
    if ((old & FUTEX_TID_MASK) != (uint32)p->pid)
        return;

    uint32 updated = (old & FUTEX_WAITERS) | FUTEX_OWNER_DIED;
    if (proc_user_write_u32(p->pgtbl, futex_addr, updated) == 0)
        proc_wakeup_force((void *)futex_addr);
}

static void proc_exit_robust_list_values(proc_t *p, uint64 head, uint64 len)
{
    if (p == NULL || p->pgtbl == NULL || head == 0)
        return;
    if (len != ROBUST_LIST_HEAD_SIZE)
        return;

    uint64 next = 0;
    uint64 offset_raw = 0;
    uint64 pending = 0;
    if (proc_user_read_u64(p->pgtbl, head, &next) < 0 ||
        proc_user_read_u64(p->pgtbl, head + 8, &offset_raw) < 0 ||
        proc_user_read_u64(p->pgtbl, head + 16, &pending) < 0)
        return;

    int64 futex_offset = (int64)offset_raw;
    for (uint32 seen = 0; next != 0 && next != head && seen < ROBUST_LIST_LIMIT; seen++) {
        uint64 entry = next;
        if (proc_user_read_u64(p->pgtbl, entry, &next) < 0)
            break;
        proc_robust_mark_futex(p, entry, futex_offset);
    }

    if (pending != 0 && pending != head)
        proc_robust_mark_futex(p, pending, futex_offset);
}

static void proc_exit_robust_list(proc_t *p)
{
    if (p == NULL)
        return;
    proc_exit_robust_list_values(p, p->robust_list_head, p->robust_list_len);
}

static void proc_free_mmap_list(proc_t *p)
{
    mmap_region_t *mmap = p->mmap;
    while (mmap != NULL) {
        mmap_region_t *next = mmap->next;
        mmap_region_free(mmap);
        mmap = next;
    }
    p->mmap = NULL;
}

static int proc_has_live_vm_sibling(proc_t *target)
{
    proc_t *owner = target->vm_owner ? target->vm_owner : target;
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        if (p == target)
            continue;

        int live = 0;
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->vm_owner == owner)
            live = 1;
        spinlock_release(&p->lk);
        if (live)
            return 1;
    }
    return 0;
}

#define RLIMIT_STACK_INDEX 3

static void proc_init_rlimits(proc_t *p)
{
    for (int i = 0; i < N_RLIMIT; i++) {
        p->rlimit_cur[i] = (uint64)-1;
        p->rlimit_max[i] = (uint64)-1;
    }
    p->rlimit_cur[RLIMIT_STACK_INDEX] = 1 * 1024 * 1024;
    p->rlimit_cur[RLIMIT_NOFILE_INDEX] = N_OPEN_FILE_PER_PROC;
    p->rlimit_max[RLIMIT_NOFILE_INDEX] = N_OPEN_FILE_PER_PROC;
}

/* 获取一个pid */
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&pid_lk);
    assert(global_pid > 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&pid_lk);
    return tmp;
}

/* 释放进程锁 + trap_user_return */
static void proc_return()
{
    proc_t *p = myproc();

    if (p->pid != 1) {
//        printf("proc_return: pid=%d a0=%d epc=%p\n", p->pid, (int)p->tf->a0, (void*)p->tf->user_to_kern_epc);
    }

    spinlock_release(&p->lk); // 先释放锁
    // 如果是第一个进程(proczero)，则进行特殊初始化
    if (p->pid == 1) {
        fs_init();
        p->open_file[0] = file_open("/dev/stdin", FILE_OPEN_READ);
        p->open_file[1] = file_open("/dev/stdout", FILE_OPEN_WRITE);
        p->open_file[2] = file_open("/dev/stderr", FILE_OPEN_WRITE);
        p->cwd = inode_get(ext4_is_active() ? EXT4_ROOT_INO : ROOT_INODE);
    }

    proc_exit_group_if_requested();

    // 回到用户态
    trap_user_return();
}

/* 进程模块初始化 */
void proc_init()
{    
    // 初始化全局 pid 与其锁
    global_pid = 1;
    spinlock_init(&pid_lk, "pid_lock");
    memset(sleepchan_hints, 0, sizeof(sleepchan_hints));
    sleepchan_overflow = 0;
    spinlock_init(&sleepchan_lk, "sleepchan");
    spinlock_init(&shared_vm_lk, "shared_vm");

    // 初始化MLFQ调度器
    mlfq_init();

    // 初始化进程数组
    for (int i = 0; i < N_PROC; i++) {
        memset(&proc_list[i], 0, sizeof(proc_t)); // 初始化为null
        spinlock_init(&proc_list[i].lk, "proc_lock");
    }
    // proczero 指向第一个槽位
    proczero = &proc_list[0];
}

/* 
    申请一个UNUSED进程结构体(返回时带锁)
    并执行通用的初始化逻辑
*/
proc_t *proc_alloc()
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        // 试图占用该进程结构体
        spinlock_acquire(&p->lk);
        if (p->state == UNUSED) {
            memset(p->name, 0, sizeof(p->name));
            p->pid = alloc_pid();
            p->exit_code = 0;
            p->sleep_space = NULL;
            p->sleep_deadline = 0;
            p->sleep_timedout = 0;
            p->parent = myproc();
            p->pgtbl = NULL;
            p->heap_top = 0;
            p->ustack_npage = 0;
            p->mmap = NULL;
            p->vm_owner = p;
            p->shared_vm = 0;
            p->thread_group = 0;
            p->tf = NULL;

            // MLFQ 初始化
            p->mlfq_level = 0;
            p->mlfq_ticks_left = 0;
            p->mlfq_in_readyq = 0;
            p->mlfq_yield_reason = MLFQ_YIELD_NONE;
            p->mlfq_wait_ticks = 0;

            // Lab-11: Per-CPU Runqueue + Lazy Aging
            p->mlfq_cpu = 0;
            p->mlfq_age_start_tick = 0;

            // Lab-11: Markov 统计
            p->mkv_has_prev = 0;
            p->mkv_prev_state = 0;
            p->mkv_pad = 0;
            memset(p->mkv_trans, 0, sizeof(p->mkv_trans));

            p->mkv_pred_valid = 0;
            p->mkv_last_pred_state = 0;
            p->mkv_last_act_state = 0;
            p->mkv_pad2 = 0;
            p->mkv_pred_total = 0;
            p->mkv_pred_hit = 0;
            p->mkv_l2_boost_count = 0;
            p->mkv_run_start_cpu_ticks = 0;

            // 调度统计初始化
            p->sched_last_ready_tick = 0;
            p->sched_run_start_tick = 0;
            p->sched_first_run_tick = 0;
            p->sched_cpu_ticks = 0;
            p->sched_wait_sum = 0;
            p->sched_wait_max = 0;
            p->sched_run_count = 0;
            p->sched_ready_count = 0;
            p->sched_ctx_switches = 0;
            p->sched_preempt_expire = 0;
            p->sched_preempt_higher = 0;
            p->sched_yield_voluntary = 0;
            p->sched_sleep_count = 0;

            // 预设内核栈与上下文
            p->kstack = (uint64)KSTACK(i);
            p->ctx.ra = (uint64)proc_return; // 切入到该进程时，从这里返回到用户态入口
            p->ctx.sp = p->kstack + 2 * PGSIZE;

            // Signal 初始化
            p->cwd = NULL;
            for (int fd = 0; fd < N_OPEN_FILE_PER_PROC; fd++) {
                p->open_file[fd] = NULL;
                p->fd_cloexec[fd] = 0;
            }
            proc_init_rlimits(p);

            memset(p->sig_handler, 0, sizeof(p->sig_handler));
            memset(p->sig_flags, 0, sizeof(p->sig_flags));
            p->sig_restorer = 0;
            p->sig_pending = 0;
            p->sig_mask = 0;
            p->sig_delivering = 0;
            p->group_exit_pending = 0;
            p->group_exit_code = 0;
            p->clear_child_tid = 0;
            p->robust_list_head = 0;
            p->robust_list_len = 0;
            p->reparented_to_init = 0;
            p->itimer_expire = 0;
            p->itimer_interval = 0;
            p->ub_looper_secs = 0;

            return p; // 保持锁定返回
        }else{
            spinlock_release(&p->lk);
        }
    }
    return NULL;
}

/* 
    回收一个进程结构体并释放它包含的资源
    tips: 调用者需要持有进程锁
*/
void proc_free(proc_t *p)
{
    int has_live_vm_sibling = proc_has_live_vm_sibling(p);

    // 释放用户态页表相关资源
    if (p->pgtbl) {
        if (has_live_vm_sibling)
            uvm_destroy_shared_pgtbl(p->pgtbl);
        else
            uvm_destroy_pgtbl(p->pgtbl);
        p->pgtbl = NULL;
    }
    if (!has_live_vm_sibling && p->mmap)
        proc_free_mmap_list(p);

    // open_file 和 cwd 已由 proc_exit 关闭，这里只做防御性清理
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++)
        p->open_file[i] = NULL;
    memset(p->fd_cloexec, 0, sizeof(p->fd_cloexec));
    memset(p->rlimit_cur, 0, sizeof(p->rlimit_cur));
    memset(p->rlimit_max, 0, sizeof(p->rlimit_max));
    p->cwd = NULL;

    // 清空结构体并置为 UNUSED
    memset(p->name, 0, sizeof(p->name));
    p->pid = 0;
    p->parent = NULL;
    p->tf = NULL;
    p->exit_code = 0;
    p->sleep_space = NULL;
    p->sleep_deadline = 0;
    p->sleep_timedout = 0;
    p->pgtbl = NULL;
    p->heap_top = 0;
    p->ustack_npage = 0;
    p->mmap = NULL;
    p->vm_owner = NULL;
    p->shared_vm = 0;
    p->thread_group = 0;
    p->kstack = 0;
    memset(&p->ctx, 0, sizeof(p->ctx));

    // MLFQ 清理
    p->mlfq_level = 0;
    p->mlfq_ticks_left = 0;
    p->mlfq_in_readyq = 0;
    p->mlfq_yield_reason = MLFQ_YIELD_NONE;
    p->mlfq_wait_ticks = 0;

    // Lab-11: Per-CPU Runqueue + Lazy Aging
    p->mlfq_cpu = 0;
    p->mlfq_age_start_tick = 0;

    // Lab-11: Markov 统计
    p->mkv_has_prev = 0;
    p->mkv_prev_state = 0;
    p->mkv_pad = 0;
    memset(p->mkv_trans, 0, sizeof(p->mkv_trans));

    p->mkv_pred_valid = 0;
    p->mkv_last_pred_state = 0;
    p->mkv_last_act_state = 0;
    p->mkv_pad2 = 0;
    p->mkv_pred_total = 0;
    p->mkv_pred_hit = 0;
    p->mkv_l2_boost_count = 0;
    p->mkv_run_start_cpu_ticks = 0;

    // 调度统计清理
    p->sched_last_ready_tick = 0;
    p->sched_run_start_tick = 0;
    p->sched_first_run_tick = 0;
    p->sched_cpu_ticks = 0;
    p->sched_wait_sum = 0;
    p->sched_wait_max = 0;
    p->sched_run_count = 0;
    p->sched_ready_count = 0;
    p->sched_ctx_switches = 0;
    p->sched_preempt_expire = 0;
    p->sched_preempt_higher = 0;
    p->sched_yield_voluntary = 0;
    p->sched_sleep_count = 0;

    // Signal 清理
    memset(p->sig_handler, 0, sizeof(p->sig_handler));
    memset(p->sig_flags, 0, sizeof(p->sig_flags));
    p->sig_restorer = 0;
    p->sig_pending = 0;
    p->sig_mask = 0;
    p->sig_delivering = 0;
    p->group_exit_pending = 0;
    p->group_exit_code = 0;
    p->clear_child_tid = 0;
    p->robust_list_head = 0;
    p->robust_list_len = 0;
    p->reparented_to_init = 0;
    p->itimer_expire = 0;
    p->itimer_interval = 0;
    p->ub_looper_secs = 0;

    p->state = UNUSED;
}

static void proc_reap_background_zombies(void)
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p != proczero && p->state == ZOMBIE &&
            (p->shared_vm || (p->parent == proczero && p->reparented_to_init))) {
            proc_free(p);
            spinlock_release(&p->lk);
            continue;
        }
        spinlock_release(&p->lk);
    }
}

/* 
    获得一个初始化过的用户页表
    完成trapframe和trampoline的映射
*/
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    // 1. 分配一页作为用户根页表
    pgtbl_t upgtbl = (pgtbl_t)pmem_alloc(true);
    if (!upgtbl) {
        buffer_freemem(PROC_PGTBL_RECLAIM_ON_OOM);
        upgtbl = (pgtbl_t)pmem_alloc(true);
    }
    if (!upgtbl)
        return NULL;
    memset(upgtbl, 0, PGSIZE);  // 清零

    // 2. 在用户页表中映射 trampoline 与 trapframe
    if (vm_try_mappages(upgtbl, (uint64)TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X) < 0 ||
        vm_try_mappages(upgtbl, (uint64)TRAPFRAME, (uint64)trapframe, PGSIZE, PTE_R | PTE_W) < 0) {
        uvm_destroy_pgtbl(upgtbl);
        return NULL;
    }

    return upgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问

	注意: 用用户空间的地址映射需要标记 PTE_U
*/
void proc_make_first()
{
    // 通过通用分配接口申请 proczero（返回时持有锁）
    proc_t *p = proc_alloc();
    if (!p) panic("proc_make_first: proc_alloc failed");
    p->pid = 1; // proczero 固定为 pid=1

    // 1. 申请trapframe的物理页
    trapframe_t *tf = (trapframe_t *)pmem_alloc(false);
    if (!tf) {
        panic("proc_make_first: pmem_alloc for trapframe failed");
    }
    memset(tf, 0, PGSIZE);  // 清零

    // 2. 调用proc_pgtbl_init：申请用户页表，并映射trampoline和trapframe
    pgtbl_t upgtbl = proc_pgtbl_init((uint64)tf);
    if (!upgtbl) {
        panic("proc_make_first: proc_pgtbl_init failed");
    }

    // 3. 准备用户地址空间其他部分
    // 3.1 空洞（1页） [0, PGSIZE) 不映射
    
    // 3.2 为ELF文件(code + data)申请一个物理页[PGSIZE, 2*PGSIZE)、进行数据转移、完成映射
    const uint64 UCODE_VA = PGSIZE; // 起始虚拟地址
    void *ucode_pa = pmem_alloc(false);
    if (!ucode_pa) {
        panic("proc_make_first: pmem_alloc for ucode failed");
    }
    memset(ucode_pa, 0, PGSIZE);  // 清零
    if (initcode_len > PGSIZE) { // 长度检查
        panic("proc_make_first: initcode too big");
    }
    memmove(ucode_pa, initcode, (uint32)initcode_len);
    vm_mappages(upgtbl, UCODE_VA, (uint64)ucode_pa, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);

    // 3.3 用户栈ustack（1页，在trapframe之下） [TRAPFRAME - PGSIZE, TRAPFRAME)
    const uint64 USTACK_TOP = (uint64)TRAPFRAME; // 栈顶
    const uint64 USTACK_VA = USTACK_TOP - PGSIZE; // 栈底
    void *ustack_pa = pmem_alloc(false);
    if (!ustack_pa) {
        panic("proc_make_first: pmem_alloc for ustack failed");
    }
    memset(ustack_pa, 0, PGSIZE);
    vm_mappages(upgtbl,USTACK_VA,(uint64)ustack_pa,PGSIZE,PTE_R | PTE_W | PTE_U);

    // 4. 填充 proczero 结构体
    // strncpy(p->name, "proczero", sizeof(p->name)-1);
    p->pgtbl = upgtbl;
    p->heap_top = 2 * PGSIZE;
    p->ustack_npage = 1;
    p->tf = tf;
    p->mmap = NULL;
    p->state = RUNNABLE;

    // 调度统计：进入就绪态
    p->sched_last_ready_tick = timer_get_ticks();
    p->mlfq_age_start_tick = p->sched_last_ready_tick;
    p->sched_ready_count++;

    // 5. 设置 trapframe 中的入口与用户栈
    tf->user_to_kern_epc = UCODE_VA;
    tf->sp = USTACK_TOP;

    // 6. 记录为 proczero 并解锁
    proczero = p;
    spinlock_release(&p->lk);

    // 入MLFQ就绪队列(避免持有 p->lk 时拿 mlfq 锁)
    mlfq_on_new(p);
}

/*
    父进程产生子进程
    UNUSED -> RUNNABLE
*/
int proc_fork_with_stack(uint64 child_stack)
{
    proc_t *parent = myproc();
    proc_t *child = proc_alloc();
    if (!child) return -1;

    // 设置进程名字
    // strncpy(child->name, parent->name, sizeof(child->name)-1);

    // 复制trapframe
    trapframe_t *tf = (trapframe_t *)pmem_alloc(false);
    if (!tf) { spinlock_release(&child->lk); return -1; }
    *tf = *parent->tf;
    tf->a0=0;
    if (child_stack != 0)
        tf->sp = child_stack;

    // 填充子进程结构体
    child->tf = tf;
    child->parent = parent;
    child->exit_code = 0;
    child->pgtbl = proc_pgtbl_init((uint64)tf);
    if (!child->pgtbl) {
        pmem_free((uint64)tf, false);
        child->tf = NULL;
        spinlock_release(&child->lk);
        return -1;
    }
    child->heap_top = parent->heap_top;
    child->ustack_npage = parent->ustack_npage;
    child->mmap = NULL; // 子进程初始无mmap
    child->vm_owner = child;
    child->shared_vm = 0;
    child->thread_group = 0;
    child->state = RUNNABLE;

    // 调度统计：进入就绪态
    child->sched_last_ready_tick = timer_get_ticks();
    child->mlfq_age_start_tick = child->sched_last_ready_tick;
    child->sched_ready_count++;

    // 复制页表
    if (uvm_copy_pgtbl(parent->pgtbl, child->pgtbl, parent->heap_top, parent->ustack_npage, parent->mmap) < 0) {
        proc_free(child);
        spinlock_release(&child->lk);
        return -1;
    }

    // 继承open_file
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++) {
        if (parent->open_file[i]) {
            child->open_file[i] = file_dup(parent->open_file[i]);
        } else {
            child->open_file[i] = NULL;
        }
        child->fd_cloexec[i] = parent->fd_cloexec[i];
    }
    memcpy(child->rlimit_cur, parent->rlimit_cur, sizeof(parent->rlimit_cur));
    memcpy(child->rlimit_max, parent->rlimit_max, sizeof(parent->rlimit_max));

    // 继承信号处理器
    memcpy(child->sig_handler, parent->sig_handler, sizeof(parent->sig_handler));
    memcpy(child->sig_flags, parent->sig_flags, sizeof(parent->sig_flags));
    child->sig_restorer = parent->sig_restorer;
    child->sig_pending = 0;
    child->sig_mask = parent->sig_mask;
    child->sig_delivering = 0;
    child->group_exit_pending = 0;
    child->group_exit_code = 0;
    child->clear_child_tid = 0;
    child->robust_list_head = 0;
    child->robust_list_len = 0;
    child->itimer_expire = 0;
    child->itimer_interval = 0;
    child->ub_looper_secs = 0;
    // 继承cwd
    if (parent->cwd) {
        child->cwd = inode_dup(parent->cwd);
    } else {
        child->cwd = NULL;
    }

    int pid = child->pid;
    spinlock_release(&child->lk);

    // 调试信息：记录父子 pid 和子进程当前状态
    // printf("proc_fork: parent=%d child=%d state=%d\n", parent ? parent->pid : -1, pid, child->state);

    // 入MLFQ就绪队列(避免持有 child->lk 时拿 mlfq 锁)
    // printf("proc_fork: before mlfq_on_new child=%d\n", pid);
    mlfq_on_new(child);
    // printf("proc_fork: after mlfq_on_new child=%d\n", pid);

    return pid;
}

int proc_fork()
{
    return proc_fork_with_stack(0);
}

/*
    用户态时钟中断触发: 更新时间片
    返回 1 表示需要让出 CPU
*/
int proc_on_tick(void)
{
    proc_t *p = myproc();
    if (!p)
        return 0;

    if (p->state != RUNNING)
        return 0;

    // aging: 更新就绪队列中的等待时间并按阈值提升
    mlfq_age_tick();

    if (p->mlfq_ticks_left > 0)
        p->mlfq_ticks_left--;

    if (p->mlfq_ticks_left <= 0) {
        p->mlfq_yield_reason = MLFQ_YIELD_EXPIRE;
        return 1;
    }

    // 如果更高优先级队列出现就绪任务, 也允许抢占
    if (mlfq_has_higher(p->mlfq_level)) {
        p->mlfq_yield_reason = MLFQ_YIELD_HIGHER;
        return 1;
    }

    return 0;
}

void proc_shared_vm_sync_mmap(mmap_region_t *old_head, mmap_region_t *new_head)
{
    proc_t *caller = myproc();
    if (caller == NULL)
        return;

    proc_t *owner = caller->vm_owner ? caller->vm_owner : caller;
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->vm_owner == owner &&
            (p == caller || p->mmap == old_head)) {
            p->mmap = new_head;
        }
        spinlock_release(&p->lk);
    }
}

void proc_shared_vm_sync_heap_grow(uint64 old_top, uint64 new_top)
{
    proc_t *caller = myproc();
    if (caller == NULL || new_top <= old_top)
        return;

    proc_t *owner = caller->vm_owner ? caller->vm_owner : caller;
    uint64 old_pages = (old_top + PGSIZE - 1) / PGSIZE;
    uint64 new_pages = (new_top + PGSIZE - 1) / PGSIZE;

    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state == UNUSED || p->state == ZOMBIE || p->vm_owner != owner) {
            spinlock_release(&p->lk);
            continue;
        }

        if (p->heap_top < new_top)
            p->heap_top = new_top;

        if (p == caller) {
            spinlock_release(&p->lk);
            continue;
        }

        for (uint64 page = old_pages; page < new_pages; page++) {
            uint64 va = page * PGSIZE;
            pte_t *src = vm_getpte(caller->pgtbl, va, false);
            if (src == NULL || !(*src & PTE_V))
                continue;

            pte_t *dst = vm_getpte(p->pgtbl, va, false);
            if (dst != NULL && (*dst & PTE_V))
                continue;

            uint64 pa = (uint64)PTE_TO_PA(*src);
            int flags = (int)PTE_FLAGS(*src);
            vm_try_mappages(p->pgtbl, va, pa, PGSIZE, flags);
        }
        spinlock_release(&p->lk);
    }
}

uint64 proc_shared_vm_sync_heap_shrink(uint64 old_top, uint64 requested_top)
{
    proc_t *caller = myproc();
    if (caller == NULL)
        return (uint64)-1;

    const uint64 min_heap = 2 * PGSIZE;
    if (old_top <= min_heap)
        return (uint64)-1;

    uint64 new_top = requested_top < min_heap ? min_heap : requested_top;
    if (new_top >= old_top)
        return old_top;

    uint64 old_pages = (old_top + PGSIZE - 1) / PGSIZE;
    uint64 new_pages = (new_top + PGSIZE - 1) / PGSIZE;

    for (uint64 page = new_pages; page < old_pages; page++) {
        uint64 va = page * PGSIZE;
        uint64 pa = proc_shared_vm_unmap_page(va);
        if (pa != 0)
            pmem_free(pa, false);
    }

    proc_t *owner = caller->vm_owner ? caller->vm_owner : caller;
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->state != ZOMBIE &&
            p->vm_owner == owner && p->heap_top > new_top) {
            p->heap_top = new_top;
        }
        spinlock_release(&p->lk);
    }

    return new_top;
}

int proc_shared_vm_lookup_page(uint64 va, uint64 *pa_out, int *flags_out)
{
    proc_t *caller = myproc();
    if (caller == NULL || pa_out == NULL || flags_out == NULL)
        return -1;

    proc_t *owner = caller->vm_owner ? caller->vm_owner : caller;
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state == UNUSED || p->state == ZOMBIE ||
            p->vm_owner != owner || p->pgtbl == NULL) {
            spinlock_release(&p->lk);
            continue;
        }

        pte_t *pte = vm_getpte(p->pgtbl, va, false);
        if (pte != NULL && (*pte & PTE_V) && !PTE_CHECK(*pte)) {
            *pa_out = (uint64)PTE_TO_PA(*pte);
            *flags_out = (int)PTE_FLAGS(*pte);
            spinlock_release(&p->lk);
            return 0;
        }
        spinlock_release(&p->lk);
    }
    return -1;
}

int proc_shared_vm_map_page(uint64 va, uint64 pa, int flags)
{
    proc_t *caller = myproc();
    if (caller == NULL)
        return -1;

    int ret = 0;
    proc_t *owner = caller->vm_owner ? caller->vm_owner : caller;
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state == UNUSED || p->state == ZOMBIE ||
            p->vm_owner != owner || p->pgtbl == NULL) {
            spinlock_release(&p->lk);
            continue;
        }

        pte_t *pte = vm_getpte(p->pgtbl, va, false);
        if (pte == NULL || !(*pte & PTE_V)) {
            if (vm_try_mappages(p->pgtbl, va, pa, PGSIZE, flags) < 0)
                ret = -1;
        }
        spinlock_release(&p->lk);
    }
    return ret;
}

int proc_shared_vm_fault_page(uint64 va, int flags)
{
    proc_t *caller = myproc();
    if (caller == NULL)
        return -1;

    proc_t *owner = caller->vm_owner ? caller->vm_owner : caller;
    uint64 pa = 0;
    int map_flags = flags;
    int allocated = 0;
    int ret = 0;

    spinlock_acquire(&shared_vm_lk);

    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->state != ZOMBIE &&
            p->vm_owner == owner && p->pgtbl != NULL) {
            pte_t *pte = vm_getpte(p->pgtbl, va, false);
            if (pte != NULL && (*pte & PTE_V) && !PTE_CHECK(*pte)) {
                pa = (uint64)PTE_TO_PA(*pte);
                map_flags = (int)PTE_FLAGS(*pte);
                spinlock_release(&p->lk);
                break;
            }
        }
        spinlock_release(&p->lk);
    }

    if (pa == 0) {
        pa = (uint64)pmem_alloc(false);
        if (pa == 0) {
            spinlock_release(&shared_vm_lk);
            return -1;
        }
        memset((void *)pa, 0, PGSIZE);
        allocated = 1;
    }

    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->state != ZOMBIE &&
            p->vm_owner == owner && p->pgtbl != NULL) {
            pte_t *pte = vm_getpte(p->pgtbl, va, false);
            if (pte == NULL || !(*pte & PTE_V)) {
                if (vm_try_mappages(p->pgtbl, va, pa, PGSIZE, map_flags) < 0)
                    ret = -1;
            }
        }
        spinlock_release(&p->lk);
    }

    if (ret < 0 && allocated) {
        for (int i = 0; i < N_PROC; i++) {
            proc_t *p = &proc_list[i];
            spinlock_acquire(&p->lk);
            if (p->state != UNUSED && p->vm_owner == owner && p->pgtbl != NULL) {
                pte_t *pte = vm_getpte(p->pgtbl, va, false);
                if (pte != NULL && (*pte & PTE_V) &&
                    (uint64)PTE_TO_PA(*pte) == pa)
                    *pte = 0;
            }
            spinlock_release(&p->lk);
        }
    }

    spinlock_release(&shared_vm_lk);

    if (ret < 0 && allocated)
        pmem_free(pa, false);
    return ret;
}
uint64 proc_shared_vm_unmap_page(uint64 va)
{
    proc_t *caller = myproc();
    if (caller == NULL)
        return 0;

    uint64 first_pa = 0;
    proc_t *owner = caller->vm_owner ? caller->vm_owner : caller;
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state == UNUSED || p->state == ZOMBIE ||
            p->vm_owner != owner || p->pgtbl == NULL) {
            spinlock_release(&p->lk);
            continue;
        }

        pte_t *pte = vm_getpte(p->pgtbl, va, false);
        if (pte != NULL && (*pte & PTE_V) && !PTE_CHECK(*pte)) {
            uint64 pa = (uint64)PTE_TO_PA(*pte);
            if (first_pa == 0)
                first_pa = pa;
            *pte = 0;
        }
        spinlock_release(&p->lk);
    }
    sfence_vma();
    return first_pa;
}

/*
    进程主动放弃CPU控制权
    RUNNING->RUNNABLE
*/
void proc_yield()
{
    proc_t *p = myproc();
    int reason;

    uint64 now = timer_get_ticks();

    // 统一锁顺序: 先拿 MLFQ 锁，再拿 p->lk
    // 这样避免与调度器(mlfq -> p->lk)发生锁反转
    mlfq_lock();
    spinlock_acquire(&p->lk);

    p->state = RUNNABLE;
    // 防御：RUNNING 进程不应当在就绪队列中；若标志残留会导致入队被跳过，从而永远跑不到。
    p->mlfq_in_readyq = 0;
    reason = p->mlfq_yield_reason;
    if (reason == MLFQ_YIELD_NONE)
        reason = MLFQ_YIELD_EXPIRE;
    p->mlfq_yield_reason = MLFQ_YIELD_NONE;

    // 调度统计：按原因计数 + 重新进入就绪态
    p->sched_last_ready_tick = now;
    p->mlfq_age_start_tick = now;
    p->sched_ready_count++;
    if (reason == MLFQ_YIELD_EXPIRE) p->sched_preempt_expire++;
    else if (reason == MLFQ_YIELD_HIGHER) p->sched_preempt_higher++;
    else if (reason == MLFQ_YIELD_VOLUNTARY) p->sched_yield_voluntary++;

    // Lab-11: Markov（slice 结束：expire/higher）
    mkv_on_stop_locked(p, reason, now);

    // 入队(当前已持有 mlfq_lock)
    mlfq_on_yield_locked(p, reason);
    mlfq_unlock();

    // 保持持锁切换; 被再次调度回来后才会继续向下执行
    proc_sched();
    spinlock_release(&p->lk);
}

/*
    当父进程退出时, 让它的所有子进程认proczero为父
    因为proczero永不退出, 可以回收子进程的资源
*/
static void proc_reparent(proc_t *parent)
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        if (p == parent) continue;
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->parent == parent) {
            p->parent = proczero;
            p->reparented_to_init = 1;
        }
        spinlock_release(&p->lk);
    }
}

static void proc_kill_descendants(proc_t *parent)
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        bool child = false;

        if (p == parent)
            continue;
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->parent == parent)
            child = true;
        spinlock_release(&p->lk);

        if (child) {
            file_t *files[N_OPEN_FILE_PER_PROC];
            inode_t *cwd;
            uint64 clear_child_tid;
            uint64 robust_list_head;
            uint64 robust_list_len;

            proc_kill_descendants(p);

            memset(files, 0, sizeof(files));
            cwd = NULL;
            clear_child_tid = 0;
            robust_list_head = 0;
            robust_list_len = 0;
            spinlock_acquire(&p->lk);
            if (p->state != UNUSED && p->state != ZOMBIE) {
                robust_list_head = p->robust_list_head;
                robust_list_len = p->robust_list_len;
                p->robust_list_head = 0;
                p->robust_list_len = 0;
                for (int fd = 0; fd < N_OPEN_FILE_PER_PROC; fd++) {
                    files[fd] = p->open_file[fd];
                    p->open_file[fd] = NULL;
                    p->fd_cloexec[fd] = 0;
                }
                cwd = p->cwd;
                p->cwd = NULL;
                p->exit_code = -SIGALRM;
                p->sleep_space = NULL;
                p->itimer_expire = 0;
                p->itimer_interval = 0;
                p->ub_looper_secs = 0;
                clear_child_tid = p->clear_child_tid;
                p->clear_child_tid = 0;
                p->state = ZOMBIE;
            }
            spinlock_release(&p->lk);

            proc_exit_robust_list_values(p, robust_list_head, robust_list_len);

            if (clear_child_tid != 0) {
                uint32 zero = 0;
                uvm_copyout(p->pgtbl, clear_child_tid, (uint64)&zero, sizeof(zero));
                proc_wakeup_force((void *)clear_child_tid);
            }
            for (int fd = 0; fd < N_OPEN_FILE_PER_PROC; fd++) {
                if (files[fd] != NULL)
                    file_close(files[fd]);
            }
            if (cwd != NULL)
                inode_put(cwd);
        }
    }
}

/*
    唤醒等待呼叫的进程
    由proc_exit调用
    tips: 调用者需要持有p的进程锁
*/
static void proc_try_wakeup(proc_t *p)
{
    proc_t *parent = p->parent;
    if (!parent) return;
    // 唤醒等待“自己”的父进程
    bool woke = false;
    spinlock_acquire(&parent->lk);
    bool thread_exit = p->shared_vm && p->thread_group;
    bool sigchld = !thread_exit && parent->sig_handler[SIGCHLD] > 1;
    if (sigchld)
        parent->sig_pending |= (1UL << (SIGCHLD - 1));
    if (parent->state == SLEEPING && (parent->sleep_space == parent || sigchld)) {
        parent->state = RUNNABLE;
        parent->sleep_space = NULL;
        parent->sched_last_ready_tick = timer_get_ticks();
        parent->mlfq_age_start_tick = parent->sched_last_ready_tick;
        parent->sched_ready_count++;
        parent->mlfq_in_readyq = 0;
        woke = true;
    }
    spinlock_release(&parent->lk);

    // 修复：父进程变为 RUNNABLE 后，需要重新进入就绪队列，否则在 MLFQ 下会“永远跑不到”
    if (woke)
        mlfq_on_wakeup(parent);
}

void proc_check_itimers(uint64 now)
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        bool woke = false;

        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->state != ZOMBIE && p->itimer_expire != 0 &&
            now >= p->itimer_expire) {
            p->sig_pending |= (1UL << (SIGALRM - 1));
            if (p->itimer_interval != 0)
                p->itimer_expire = now + p->itimer_interval;
            else
                p->itimer_expire = 0;

            if (p->state == SLEEPING) {
                p->state = RUNNABLE;
                p->sleep_space = NULL;
                p->sched_last_ready_tick = timer_get_ticks();
                p->mlfq_age_start_tick = p->sched_last_ready_tick;
                p->sched_ready_count++;
                p->mlfq_in_readyq = 0;
                woke = true;
            }
        }
        spinlock_release(&p->lk);

        if (woke)
            mlfq_on_wakeup(p);
    }
}

void proc_check_sleep_deadlines(uint64 now)
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        int should_enqueue = 0;

        spinlock_acquire(&p->lk);
        if (p->state == SLEEPING && p->sleep_deadline != 0 &&
            now >= p->sleep_deadline) {
            p->sleep_timedout = 1;
            p->state = RUNNABLE;
            p->sleep_space = NULL;
            p->sleep_deadline = 0;
            p->sched_last_ready_tick = timer_get_ticks();
            p->mlfq_age_start_tick = p->sched_last_ready_tick;
            p->sched_ready_count++;
            p->mlfq_in_readyq = 0;
            should_enqueue = 1;
        }
        spinlock_release(&p->lk);

        if (should_enqueue)
            mlfq_on_wakeup(p);
    }
}

/*
    进程退出
    RUNNING -> ZOMBIE
*/
void proc_exit(int exit_code)
{
    proc_t *p = myproc();
    proc_exit_robust_list(p);
    p->robust_list_head = 0;
    p->robust_list_len = 0;
    if (p->clear_child_tid != 0) {
        uint32 zero = 0;
        uvm_copyout(p->pgtbl, p->clear_child_tid, (uint64)&zero, sizeof(zero));
        proc_wakeup_force((void *)p->clear_child_tid);
    }
    // 关闭所有打开的文件描述符（必须在获取进程锁前完成，因为 file_close 可能 sleep）
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++) {
        if (p->open_file[i]) {
            file_close(p->open_file[i]);
            p->open_file[i] = NULL;
            p->fd_cloexec[i] = 0;
        }
    }
    if (p->cwd) {
        inode_put(p->cwd);
        p->cwd = NULL;
    }
    if (p->sig_delivering)
        proc_kill_descendants(p);

    spinlock_acquire(&p->lk);
    p->exit_code = exit_code;
    // 将孩子过继给 proczero
    proc_reparent(p);
    // 标记为 ZOMBIE 并尝试唤醒父进程
    p->state = ZOMBIE;
    proc_try_wakeup(p);

    proc_sched();
}

static int proc_signal_group_exit(proc_t *owner, proc_t *me, int exit_code)
{
    int live = 0;

    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        if (p == me)
            continue;

        int should_enqueue = 0;
        spinlock_acquire(&p->lk);
        int same_group = p->thread_group &&
            (p == owner || (p->shared_vm && p->vm_owner == owner));
        if (p->state != UNUSED && p->state != ZOMBIE && same_group) {
            p->group_exit_pending = 1;
            p->group_exit_code = exit_code;
            live++;
            if (p->state == SLEEPING) {
                p->state = RUNNABLE;
                p->sleep_space = NULL;
                p->sched_last_ready_tick = timer_get_ticks();
                p->mlfq_age_start_tick = p->sched_last_ready_tick;
                p->sched_ready_count++;
                p->mlfq_in_readyq = 0;
                should_enqueue = 1;
            } else if (p->state == RUNNABLE) {
                should_enqueue = 1;
            }
        }
        spinlock_release(&p->lk);

        if (should_enqueue)
            mlfq_on_wakeup(p);
    }

    return live;
}

void proc_exit_group_if_requested()
{
    proc_t *p = myproc();
    int pending = 0;
    int exit_code = 0;

    spinlock_acquire(&p->lk);
    if (p->group_exit_pending) {
        pending = 1;
        exit_code = p->group_exit_code;
        p->group_exit_pending = 0;
    }
    spinlock_release(&p->lk);

    if (pending)
        proc_exit(exit_code);
}

void proc_exit_group(int exit_code)
{
    proc_t *me = myproc();
    if (!me->thread_group)
        proc_exit(exit_code);

    proc_t *owner = me->vm_owner ? me->vm_owner : me;
    while (proc_signal_group_exit(owner, me, exit_code) > 0)
        proc_yield();

    proc_exit(exit_code);
}

void proc_signal_action_sync(int signum, uint64 handler, uint64 flags, uint64 restorer)
{
    proc_t *caller = myproc();
    if (caller == NULL || signum < 1 || signum > NSIG)
        return;

    proc_t *owner = caller->vm_owner ? caller->vm_owner : caller;
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        int same_group = p == caller;
        if (caller->thread_group) {
            same_group = p->thread_group &&
                (p == owner || (p->shared_vm && p->vm_owner == owner));
        }
        if (p->state != UNUSED && same_group) {
            p->sig_handler[signum] = handler;
            p->sig_flags[signum] = flags;
            p->sig_restorer = restorer;
        }
        spinlock_release(&p->lk);
    }
}

static int proc_wait_pending_interrupt(proc_t *parent)
{
    uint64 sigchld_bit = 1UL << (SIGCHLD - 1);

    spinlock_acquire(&parent->lk);
    int consumed_sigchld = (parent->sig_pending & sigchld_bit) != 0;
    if (consumed_sigchld)
        parent->sig_pending &= ~sigchld_bit;
    int interrupted = parent->sig_pending != 0;
    spinlock_release(&parent->lk);
    if (interrupted)
        return 1;
    return consumed_sigchld ? -1 : 0;
}

static void proc_wait_clear_sigchld(proc_t *parent)
{
    uint64 sigchld_bit = 1UL << (SIGCHLD - 1);

    spinlock_acquire(&parent->lk);
    parent->sig_pending &= ~sigchld_bit;
    spinlock_release(&parent->lk);
}

/*
    wait4(wait_pid, user_addr, wnohang)
    wait_pid: -1=任意子进程, >0=等特定pid
    wnohang: 1=非阻塞(没有ZOMBIE子进程立即返回0)
*/
int proc_wait4(int64 wait_pid, uint64 user_addr, int wnohang)
{
    proc_t *parent = myproc();

    for (;;) {
        int has_child = 0;
        for (int i = 0; i < N_PROC; i++) {
            proc_t *p = &proc_list[i];
            if (p == parent) continue;
            spinlock_acquire(&p->lk);
            if (p->parent == parent && p->state != UNUSED) {
                // pid过滤：-1=任意，>0=特定pid
                if (wait_pid > 0 && p->pid != (int)wait_pid) {
                    if (parent == proczero && p->state == ZOMBIE) {
                        proc_free(p);
                        spinlock_release(&p->lk);
                        continue;
                    }
                    spinlock_release(&p->lk);
                    continue;
                }
                has_child = 1;
                if (p->state == ZOMBIE) {
                    if (user_addr) {
                        int wstatus;
                        if (p->exit_code < 0)
                            wstatus = (-p->exit_code) & 0x7f;
                        else
                            wstatus = (p->exit_code & 0xff) << 8;
                        uvm_copyout(parent->pgtbl, user_addr, (uint64)&wstatus, sizeof(int));
                    }
                    int pid = p->pid;
                    proc_wait_clear_sigchld(parent);
                    proc_free(p);
                    spinlock_release(&p->lk);
                    return pid;
                }
            }
            spinlock_release(&p->lk);
        }

        if (!has_child)
            return -1;

        if (wnohang)
            return 0;   // WNOHANG: 没有ZOMBIE子进程，立即返回0

        int pending = proc_wait_pending_interrupt(parent);
        if (pending > 0)
            return -EINTR;
        if (pending < 0)
            continue;

        spinlock_acquire(&parent->lk);
        proc_sleep(parent, &parent->lk);
        spinlock_release(&parent->lk);
        pending = proc_wait_pending_interrupt(parent);
        if (pending > 0)
            return -EINTR;
    }
}

/*
    进程等待sleep_space对应的资源, 进入睡眠状态
    RUNNING -> SLEEPING
*/
static int proc_sleep_common(void *sleep_space, spinlock_t *lock, uint64 deadline)
{
    proc_t *p = myproc();
    if (p == NULL || lock == NULL)
        return 0;

    // 应对外设中断处理程序调用proc_sleep的情况
    if (lock != &p->lk) {
        spinlock_acquire(&p->lk);
        sleepchan_inc(sleep_space);
        spinlock_release(lock);
    } else {
        sleepchan_inc(sleep_space);
    }

    // 开始睡眠
    p->sleep_space = sleep_space;
    p->sleep_deadline = deadline;
    p->sleep_timedout = 0;
    p->state = SLEEPING;
    // 防御：进程处于 SLEEPING 时不应在就绪队列中。
    p->mlfq_in_readyq = 0;
    p->sched_sleep_count++;

    // Lab-11: Markov（slice 结束：sleep）
    mkv_on_stop_locked(p, MLFQ_YIELD_NONE, timer_get_ticks());
    // printf("proc %d is sleeping!\n", p->pid);

    // 切到调度器
    proc_sched();

    // 被唤醒
    // printf("proc %d is wakeup!\n", p->pid);
    int timedout = p->sleep_timedout;
    p->sleep_deadline = 0;
    p->sleep_timedout = 0;
    sleepchan_dec(sleep_space);

    // 恢复原样
    if (lock != &p->lk) {
        spinlock_release(&p->lk);
        spinlock_acquire(lock);
    }
    return timedout;
}

void proc_sleep(void *sleep_space, spinlock_t *lock)
{
    (void)proc_sleep_common(sleep_space, lock, 0);
}

int proc_sleep_until(void *sleep_space, spinlock_t *lock, uint64 deadline)
{
    return proc_sleep_common(sleep_space, lock, deadline);
}

/*
    唤醒所有等待sleep_space的进程
    SLEEPING -> RUNNABLE
*/
static void proc_wakeup_scan(void *sleep_space, int use_hint)
{
    if (use_hint && !sleepchan_has_waiter(sleep_space))
        return;

    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state == SLEEPING && p->sleep_space == sleep_space) {
            p->state = RUNNABLE;
            p->sleep_space = NULL;
            p->sched_last_ready_tick = timer_get_ticks();
            p->mlfq_age_start_tick = p->sched_last_ready_tick;
            p->sched_ready_count++;
            // 防御：清理可能残留的 in_readyq 标志，确保一定能重新入队
            p->mlfq_in_readyq = 0;
            spinlock_release(&p->lk);

            // I/O 唤醒: 提升到最高优先级(不持有 p->lk)
            mlfq_on_wakeup(p);
            continue;
        }
        spinlock_release(&p->lk);
    }
}

void proc_wakeup(void *sleep_space)
{
    proc_wakeup_scan(sleep_space, 1);
}

void proc_wakeup_force(void *sleep_space)
{
    proc_wakeup_scan(sleep_space, 0);
}

/* 查找pid对应的进程并返回（返回时持有该进程锁），找不到返回NULL */
proc_t *proc_get_by_pid(int pid)
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->pid == pid) {
            return p; // 返回时保持持锁
        }
        spinlock_release(&p->lk);
    }
    return NULL;
}

/* 
    用户进程切换到调度器
    tips: 调用者保证持有当前进程的锁
*/
void proc_sched()
{
    cpu_t *c = mycpu();
    proc_t *p = myproc();

    // 切回原生进程
    c->proc = NULL; 
    swtch(&p->ctx, &c->ctx);
}

/* 
    调度器
    RUNNABLE->RUNNING
*/
void proc_scheduler()
{
    cpu_t *c = mycpu();
    for (;;) {
        intr_on(); // 开启中断，否则所有进程 sleep 时 CPU 会死锁在关中断状态！
        c->proc = NULL;

        proc_reap_background_zombies();

        // Lab-11: 该 CPU 处于 scheduler 循环中(不在运行进程)
        mlfq_set_cpu_running(mycpuid(), 0);

        proc_t *p = mlfq_pick_next();
        if (p == NULL)
            continue;

        spinlock_acquire(&p->lk);
        if (p->state != RUNNABLE) {
            spinlock_release(&p->lk);
            continue;
        }

        // 切入该进程
        p->state = RUNNING;
        c->proc = p;

        // Lab-11: 标记该 CPU 正在运行进程(用于 wakeup 负载评估)
        mlfq_set_cpu_running(mycpuid(), 1);

        // 调度统计：RUNNABLE -> RUNNING
        uint64 now = timer_get_ticks();
        if (p->sched_first_run_tick == 0)
            p->sched_first_run_tick = now;
        if (p->sched_last_ready_tick != 0) {
            uint64 w = now - p->sched_last_ready_tick;
            p->sched_wait_sum += w;
            if (w > p->sched_wait_max)
                p->sched_wait_max = w;
        }
        p->sched_run_start_tick = now;
        p->sched_run_count++;

        // Lab-11: Per-CPU 归属（便于后续 enqueue/steal 的 locality）
        p->mlfq_cpu = mycpuid();

        // Lab-11: Markov burst 计时起点（使用 sched_cpu_ticks）
        p->mkv_run_start_cpu_ticks = p->sched_cpu_ticks;

        swtch(&c->ctx, &p->ctx);

        // 从进程切回调度器后，仍持有 p->lk
        p->sched_ctx_switches++;
        if (p->state == ZOMBIE && p->shared_vm)
            proc_free(p);
        spinlock_release(&p->lk);
    }
}

uint32 proc_schedstat(uint64 user_dst, uint32 max_entries)
{
    if (max_entries == 0)
        return 0;

    proc_t *caller = myproc();
    if (!caller)
        return 0;

    uint32 copied = 0;
    for (int i = 0; i < N_PROC && copied < max_entries; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state == UNUSED || p->pid == 0) {
            spinlock_release(&p->lk);
            continue;
        }

        sched_stat_t st;
        st.pid = (uint32)p->pid;
        st.state = (uint32)p->state;
        st.mlfq_level = (uint32)p->mlfq_level;
        st._pad = 0;
        st.cpu_ticks = p->sched_cpu_ticks;
        st.wait_sum = p->sched_wait_sum;
        st.wait_max = p->sched_wait_max;
        st.run_count = p->sched_run_count;
        st.ready_count = p->sched_ready_count;
        st.ctx_switches = p->sched_ctx_switches;
        st.preempt_expire = p->sched_preempt_expire;
        st.preempt_higher = p->sched_preempt_higher;
        st.yield_voluntary = p->sched_yield_voluntary;
        st.sleep_count = p->sched_sleep_count;
        st.first_run_tick = p->sched_first_run_tick;

        // Lab-11: Markov / Adaptive quantum
        st.mkv_pred_total = p->mkv_pred_total;
        st.mkv_pred_hit = p->mkv_pred_hit;
        st.mkv_l2_boost_count = p->mkv_l2_boost_count;
        st.mkv_last_pred_state = (uint32)p->mkv_last_pred_state;
        st.mkv_last_act_state = (uint32)p->mkv_last_act_state;
        spinlock_release(&p->lk);

        uvm_copyout(caller->pgtbl,
                    user_dst + (uint64)copied * sizeof(sched_stat_t),
                    (uint64)&st,
                    sizeof(sched_stat_t));
        copied++;
    }
    return copied;
}
