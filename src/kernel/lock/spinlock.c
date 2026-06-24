#include "mod.h"

/*
    开关中断的基本逻辑:
    1. 多个地方可能开关中断, 因此它不是“开/关”的二元状态，而是“关 关 关 开 开 开”的stack
    2. 在第一次执行关中断时, 记录中断的初始状态为X
    3. 每次关中断, stack中的元素加1
    4. 每次开中断, stack中的元素减1
    5. 如果stack中元素清空, 将中断状态设为初始的X
*/

// 带层数叠加的关中断
void push_off(void)
{
    int old = intr_get();
    intr_off();
    cpu_t *cpu = mycpu();
    if (cpu->noff == 0)
        cpu->origin = old;
    cpu->noff++;
}

// 带层数叠加的开中断
void pop_off(void)
{
    cpu_t *cpu = mycpu();
    assert(intr_get() == 0, "push_off: 1\n"); // 确保此时中断是关闭的
    assert(cpu->noff >= 1, "push_off: 2\n");  // 确保push和pop的对应
    cpu->noff--;
    if (cpu->noff == 0 && cpu->origin == 1) // 只有所有push操作都被抵消且原来状态是开着时
        intr_on();
}


// 自旋锁初始化
void spinlock_init(spinlock_t *lk, char *name)
{
    // 初始状态：未上锁（无cpu持有该锁）
    lk->locked = 0;
    lk->name = name;
    lk->cpuid = -1;
}

// 是否持有自旋锁
bool spinlock_holding(spinlock_t *lk)
{
    // 检查当前的cpu是否持有该锁
    return lk->locked && lk->cpuid == mycpuid();
}

// 获取自旋锁
// 原地循环，直到能够获取锁
void spinlock_acquire(spinlock_t *lk)
{
    // 关中断
    push_off();

    // 确保当前cpu没有已经持有该锁
    assert(!spinlock_holding(lk),"spinlock_acquire: lock is already held by this cpu\n");

    // 自旋等待，直到成功获取锁
    while (__sync_lock_test_and_set(&lk->locked,1)!=0) // 原子操作：尝试获取锁
        ;

    // 防止指令的乱序执行导致锁机制失效
    __sync_synchronize();
    // 记录该锁的cpu信息

    lk->cpuid = mycpuid();
}

// 释放自旋锁
void spinlock_release(spinlock_t *lk)
{
    // 确保当前cpu持有锁
    assert(spinlock_holding(lk),"spinlock_release: lock is not held by this cpu\n");

    // 清除该锁的cpu信息
    lk->cpuid = -1;

    __sync_synchronize();

    // 释放锁
    __sync_lock_release(&lk->locked); // 原子操作

    // 开中断
    pop_off();
}
