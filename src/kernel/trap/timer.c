#include "mod.h"

/*-------------------- SBI timer interface --------------------*/

static void sbi_set_timer(uint64 stime_value)
{
    register uint64 a0 asm("a0") = stime_value;
    register uint64 a6 asm("a6") = 0;
    register uint64 a7 asm("a7") = 0x54494D45; // EID "TIME"
    asm volatile("ecall" : "+r"(a0) : "r"(a6), "r"(a7) : "memory");
}

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间
static uint64 mscratch[NCPU][5];

// 时钟初始化
void timer_init()
{
    // 获取当前cpuid
    int hartid = r_tp();

    // 设置初始值 cmp_time = cur_time + time_interval
    *(uint64*)CLINT_MTIMECMP(hartid) = *(uint64*)CLINT_MTIME + INTERVAL;

    // cur_mscratch 指向当前CPU的msrcatch数组
    uint64* cur_mscratch = mscratch[hartid];

    // cur_mscratch[1] [2] [3]先空着, 在trap.S里使用
    cur_mscratch[3] = CLINT_MTIMECMP(hartid); // cmp_time
    cur_mscratch[4] = INTERVAL;               // interval

    // 存放到临时寄存器, 便于与trap.S中的timer_vector协作
    w_mscratch((uint64)cur_mscratch);

    // 设置 M-mode 中断处理函数
    w_mtvec((uint64)timer_vector);

    // 打开 M-mode 中断总开关
    w_mstatus(r_mstatus() | MSTATUS_MIE);

    // 打开 M-mode 时钟中断分开关
    w_mie(r_mie() | MIE_MTIE);
}

// SBI 模式下的定时器初始化（OpenSBI 启动路径使用）
void timer_init_sbi()
{
    uint64 now = r_time();
    sbi_set_timer(now + INTERVAL);
}

/*--------------------- 工作在S-mode --------------------*/

// 全局系统时钟
static timer_t sys_timer;

// 时钟创建
void timer_create()
{
    // 初始化sys_timer
    sys_timer.ticks = 0;
    spinlock_init(&sys_timer.lk, "sys_timer");
}

// 时钟更新
void timer_update()
{
    // ticks++ (保证原子性)
    spinlock_acquire(&sys_timer.lk);
    sys_timer.ticks++;
    spinlock_release(&sys_timer.lk);

    // 唤醒所有在 sys_timer 上睡眠的进程
    proc_wakeup((void *)&sys_timer);
}

// 获取滴答数量 (不把sys_timer暴露出去, 只提供安全的访问接口)
uint64 timer_get_ticks()
{
    // 返回ticks
    uint64 ticks;
    spinlock_acquire(&sys_timer.lk);
    ticks = sys_timer.ticks;
    spinlock_release(&sys_timer.lk);
    return ticks;
}

// 让进程睡眠ntick个时钟周期
void timer_wait(uint64 ntick)
{
    spinlock_acquire(&sys_timer.lk);
    // 记录开始时的ticks
    uint64 start_ticks = sys_timer.ticks;
    // 循环等待，直到达到指定的ticks数
    while (sys_timer.ticks - start_ticks < ntick) {
        // 在sys_timer上睡眠，释放sys_timer锁
        // 当timer_update唤醒时会重新获取锁
        proc_sleep((void *)&sys_timer, &sys_timer.lk);
    }
    spinlock_release(&sys_timer.lk);
}