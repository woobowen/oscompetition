#include "mod.h"

// MLFQ 参数
#define MLFQ_LEVELS 3

// aging: 等待超过该阈值则提升一档
#define MLFQ_AGING_THRESHOLD 10

// 每级时间片(单位: 用户态时钟中断tick)
static const int mlfq_quantum[MLFQ_LEVELS] = { 1, 2, 4 };

typedef struct mlfq_runq {
	proc_t *buf[N_PROC];
	int head;
	int tail;
	int size;
} mlfq_runq_t;

static spinlock_t mlfq_lk;
static mlfq_runq_t mlfq_q[MLFQ_LEVELS];

void mlfq_lock(void)
{
	spinlock_acquire(&mlfq_lk);
}

void mlfq_unlock(void)
{
	spinlock_release(&mlfq_lk);
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

static proc_t *runq_pop(mlfq_runq_t *q)
{
	if (q->size == 0)
		return NULL;
	proc_t *p = q->buf[q->head];
	q->head = (q->head + 1) % N_PROC;
	q->size--;
	return p;
}

// 在持有 mlfq_lk 的前提下检查 p 是否真的存在于任一就绪队列中。
static int mlfq_contains_locked(proc_t *p)
{
	for (int level = 0; level < MLFQ_LEVELS; level++) {
		mlfq_runq_t *q = &mlfq_q[level];
		for (int i = 0; i < q->size; i++) {
			int idx = (q->head + i) % N_PROC;
			if (q->buf[idx] == p)
				return 1;
		}
	}
	return 0;
}

// 核心入队逻辑
// 要求：调用者必须同时持有 mlfq_lk 和 p->lk
// 这样才能安全地检查 p->state 和 p->mlfq_in_readyq
static void mlfq_enqueue_locked_plocked(proc_t *p, int level, bool reset_slice)
{
    level = mlfq_clamp_level(level);

    // 队列自校验：防止标志位残留导致丢队列
    if (p->mlfq_in_readyq) {
        if (mlfq_contains_locked(p))
            return;
        p->mlfq_in_readyq = 0;
    }

    // 必须在持有 p->lk 的情况下检查 state，否则可能读到旧值(SLEEPING)导致直接返回
    if (p->state != RUNNABLE)
        return;

    p->mlfq_level = level;
    if (reset_slice || p->mlfq_ticks_left <= 0)
        p->mlfq_ticks_left = mlfq_quantum[level];

    p->mlfq_wait_ticks = 0;
    p->mlfq_in_readyq = 1;
    runq_push(&mlfq_q[level], p);
}


void mlfq_init(void)
{
	spinlock_init(&mlfq_lk, "mlfq");
	for (int i = 0; i < MLFQ_LEVELS; i++)
		runq_init(&mlfq_q[i]);
}

void mlfq_on_new(proc_t *p)
{
	spinlock_acquire(&mlfq_lk);
    spinlock_acquire(&p->lk); // 加锁 p->lk
    mlfq_enqueue_locked_plocked(p, 0, true);
    spinlock_release(&p->lk);
    spinlock_release(&mlfq_lk);
}

void mlfq_on_wakeup(proc_t *p)
{
	// I/O 型任务被唤醒: 提升到最高优先级
	spinlock_acquire(&mlfq_lk);
    spinlock_acquire(&p->lk); // 加锁 p->lk，防止 state 竞态
    mlfq_enqueue_locked_plocked(p, 0, true);
    spinlock_release(&p->lk);
    spinlock_release(&mlfq_lk);
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

	spinlock_acquire(&mlfq_lk);
    spinlock_acquire(&p->lk); // 加锁 p->lk
    mlfq_enqueue_locked_plocked(p, level, reset_slice);
    spinlock_release(&p->lk);
    spinlock_release(&mlfq_lk);
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

	// 调用者已持有 p->lk，直接调用内部实现
    mlfq_enqueue_locked_plocked(p, level, reset_slice);
}

proc_t *mlfq_pick_next(void)
{
	spinlock_acquire(&mlfq_lk);
	for (int level = 0; level < MLFQ_LEVELS; level++) {
		while (mlfq_q[level].size > 0) {
			proc_t *p = runq_pop(&mlfq_q[level]);
			if (p == NULL)
				break;

			// 出队后先校验状态：只调度 RUNNABLE。
			// 这里遵循锁顺序: mlfq_lk -> p->lk。
			spinlock_acquire(&p->lk);
			if (p->state != RUNNABLE) {
				p->mlfq_in_readyq = 0;
				spinlock_release(&p->lk);
				continue;
			}

			p->mlfq_in_readyq = 0;

			// 将要运行: 清空等待累计；若时间片无效则补齐
			p->mlfq_wait_ticks = 0;
			if (p->mlfq_ticks_left <= 0)
				p->mlfq_ticks_left = mlfq_quantum[mlfq_clamp_level(p->mlfq_level)];
			spinlock_release(&p->lk);

			spinlock_release(&mlfq_lk);
			return p;
		}
	}
	spinlock_release(&mlfq_lk);
	return NULL;
}

bool mlfq_has_higher(int level)
{
	level = mlfq_clamp_level(level);

	bool found = false;
	spinlock_acquire(&mlfq_lk);
	for (int i = 0; i < level; i++) {
		if (mlfq_q[i].size > 0) {
			found = true;
			break;
		}
	}
	spinlock_release(&mlfq_lk);
	return found;
}

// aging: 对“队列中等待”的 RUNNABLE 进程累计 wait_ticks
// 达到阈值则提升一档(例如 L2 -> L1)
void mlfq_age_tick(void)
{
	spinlock_acquire(&mlfq_lk);

	// 仅对非最高优先级队列做提升
	for (int level = 1; level < MLFQ_LEVELS; level++) {
		int n = mlfq_q[level].size;
		for (int i = 0; i < n; i++) {
			proc_t *p = runq_pop(&mlfq_q[level]);
			if (p == NULL)
				continue;

			// 仍在就绪队列中的进程应当是 RUNNABLE；这里拿 p->lk 做防御校验。
			spinlock_acquire(&p->lk);
			if (p->state != RUNNABLE) {
				p->mlfq_in_readyq = 0;
				spinlock_release(&p->lk);
				continue;
			}

			p->mlfq_wait_ticks++;

			if (p->mlfq_wait_ticks >= MLFQ_AGING_THRESHOLD) {
				p->mlfq_wait_ticks = 0;
				int new_level = level - 1;
				p->mlfq_level = new_level;
				p->mlfq_ticks_left = mlfq_quantum[new_level];
				spinlock_release(&p->lk);
				runq_push(&mlfq_q[new_level], p);
			} else {
				spinlock_release(&p->lk);
				runq_push(&mlfq_q[level], p);
			}
		}
	}

	spinlock_release(&mlfq_lk);
}
