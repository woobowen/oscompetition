#include "mod.h"

// MLFQ 参数
#define MLFQ_LEVELS 3

// aging: 等待超过该阈值则提升一档
#define MLFQ_AGING_THRESHOLD 10

// lazy aging: 每次 tick/挑选时最多扫描的元素数(每级、每CPU)
#define MLFQ_AGING_BUDGET 4

// 每级时间片(单位: 用户态时钟中断tick)
static const int mlfq_quantum[MLFQ_LEVELS] = { 1, 2, 4 };

// trap/timer.c: 已存在，用于读取全局 tick
extern uint64 timer_get_ticks();

typedef struct mlfq_runq {
	proc_t *buf[N_PROC];
	int head;
	int tail;
	int size;
} mlfq_runq_t;

typedef struct mlfq_cpu_rq {
	spinlock_t lk;
	mlfq_runq_t q[MLFQ_LEVELS];
} mlfq_cpu_rq_t;

static mlfq_cpu_rq_t mlfq_rq[NCPU];

// Lab-11: wakeup 选核时的轻量运行状态提示。
// 1 表示该 CPU 正在运行某个进程(不在 scheduler 循环里)
static uint8 mlfq_cpu_running[NCPU];

// ---- wakeup 选核可调参数 ----
// wakeup 迁移抖动抑制：最佳 CPU 与本 CPU 负载差距不大则留本核
#define MLFQ_WAKEUP_HYSTERESIS 0

// wakeup/newproc 选核时的负载权重：load = w0*L0 + w1*L1 + w2*L2 + wr*running
#define MLFQ_WAKEUP_W_L0      2
#define MLFQ_WAKEUP_W_L1      1
#define MLFQ_WAKEUP_W_L2      1
#define MLFQ_WAKEUP_W_RUNNING 2

// 新建进程入队时的“打散”轮转起点，用于负载相同时的 tie-break
static uint32 mlfq_new_rr;

void mlfq_lock(void)
{
	int cpu = mycpuid();
	spinlock_acquire(&mlfq_rq[cpu].lk);
}

void mlfq_unlock(void)
{
	int cpu = mycpuid();
	spinlock_release(&mlfq_rq[cpu].lk);
}

static int mlfq_clamp_level(int level)
{
	if (level < 0) return 0;
	if (level >= MLFQ_LEVELS) return MLFQ_LEVELS - 1;
	return level;
}

static void runq_init(mlfq_runq_t *q)
{
	q->head = 0;
	q->tail = 0;
	q->size = 0;
}

static void runq_push(mlfq_runq_t *q, proc_t *p)
{
	assert(q->size < N_PROC, "mlfq: runq overflow");
	q->buf[q->tail] = p;
	q->tail = (q->tail + 1) % N_PROC;
	q->size++;
}

static void runq_push_head(mlfq_runq_t *q, proc_t *p)
{
	assert(q->size < N_PROC, "mlfq: runq overflow");
	q->head = (q->head + N_PROC - 1) % N_PROC;
	q->buf[q->head] = p;
	q->size++;
}

static proc_t *runq_pop(mlfq_runq_t *q)
{
	if (q->size == 0)
		return NULL;
	proc_t *p = q->buf[q->head];
	q->head = (q->head + 1) % N_PROC;
	q->size--;
	return p;
}

static proc_t *runq_pop_tail(mlfq_runq_t *q)
{
	if (q->size == 0)
		return NULL;
	q->tail = (q->tail + N_PROC - 1) % N_PROC;
	proc_t *p = q->buf[q->tail];
	q->size--;
	return p;
}

// 在持有任一 CPU 的 mlfq_rq[cpu].lk 的前提下检查 p 是否真的存在于该 CPU 的任一就绪队列中
static int mlfq_contains_locked_cpu(int cpu, proc_t *p)
{
	for (int level = 0; level < MLFQ_LEVELS; level++) {
		mlfq_runq_t *q = &mlfq_rq[cpu].q[level];
		for (int i = 0; i < q->size; i++) {
			int idx = (q->head + i) % N_PROC;
			if (q->buf[idx] == p)
				return 1;
		}
	}
	return 0;
}

// ---- Markov 预测，用于自适应 quantum ----

