#include "help.h"

#define MAX_STAT 64

static sched_stat_t st_buf[MAX_STAT];

static sched_stat_t *find_stat(uint32 pid, uint32 n)
{
    for (uint32 i = 0; i < n; i++) {
        if (st_buf[i].pid == pid)
            return &st_buf[i];
    }
    return 0;
}

static void cpu_hog(void)
{
    volatile uint64 x = 0;
    for (uint64 r = 0; r < 2000; r++) {
        for (uint64 i = 0; i < 20000; i++)
            x += i;
    }
    (void)x;
    sys_exit(0);
}

static void bursty_high_prio(void)
{
    // 反复 sleep(1) -> wakeup boost 到高优先级
    // 每次醒来只做很小的计算，然后立刻再次 sleep
    volatile uint64 x = 0;
    for (int k = 0; k < 400; k++) {
        sys_sleep(1);
        for (uint64 i = 0; i < 200; i++)
            x += i;
    }
    (void)x;
    sys_exit(0);
}

// 目标：验证在大量高优先级“唤醒”任务存在时，低队列 CPU hog 仍能获得运行机会（aging 防饥饿）
void main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    fprintf(STDOUT, "test_mlfq_aging: start\n");

    uint32 hog_pid = sys_fork();
    if (hog_pid == 0)
        cpu_hog();

    // 创建一批“高优先级压力”任务
    const int N_BURST = 10;
    for (int i = 0; i < N_BURST; i++) {
        uint32 pid = sys_fork();
        if (pid == 0)
            bursty_high_prio();
    }

    // 给调度器足够时间
    sys_sleep(200);

    uint32 n = sys_schedstat(st_buf, MAX_STAT);
    sched_stat_t *hog = find_stat(hog_pid, n);

    if (!hog) {
        fprintf(STDOUT, "FAIL: cannot find hog pid=%d\n", (int)hog_pid);
        sys_exit(1);
    }

    // 判据：hog 至少被调度过（run_count>0），且至少占用过一些 tick（cpu_ticks>0）
    // 如果没有 aging，hog 可能长期得不到 CPU。
    if (hog->run_count == 0 || hog->cpu_ticks == 0) {
        fprintf(STDOUT, "FAIL: hog starved? pid=%d run=%d cpu=%d wait_max=%d lvl=%d\n",
                (int)hog->pid,
                (int)hog->run_count,
                (int)hog->cpu_ticks,
                (int)hog->wait_max,
                (int)hog->mlfq_level);
        sys_exit(1);
    }

    fprintf(STDOUT, "PASS: hog got CPU pid=%d run=%d cpu=%d wait_max=%d lvl=%d\n",
            (int)hog->pid,
            (int)hog->run_count,
            (int)hog->cpu_ticks,
            (int)hog->wait_max,
            (int)hog->mlfq_level);

    sys_exit(0);
}
