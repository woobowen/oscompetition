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

static int find_stat(uint32 pid, uint32 n)
{
    for (uint32 i = 0; i < n; i++) {
        if (st_buf[i].pid == pid) return (int)i;
    }
    return -1;
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

static void io_worker(int rounds)
{
    volatile uint64 x = 0;
    for (int i = 0; i < rounds; i++) {
        for (uint64 k = 0; k < 20000; k++) x += k;
        sys_sleep(1);
    }
    (void)x;
    sys_exit(0);
}

int main(void)
{
    const int N = 8;
    const int ROUNDS = 30;

    fprintf(STDOUT, "test_workload_io: start (N=%d rounds=%d)\n", N, ROUNDS);

    uint32 child_pids[32];
    int started = 0;

    for (int i = 0; i < N; i++) {
        uint32 pid = sys_fork();
        if (pid == 0) io_worker(ROUNDS);
        if (pid != (uint32)-1) {
            child_pids[started++] = pid;
        }
    }

    sys_sleep(30);
    dump_stats("mid");

    // 轮询等待子进程陆续结束，避免“wait 卡住看起来像死锁”
    int last_done = -1;
    int stagnant = 0;
    for (int t = 0; t < 400; t++) {              // 最多等 400 次 * 2tick = 800tick
        sys_sleep(2);
        uint32 n = sys_schedstat(st_buf, MAX_STAT);

        int done = 0;
        for (int i = 0; i < started; i++) {
            int idx = find_stat(child_pids[i], n);
            if (idx >= 0 && st_buf[idx].state == 4) done++;
        }

        if (done == started) break;

        if (done == last_done) stagnant++;
        else stagnant = 0;

        last_done = done;

        // 长时间无进展：打印诊断并失败退出
        if (stagnant >= 100) {                   // 约 200 tick 无变化
            fprintf(STDOUT, "FAIL: no progress for long time; dumping children states\n");
            for (int i = 0; i < started; i++) {
                int idx = find_stat(child_pids[i], n);
                if (idx >= 0) {
                    fprintf(STDOUT, " child pid=%d state=%s lvl=%d cpu=%d sleep=%d run=%d ready=%d\n",
                        (int)st_buf[idx].pid,
                        state_str(st_buf[idx].state),
                        (int)st_buf[idx].mlfq_level,
                        (int)st_buf[idx].cpu_ticks,
                        (int)st_buf[idx].sleep_count,
                        (int)st_buf[idx].run_count,
                        (int)st_buf[idx].ready_count);
                } else {
                    fprintf(STDOUT, " child pid=%d not in schedstat\n", (int)child_pids[i]);
                }
            }
            sys_exit(1);
        }
    }

    fprintf(STDOUT, "test_workload_io: waiting children... started=%d\n", started);
    for (int i = 0; i < started; i++) {
        int r = (int)sys_wait(NULL);
        fprintf(STDOUT, "wait #%d -> %d\n", i, r);
    }

    dump_stats("end");
    fprintf(STDOUT, "test_workload_io: done\n");
    sys_sleep(1);
    sys_exit(0);
    return 0;
}