// state = burst(S/M/L)*3 + reason(sleep/expire/higher)
#define MLFQ_MKV_BURST_S 0
#define MLFQ_MKV_BURST_M 1
#define MLFQ_MKV_BURST_L 2

#define MLFQ_MKV_R_SLEEP 0
#define MLFQ_MKV_R_EXPIRE 1
#define MLFQ_MKV_R_HIGHER 2

#define MLFQ_MKV_STATES 9

static int mlfq_markov_predict_state_plocked(proc_t *p)
{
	if (!p->mkv_has_prev)
		return -1;
	int prev = (int)p->mkv_prev_state;
	if (prev < 0 || prev >= MLFQ_MKV_STATES)
		return -1;

	// 冷启动：如果该 prev 状态从未观察到任何出边转移，直接预测“保持不变”
	uint32 sum = 0;
	for (int s = 0; s < MLFQ_MKV_STATES; s++)
		sum += p->mkv_trans[prev][s];
	if (sum == 0)
		return prev;

	uint32 best_v = 0;
	int best_s = 0;
	for (int s = 0; s < MLFQ_MKV_STATES; s++) {
		uint32 v = p->mkv_trans[prev][s] + 1; // Laplace smoothing
		if (v > best_v || (v == best_v && s < best_s)) {
			best_v = v;
			best_s = s;
		}
	}
	return best_s;
}

static int mlfq_cpu_load_locked(int cpu)
{
	// caller holds mlfq_rq[cpu].lk
	// I/O 友好：对 L0(最高优先级)队列加权更大，使 wakeup 更倾向于把任务放到 L0 更空的 CPU。
	int load = 0;
	load += MLFQ_WAKEUP_W_L0 * mlfq_rq[cpu].q[0].size;
	load += MLFQ_WAKEUP_W_L1 * mlfq_rq[cpu].q[1].size;
	load += MLFQ_WAKEUP_W_L2 * mlfq_rq[cpu].q[2].size;
	load += (mlfq_cpu_running[cpu] ? MLFQ_WAKEUP_W_RUNNING : 0);
	return load;
}

// 新建进程入队：优先放到最空的 CPU。
// 与 wakeup 不同，这里不做本核偏好；否则 fork 风暴会把所有子进程压在父进程所在核，
static int mlfq_choose_cpu_for_newproc(void)
{
	int start = (int)(__sync_fetch_and_add(&mlfq_new_rr, 1) % NCPU);
	int best_cpu = start;
	int best_load = 1 << 30;

	for (int i = 0; i < NCPU; i++) {
		int c = (start + i) % NCPU;
		spinlock_acquire(&mlfq_rq[c].lk);
		int load = mlfq_cpu_load_locked(c);
		spinlock_release(&mlfq_rq[c].lk);

		// 避免选到没有运行调度器的 CPU（例如只启动了 1 个 hart 时），
		// 把未运行的 CPU 视为高负载，优先选择已有运行调度器的 CPU。
		if (!mlfq_cpu_running[c] && c != mycpuid())
			load += 0x100000; // large penalty

		if (load < best_load) {
			best_load = load;
			best_cpu = c;
		}
	}
	return best_cpu;
}

static int mlfq_choose_cpu_for_wakeup(int local_cpu)
{
	if (local_cpu < 0 || local_cpu >= NCPU)
		local_cpu = 0;

	int best_cpu = local_cpu;
	int local_load = 0;
	int best_load = 1 << 30;

	for (int c = 0; c < NCPU; c++) {
		spinlock_acquire(&mlfq_rq[c].lk);
		int load = mlfq_cpu_load_locked(c);
		spinlock_release(&mlfq_rq[c].lk);

		// 避免把唤醒进程迁到未运行调度器的 CPU(如 -smp 1 时只有 cpu0 在跑):
		// 否则进程入队到死核的 L0, 而 pick_next 的工作窃取从不偷 L0 -> 永远跑不到。
		if (!mlfq_cpu_running[c] && c != mycpuid())
			load += 0x100000; // 与 mlfq_choose_cpu_for_newproc 同样的死核惩罚

		if (c == local_cpu)
			local_load = load;
		if (load < best_load) {
			best_load = load;
			best_cpu = c;
		}
	}

	// 抑制抖动：差距不明显就留在本核
	if (best_cpu != local_cpu) {
		if (best_load + MLFQ_WAKEUP_HYSTERESIS >= local_load)
			best_cpu = local_cpu;
	}
	return best_cpu;
}

