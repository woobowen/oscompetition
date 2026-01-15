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

static void pattern_worker(int rounds)
{
    for (int i = 0; i < rounds; i++) {
        // 期望：短 -> 中 -> 长 的重复模式
        burn(30000);   // short
        sys_sleep(1);
        burn(200000);  // medium
        sys_sleep(1);
        burn(600000);  // long
        sys_sleep(1);
    }
    sys_exit(0);
}

static void cpu_noise(int rounds)
{
    for (int i = 0; i < rounds; i++) {
        burn(200000);
    }
    sys_exit(0);
}

static void l2_expire_worker(int rounds)
{
    // 目标：持续 CPU-burst，让其被多次 time-slice expire 并最终下沉到 L2，
    // 从而触发 Lab-11 的 mkv_l2_boost_count 增长。
    for (int i = 0; i < rounds; i++) {
        burn(400000);
    }
    sys_exit(0);
}

static void print_one(uint32 pid, uint32 n, const char *tag)
{
    int idx = find_stat(pid, n);
    if (idx < 0) {
        fprintf(STDOUT, "  %s pid=%d not found\n", tag, (int)pid);
        return;
    }

    sched_stat_t *s = &st_buf[idx];
    uint64 hit = s->mkv_pred_hit;
    uint64 total = s->mkv_pred_total;
    uint64 rate = (total == 0) ? 0 : (hit * 100 / total);
    fprintf(STDOUT,
            "  %s pid=%d state=%s lvl=%d mkvP=%d mkvH=%d hit=%d%% mkvB=%d pred=%s act=%s\n",
            tag,
            (int)s->pid,
            state_str(s->state),
            (int)s->mlfq_level,
            (int)total,
            (int)hit,
            (int)rate,
            (int)s->mkv_l2_boost_count,
            mkv_state_str(s->mkv_last_pred_state),
            mkv_state_str(s->mkv_last_act_state));
}

int main(void)
{
    fprintf(STDOUT, "test_mkv_pattern: start\n");

    // 噪声任务：增加调度扰动，让预测更有说服力
    for (int i = 0; i < 2; i++) {
        uint32 pid = sys_fork();
        if (pid == 0) cpu_noise(200);
    }

    uint32 pid_pat = sys_fork();
    if (pid_pat == 0)
        pattern_worker(1200);

    uint32 pid_boost = sys_fork();
    if (pid_boost == 0)
        l2_expire_worker(10000);

    // 观察预测命中率上升趋势
    for (int t = 0; t < 20; t++) {
        sys_sleep(10);
        uint32 n = sys_schedstat(st_buf, MAX_STAT);
        fprintf(STDOUT, "[t=%d] n=%d\n", t, (int)n);
        print_one(pid_pat, n, "PAT");
        print_one(pid_boost, n, "BST");
    }

    fprintf(STDOUT, "test_mkv_pattern: done\n");
    sys_exit(0);
    return 0;
}
