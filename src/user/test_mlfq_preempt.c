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

static void cpu_hog_forever(void)
{
    volatile uint64 x = 0;
    for (;;) {
        for (uint64 i = 0; i < 50000; i++)
            x += i;
    }
}

static void waker(void)
{
    volatile uint64 x = 0;
    for (int k = 0; k < 200; k++) {
        sys_sleep(1); // wakeup -> 进入高优先级
        for (uint64 i = 0; i < 2000; i++)
            x += i;
    }
    (void)x;
    sys_exit(0);
}

// 目标：验证“高优先级就绪”会抢占正在运行的低优先级 CPU hog
void main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    fprintf(STDOUT, "test_mlfq_preempt: start\n");

    uint32 hog_pid = sys_fork();
    if (hog_pid == 0)
        cpu_hog_forever();

    // 让 hog 先跑一会
    sys_sleep(5);

    // 创建一个反复 wake 的任务，理论上会经常抢占 hog
    uint32 wpid = sys_fork();
    if (wpid == 0)
        waker();

    // 等待一段时间让抢占发生
    sys_sleep(200);

    uint32 n = sys_schedstat(st_buf, MAX_STAT);
    sched_stat_t *hog = find_stat(hog_pid, n);
    if (!hog) {
        fprintf(STDOUT, "FAIL: cannot find hog pid=%d\n", (int)hog_pid);
        sys_exit(1);
    }

    if (hog->preempt_higher == 0) {
        fprintf(STDOUT, "FAIL: no higher-prio preempt observed. pid=%d preHigh=%d preExp=%d run=%d lvl=%d\n",
                (int)hog->pid,
                (int)hog->preempt_higher,
                (int)hog->preempt_expire,
                (int)hog->run_count,
                (int)hog->mlfq_level);
        sys_exit(1);
    }

    fprintf(STDOUT, "PASS: observed higher-prio preempt. pid=%d preHigh=%d preExp=%d run=%d lvl=%d\n",
            (int)hog->pid,
            (int)hog->preempt_higher,
            (int)hog->preempt_expire,
            (int)hog->run_count,
            (int)hog->mlfq_level);

    sys_exit(0);
}
