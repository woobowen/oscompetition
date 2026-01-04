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


static const char *mkv_state_str(uint32 s)
{
    switch (s) {
    case 0: return "S/sleep";
    case 1: return "S/expire";
    case 2: return "S/higher";
    case 3: return "M/sleep";
    case 4: return "M/expire";
    case 5: return "M/higher";
    case 6: return "L/sleep";
    case 7: return "L/expire";
    case 8: return "L/higher";
    default: return "-";
    }
}

static void dump_stats(const char *tag)
{
    uint32 n = sys_schedstat(st_buf, MAX_STAT);
    fprintf(STDOUT, "[%s] entries=%d\n", tag, (int)n);
    fprintf(STDOUT, " pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act\n");
    for (uint32 i = 0; i < n; i++) {
        fprintf(STDOUT,
            "%d %s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %s\n",
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
            (int)st_buf[i].first_run_tick,
            (int)st_buf[i].mkv_pred_total,
            (int)st_buf[i].mkv_pred_hit,
            (int)st_buf[i].mkv_l2_boost_count,
            mkv_state_str(st_buf[i].mkv_last_pred_state),
            mkv_state_str(st_buf[i].mkv_last_act_state));
    }
}

static void cpu_worker(int iters)
{
    volatile uint64 x = 0;
    for (int r = 0; r < iters; r++) {
        for (uint64 i = 0; i < 120000; i++) {
            x = x * 1315423911u + i;
        }
    }
    (void)x;
    sys_exit(0);
}

int main(void)
{
    const int N = 8;
    fprintf(STDOUT, "test_workload_cpu: start (N=%d)\n", N);

    int started = 0;
    for (int i = 0; i < N; i++) {
        uint32 pid = sys_fork();
        if (pid == 0) cpu_worker(500);
        if (pid != (uint32)-1) started++;
    }

    sys_sleep(10);
    dump_stats("mid");

    fprintf(STDOUT, "test_workload_cpu: waiting children... started=%d\n", started);
    for (int i = 0; i < started; i++) {
        int r = (int)sys_wait(NULL);
        fprintf(STDOUT, "wait #%d -> %d\n", i, r);
    }

    dump_stats("end");
    fprintf(STDOUT, "test_workload_cpu: done\n");
    sys_exit(0);
    return 0;
}