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
    // 0..8 => (S/M/L) × (sleep/expire/higher)
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

    fprintf(STDOUT, "[%s] entries=%d\n", tag, n);
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

static void cpu_worker(void)
{
    volatile uint64 x = 0;
    for (int r = 0; r < 200; r++) {
        for (uint64 i = 0; i < 80000; i++) {
            x += i;
        }
    }
    (void)x;
    sys_exit(0);
}

static void io_worker(void)
{
    for (int i = 0; i < 30; i++) {
        sys_sleep(1);
    }
    sys_exit(0);
}

int main(void)
{
    fprintf(STDOUT, "test_schedstat: start\n");

    uint32 pid;

    pid = sys_fork();
    if (pid == 0) cpu_worker();

    pid = sys_fork();
    if (pid == 0) cpu_worker();

    pid = sys_fork();
    if (pid == 0) io_worker();

    pid = sys_fork();
    if (pid == 0) io_worker();

    sys_sleep(30);
    dump_stats("mid");

    fprintf(STDOUT, "test_schedstat: waiting children...\n");
    for (int i = 0; i < 4; i++) {
        int r = (int)sys_wait(NULL);
        fprintf(STDOUT, "wait #%d -> %d\n", i, r);
    }
    fprintf(STDOUT, "test_schedstat: children done\n");

    dump_stats("end");
    fprintf(STDOUT, "test_schedstat: done\n");
    sys_exit(0);
    return 0; // never reached
}
