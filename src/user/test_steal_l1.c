#include "help.h"

#define MAX_STAT 64
static sched_stat_t st_buf[MAX_STAT];

static int find_stat(uint32 pid, uint32 n)
{
    for (uint32 i = 0; i < n; i++) {
        if (st_buf[i].pid == pid)
            return (int)i;
    }
    return -1;
}

static void burn(uint64 iters)
{
    volatile uint64 x = 0;
    for (uint64 i = 0; i < iters; i++) {
        x = x * 1315423911u + i;
    }
    (void)x;
}

// 目标：尽量制造 L1 任务为主、L2 较少的阶段
static void l1_worker(int rounds)
{
    for (int i = 0; i < rounds; i++) {
        // 中等计算：尽量让进程停留在 L1，而不长期滞留 L2
        burn(200000);
    }
    sys_exit(0);
}

int main(void)
{
    fprintf(STDOUT, "test_steal_l1: start\n");

    const int N = 12;
    uint32 pids[32];
    int started = 0;

    for (int i = 0; i < N; i++) {
        uint32 pid = sys_fork();
        if (pid == 0) l1_worker(400);
        if (pid != (uint32)-1) pids[started++] = pid;
    }

    // 观察：当 L2 为空而 L1 有任务时，系统仍能持续推进
    uint64 last_sum = 0;
    for (int t = 0; t < 15; t++) {
        sys_sleep(1);
        uint32 n = sys_schedstat(st_buf, MAX_STAT);

        int l1_cnt = 0;
        int l2_cnt = 0;
        int hit_cnt = 0;          // 命中 pid 的数量（且非 ZOMBIE）
        uint64 sum_cpu = 0;

        for (int i = 0; i < started; i++) {
            int idx = find_stat(pids[i], n);
            if (idx < 0) continue;
            if (st_buf[idx].state == 4) continue; // ZOMBIE

            hit_cnt++; // 统计有效命中
            if (st_buf[idx].mlfq_level == 1) l1_cnt++;
            if (st_buf[idx].mlfq_level == 2) l2_cnt++;
            sum_cpu += st_buf[idx].cpu_ticks;
        }

        const char *flag = (l2_cnt == 0 && l1_cnt > 0) ? "L1-only" : "L1+L2";
        uint64 delta = (sum_cpu >= last_sum) ? (sum_cpu - last_sum) : 0;
        last_sum = sum_cpu;

        fprintf(STDOUT, "[t=%d] n=%d hit=%d/%d l1=%d l2=%d cpu_ticks_delta=%d (%s)\n",
                t, (int)n, hit_cnt, started, l1_cnt, l2_cnt, (int)delta, flag);
    }

    for (int i = 0; i < started; i++) {
        sys_wait(NULL);
    }

    fprintf(STDOUT, "test_steal_l1: done\n");
    sys_exit(0);
    return 0;
}