static int mlfq_quantum_plocked(proc_t *p, int level)
{
	level = mlfq_clamp_level(level);
	int base = mlfq_quantum[level];

	// 记录预测
	int pred = mlfq_markov_predict_state_plocked(p);
	if (pred >= 0) {
		p->mkv_last_pred_state = (uint8)pred;
		p->mkv_pred_valid = 1;
		p->mkv_pred_total++;
	}

	// 仅对 L2 做保守的自适应放大：
	// - 预测为“长 CPU burst + expire”时：时间片 * 2
	// - 预测为“中等 CPU burst + expire”时：给一个小幅提升(例如 6)
	if (level == MLFQ_LEVELS - 1) {
		if (pred >= 0) {
			int burst = pred / 3;
			int reason = pred % 3;
			if (reason == MLFQ_MKV_R_EXPIRE) {
				if (burst == MLFQ_MKV_BURST_L) {
					int q = base * 2;
					if (q < base)
						q = base;
					p->mkv_l2_boost_count++;
					return q;
				}
				if (burst == MLFQ_MKV_BURST_M) {
					int q = base + 2;
					if (q < 6)
						q = 6;
					p->mkv_l2_boost_count++;
					return q;
				}
			}
		}
	}

	return base;
}

// 入队逻辑
// 要求：调用者必须同时持有 mlfq_rq[cpu].lk 和 p->lk
// 这样才能安全地检查 p->state 和 p->mlfq_in_readyq
static void mlfq_enqueue_locked_plocked(int cpu, proc_t *p, int level, bool reset_slice, bool push_front)
{
    level = mlfq_clamp_level(level);

    // 队列自校验：防止标志位残留导致丢队列
    if (p->mlfq_in_readyq) {
		if (mlfq_contains_locked_cpu(cpu, p))
            return;
        p->mlfq_in_readyq = 0;
    }

    // 必须在持有 p->lk 的情况下检查 state，否则可能读到旧值(SLEEPING)导致直接返回
    if (p->state != RUNNABLE)
        return;

    p->mlfq_level = level;
	if (reset_slice || p->mlfq_ticks_left <= 0)
		p->mlfq_ticks_left = mlfq_quantum_plocked(p, level);

    p->mlfq_wait_ticks = 0;
	p->mlfq_age_start_tick = timer_get_ticks();
    p->mlfq_in_readyq = 1;
	p->mlfq_cpu = cpu;
	if (push_front)
		runq_push_head(&mlfq_rq[cpu].q[level], p);
	else
		runq_push(&mlfq_rq[cpu].q[level], p);
}


void mlfq_init(void)
{
	for (int c = 0; c < NCPU; c++) {
		spinlock_init(&mlfq_rq[c].lk, "mlfq");
		mlfq_cpu_running[c] = 0;
		for (int i = 0; i < MLFQ_LEVELS; i++)
			runq_init(&mlfq_rq[c].q[i]);
	}
}

void mlfq_set_cpu_running(int cpu, int running)
{
	if (cpu < 0 || cpu >= NCPU)
		return;
	spinlock_acquire(&mlfq_rq[cpu].lk);
	mlfq_cpu_running[cpu] = running ? 1 : 0;
	spinlock_release(&mlfq_rq[cpu].lk);
}

void mlfq_on_new(proc_t *p)
{
	int cpu;
	if (p->pid == 1)
		cpu = mycpuid();
	else
		cpu = mlfq_choose_cpu_for_newproc();
	spinlock_acquire(&mlfq_rq[cpu].lk);
	spinlock_acquire(&p->lk);
	mlfq_enqueue_locked_plocked(cpu, p, 0, true, false);
	spinlock_release(&p->lk);
	spinlock_release(&mlfq_rq[cpu].lk);
}

