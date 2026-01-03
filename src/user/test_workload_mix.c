#include "help.h"

#define MAX_STAT 64
static sched_stat_t st_buf[MAX_STAT];

static const char *state_str(uint32 state)
{
    switch (state) {
    case 0: return "UNUSED";
    case 1: return "RUNNABLE";
    case 2: return "RUNNING";
    case 3: return "SLEEPING";
    case 4: return "ZOMBIE";
    default: return "UNKNOWN";
    }
}

static void dump_stats(const char *tag)
{
    uint32 n = sys_schedstat(st_buf, MAX_STAT);
    fprintf(STDOUT, "[%s] entries=%d\n", tag, (int)n);
    fprintf(STDOUT, " pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first\n");
    for (uint32 i = 0; i < n; i++) {
        fprintf(STDOUT,
            "%d %s %d %d %d %d %d %d %d %d %d %d %d %d\n",
            (int)st_buf[i].pid,
            state_str(st_buf[i].state),
            (int)st_buf[i].mlfq_level,
            (int)st_buf[i].cpu_ticks,
            (int)st_buf[i].wait_sum,
            (int)st_buf[i].wait_max,
            (int)st_buf[i].run_count,
            (int)st_buf[i].ready_count,
            (int)st_buf[i].ctx_switches,
            (int)st_buf[i].preempt_expire,
            (int)st_buf[i].preempt_higher,
            (int)st_buf[i].yield_voluntary,
            (int)st_buf[i].sleep_count,
            (int)st_buf[i].first_run_tick);
    }
}

static void short_cpu(void)
{
    volatile uint64 x = 0;
    for (int r = 0; r < 180; r++) {
        for (uint64 i = 0; i < 90000; i++) x += i;
    }
    (void)x;
    sys_exit(0);
}

static void short_io(void)
{
    volatile uint64 x = 0;
    for (int i = 0; i < 60; i++) {
        for (uint64 k = 0; k < 3000; k++) x ^= (k + i);
        sys_sleep(1);
    }
    (void)x;
    sys_exit(0);
}

static void fork_burst(void)
{
    // fork/exit 抖动：模拟比赛里频繁创建短任务
    for (int i = 0; i < 24; i++) {
        uint32 pid = sys_fork();
        if (pid == 0) {
            sys_sleep(1);
            sys_exit(0);
        }
        if (pid != (uint32)-1) {
            sys_wait(NULL); // 立即回收，避免堆积 zombie
        }
    }
    sys_exit(0);
}

int main(void)
{
    const int N_CPU = 6;
    const int N_IO  = 6;

    fprintf(STDOUT, "test_workload_mix: start (cpu=%d io=%d burst=1)\n", N_CPU, N_IO);

    int started = 0;
    for (int i = 0; i < N_CPU; i++) {
        uint32 pid = sys_fork();
        if (pid == 0) short_cpu();
        if (pid != (uint32)-1) started++;
    }
    for (int i = 0; i < N_IO; i++) {
        uint32 pid = sys_fork();
        if (pid == 0) short_io();
        if (pid != (uint32)-1) started++;
    }
    {
        uint32 pid = sys_fork();
        if (pid == 0) fork_burst();
        if (pid != (uint32)-1) started++;
    }

    sys_sleep(30);
    dump_stats("mid1");

    sys_sleep(30);
    dump_stats("mid2");

    sys_sleep(1);
    dump_stats("mid3");

    sys_sleep(1);
    dump_stats("mid4");

    fprintf(STDOUT, "test_workload_mix: waiting children... started=%d\n", started);
    for (int i = 0; i < started; i++) {
        int r = (int)sys_wait(NULL);
        fprintf(STDOUT, "wait #%d -> %d\n", i, r);
    }

    dump_stats("end");
    fprintf(STDOUT, "test_workload_mix: done\n");
    sys_exit(0);
    return 0;
}