void mlfq_on_wakeup(proc_t *p)
{
	// I/O 型任务被唤醒: 提升到最高优先级。
	// 选核指标：L0_len + L1_len + L2_len + running(0/1)，并用小阈值抑制抖动。
	int local_cpu = p->mlfq_cpu;
	if (local_cpu < 0 || local_cpu >= NCPU)
		local_cpu = mycpuid();
	int cpu = mlfq_choose_cpu_for_wakeup(local_cpu);
	spinlock_acquire(&mlfq_rq[cpu].lk);
	spinlock_acquire(&p->lk);
	// wakeup 提升到 L0 且插队到队首：降低唤醒后的 ready wait
	mlfq_enqueue_locked_plocked(cpu, p, 0, true, true);
	spinlock_release(&p->lk);
	spinlock_release(&mlfq_rq[cpu].lk);
}

void mlfq_on_yield(proc_t *p, int reason)
{
	int level = mlfq_clamp_level(p->mlfq_level);
	bool reset_slice = false;

	switch (reason) {
	case MLFQ_YIELD_EXPIRE:
		if (level < MLFQ_LEVELS - 1)
			level++;
		reset_slice = true;
		break;
	case MLFQ_YIELD_HIGHER:
		// 被更高优先级抢占: 保持层级与剩余时间片
		reset_slice = false;
		break;
	case MLFQ_YIELD_VOLUNTARY:
		// 主动让出: 保持层级
		reset_slice = false;
		break;
	default:
		reset_slice = false;
		break;
	}

	int cpu = mycpuid();
	spinlock_acquire(&mlfq_rq[cpu].lk);
	spinlock_acquire(&p->lk);
	mlfq_enqueue_locked_plocked(cpu, p, level, reset_slice, false);
	spinlock_release(&p->lk);
	spinlock_release(&mlfq_rq[cpu].lk);
}

// 与 mlfq_on_yield 相同，但不获取/释放 mlfq_lk
// 用于需要保证锁顺序(mlfq -> p->lk)的路径
void mlfq_on_yield_locked(proc_t *p, int reason)
{
	int level = mlfq_clamp_level(p->mlfq_level);
	bool reset_slice = false;

	switch (reason) {
	case MLFQ_YIELD_EXPIRE:
		if (level < MLFQ_LEVELS - 1)
			level++;
		reset_slice = true;
		break;
	case MLFQ_YIELD_HIGHER:
		reset_slice = false;
		break;
	case MLFQ_YIELD_VOLUNTARY:
		reset_slice = false;
		break;
	default:
		reset_slice = false;
		break;
	}

	// 调用者已持有本 CPU 的 mlfq_lock 与 p->lk，直接调用内部实现
	int cpu = mycpuid();
	mlfq_enqueue_locked_plocked(cpu, p, level, reset_slice, false);
}

proc_t *mlfq_pick_next(void)
{
	int cpu = mycpuid();
	spinlock_acquire(&mlfq_rq[cpu].lk);
	for (int level = 0; level < MLFQ_LEVELS; level++) {
		while (mlfq_rq[cpu].q[level].size > 0) {
			proc_t *p = runq_pop(&mlfq_rq[cpu].q[level]);
			if (p == NULL)
				break;

			spinlock_acquire(&p->lk);
			if (p->state != RUNNABLE) {
				p->mlfq_in_readyq = 0;
				spinlock_release(&p->lk);
				continue;
			}

			p->mlfq_in_readyq = 0;
			p->mlfq_wait_ticks = 0;
			if (p->mlfq_ticks_left <= 0)
				p->mlfq_ticks_left = mlfq_quantum_plocked(p, mlfq_clamp_level(p->mlfq_level));
			p->mlfq_cpu = cpu;
			spinlock_release(&p->lk);

			spinlock_release(&mlfq_rq[cpu].lk);
			return p;
		}
	}
	spinlock_release(&mlfq_rq[cpu].lk);

	// 本地空：尝试从其他 CPU 偷 L2 (最低优先级) 队列
	for (int victim = 0; victim < NCPU; victim++) {
		if (victim == cpu)
			continue;
		spinlock_acquire(&mlfq_rq[victim].lk);
		if (mlfq_rq[victim].q[MLFQ_LEVELS - 1].size > 0) {
			proc_t *p = runq_pop_tail(&mlfq_rq[victim].q[MLFQ_LEVELS - 1]);
			if (p) {
				spinlock_acquire(&p->lk);
				if (p->state != RUNNABLE) {
					p->mlfq_in_readyq = 0;
					spinlock_release(&p->lk);
					spinlock_release(&mlfq_rq[victim].lk);
					continue;
				}
				p->mlfq_in_readyq = 0;
				p->mlfq_wait_ticks = 0;
				if (p->mlfq_ticks_left <= 0)
					p->mlfq_ticks_left = mlfq_quantum_plocked(p, mlfq_clamp_level(p->mlfq_level));
				p->mlfq_cpu = cpu;
				spinlock_release(&p->lk);

				spinlock_release(&mlfq_rq[victim].lk);
				return p;
			}
		}
		spinlock_release(&mlfq_rq[victim].lk);
	}

	// 进一步的保守偷取：仅当本地完全无活且偷不到 L2 时，允许从别核偷 L1。
	// 为了不伤害 I/O 响应性：若 victim 的 L0 非空，则不从该 victim 偷 L1。
	for (int victim = 0; victim < NCPU; victim++) {
		if (victim == cpu)
			continue;
		spinlock_acquire(&mlfq_rq[victim].lk);
		if (mlfq_rq[victim].q[0].size > 0) {
			spinlock_release(&mlfq_rq[victim].lk);
			continue;
		}
		if (mlfq_rq[victim].q[1].size > 1) {
			proc_t *p = runq_pop_tail(&mlfq_rq[victim].q[1]);
			if (p) {
				spinlock_acquire(&p->lk);
				if (p->state != RUNNABLE) {
					p->mlfq_in_readyq = 0;
					spinlock_release(&p->lk);
					spinlock_release(&mlfq_rq[victim].lk);
					continue;
				}
				p->mlfq_in_readyq = 0;
				p->mlfq_wait_ticks = 0;
				if (p->mlfq_ticks_left <= 0)
					p->mlfq_ticks_left = mlfq_quantum_plocked(p, mlfq_clamp_level(p->mlfq_level));
				p->mlfq_cpu = cpu;
				spinlock_release(&p->lk);

				spinlock_release(&mlfq_rq[victim].lk);
				return p;
			}
		}
		spinlock_release(&mlfq_rq[victim].lk);
	}

	return NULL;
}

bool mlfq_has_higher(int level)
{
	level = mlfq_clamp_level(level);

	bool found = false;
	int cpu = mycpuid();
	spinlock_acquire(&mlfq_rq[cpu].lk);
	for (int i = 0; i < level; i++) {
		if (mlfq_rq[cpu].q[i].size > 0) {
			found = true;
			break;
		}
	}
	spinlock_release(&mlfq_rq[cpu].lk);
	return found;
}

// aging: 对“队列中等待”的 RUNNABLE 进程累计 wait_ticks
// 达到阈值则提升一档(例如 L2 -> L1)
void mlfq_age_tick(void)
{
	int cpu = mycpuid();
	uint64 now = timer_get_ticks();
	spinlock_acquire(&mlfq_rq[cpu].lk);

	// Lazy Aging：每次只做预算扫描，避免 O(N) 全量遍历
	for (int level = 1; level < MLFQ_LEVELS; level++) {
		int budget = mlfq_rq[cpu].q[level].size;
		if (budget > MLFQ_AGING_BUDGET)
			budget = MLFQ_AGING_BUDGET;

		for (int i = 0; i < budget; i++) {
			proc_t *p = runq_pop(&mlfq_rq[cpu].q[level]);
			if (p == NULL)
				continue;

			spinlock_acquire(&p->lk);
			if (p->state != RUNNABLE) {
				p->mlfq_in_readyq = 0;
				spinlock_release(&p->lk);
				continue;
			}

			bool aged = false;
			if (p->mlfq_age_start_tick != 0 && now >= p->mlfq_age_start_tick) {
				uint64 waited = now - p->mlfq_age_start_tick;
				if (waited >= (uint64)MLFQ_AGING_THRESHOLD)
					aged = true;
			}

			if (aged) {
				int new_level = level - 1;
				p->mlfq_level = new_level;
				p->mlfq_ticks_left = mlfq_quantum_plocked(p, new_level);
				p->mlfq_age_start_tick = now;
				spinlock_release(&p->lk);
				runq_push(&mlfq_rq[cpu].q[new_level], p);
			} else {
				spinlock_release(&p->lk);
				runq_push(&mlfq_rq[cpu].q[level], p);
			}
		}
	}

	spinlock_release(&mlfq_rq[cpu].lk);
}
