lab-10:


【1】CPU-bound 压力：1~N 个进程纯循环计算（不 sleep）

run ./test_workload_cpu test_cpu 111 222 333

======== test start  ========

test_workload_cpu: start (N=8)
[mid] entries=10
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
2 RUNNING 0 0 3 3 11 11 10 0 0 0 10 1
3 RUNNING 0 2 8 5 3 3 2 1 1 0 0 1
4 RUNNABLE 2 3 7 4 3 4 3 2 1 0 0 1
5 RUNNABLE 2 3 4 3 2 3 2 2 0 0 0 2
6 RUNNABLE 1 2 4 3 2 3 2 1 1 0 0 2
7 RUNNABLE 1 2 6 4 2 3 2 1 1 0 0 3
8 RUNNABLE 2 3 5 3 2 3 2 2 0 0 0 3
9 RUNNABLE 1 2 7 4 2 3 2 1 1 0 0 4
10 RUNNABLE 2 3 7 4 2 3 2 2 0 0 0 4
test_workload_cpu: waiting children... started=8
wait #0 -> 5
wait #1 -> 8
wait #2 -> 10
wait #3 -> 3
wait #4 -> 4
wait #5 -> 7
wait #6 -> 6
wait #7 -> 9
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
2 RUNNING 0 0 3 3 19 19 18 0 0 0 18 1
test_workload_cpu: done

======== test sucess ========

【2】I/O-bound 压力：1~N 个进程反复 sleep/wakeup 模拟 I/O 操作

run ./test_workload_io test_io 111 222 333

======== test start  ========

test_workload_io: start (N=8 rounds=30)
[mid] entries=10
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
2 RUNNING 0 0 0 0 34 34 33 0 0 0 33 1
3 ZOMBIE 0 0 0 0 31 31 31 0 0 0 30 1
4 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1
5 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1
6 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1
7 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1
8 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1
9 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1
10 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1
test_workload_io: waiting children... started=8
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 10
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
2 RUNNING 0 0 0 0 36 36 35 0 0 0 35 1
test_workload_io: done

======== test sucess ========

【3】混合压力 + fork/exit 抖动：M 个 CPU-bound + N 个 I/O-bound 进程混合运行

run ./test_workload_mix test_mix 111 222 333

======== test start ========

test_workload_mix: start (cpu=6 io=6 burst=1)
[mid1] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
 1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
 2 RUNNING 0 0 0 0 34 34 33 0 0 0 33 1
 3 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2
 4 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2
 5 ZOMBIE 1 2 0 0 3 3 3 1 1 0 0 2
 6 ZOMBIE 1 2 0 0 3 3 3 1 1 0 0 2
 7 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3
 8 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3
 9 SLEEPING 0 0 1 1 30 30 30 0 0 0 30 3
10 SLEEPING 0 0 1 1 30 30 30 0 0 0 30 3

[mid3] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
 1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
 2 RUNNING 0 0 1 1 64 64 63 0 0 0 63 2
 3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
 4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
 5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3
 6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3
 7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3
 8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3
 9 RUNNING 0 0 2 2 60 60 59 0 0 0 59 4
10 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4
11 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4
12 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4
13 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4
14 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 4
[mid4] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
 1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
 2 RUNNING 0 0 1 1 65 65 64 0 0 0 64 2
 3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
 4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
 5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3
 6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3
 7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3
 8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3
 9 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4
10 RUNNABLE 0 0 2 2 60 61 60 0 0 0 60 4
11 RUNNABLE 0 0 2 2 60 61 60 0 0 0 60 4
12 RUNNABLE 0 0 2 2 60 61 60 0 0 0 60 4
13 RUNNABLE 0 0 2 2 60 61 60 0 0 0 60 4
14 RUNNABLE 0 0 2 2 60 61 60 0 0 0 60 4
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 4
test_workload_mix: waiting children... started=13
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 10
wait #8 -> 11
wait #9 -> 12
wait #10 -> 13
wait #11 -> 14
wait #12 -> 15
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
 1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
 2 RUNNING 1 1 1 1 66 66 65 1 0 0 64 2
test_workload_mix: done

======== test sucess ========


lab-11:

【1】CPU-bound 压力：1~N 个进程纯循环计算（不 sleep）


run ./test_workload_cpu test_cpu 111 222 333

======== test start  ========

test_workload_cpu: start (N=8)
[mid] entries=10
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 7 7 7 7 6 0 0 0 6 1 6 5 0 S/sleep S/sleep
3 RUNNABLE 1 1 0 0 1 2 1 1 0 0 0 1 1 0 0 S/expire S/expire
4 RUNNABLE 1 2 7 6 2 3 2 1 1 0 0 2 1 0 0 S/expire S/higher
5 RUNNABLE 1 1 2 2 1 2 1 1 0 0 0 3 1 0 0 S/expire S/expire
6 RUNNABLE 1 1 3 3 1 2 1 1 0 0 0 4 1 0 0 S/expire S/expire
7 RUNNABLE 1 2 8 4 2 3 2 1 1 0 0 5 1 0 0 S/expire S/higher
8 RUNNABLE 1 1 5 5 1 2 1 1 0 0 0 6 1 0 0 S/expire S/expire
9 RUNNABLE 1 1 6 6 1 2 1 1 0 0 0 7 1 0 0 S/expire S/expire
10 RUNNABLE 1 1 7 7 1 2 1 1 0 0 0 8 1 0 0 S/expire S/expire
test_workload_cpu: waiting children... started=8
wait #0 -> 4
wait #1 -> 7
wait #2 -> 9
wait #3 -> 8
wait #4 -> 5
wait #5 -> 6
wait #6 -> 10
wait #7 -> 3
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 7 7 15 15 14 0 0 0 14 1 14 13 0 S/sleep S/sleep
test_workload_cpu: done

======== test sucess ========



【2】I/O-bound 压力：1~N 个进程反复 sleep/wakeup 模拟 I/O 操作

run ./test_workload_io test_io 111 222 333

======== test start  ========

test_workload_io: start (N=8 rounds=30)
[mid] entries=10
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 34 34 33 0 0 0 33 2 33 32 0 S/sleep S/sleep
3 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
4 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
5 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
6 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
7 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
8 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
9 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
10 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
test_workload_io: waiting children... started=8
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 10
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 36 36 35 0 0 0 35 2 35 34 0 S/sleep S/sleep
test_workload_io: done

【3】混合压力 + fork/exit 抖动：M 个 CPU-bound + N 个 I/O-bound 进程混合运行

run ./test_workload_mix test_mix 111 222 333

======== test start  ========

test_workload_mix: start (cpu=6 io=6 burst=1)
[mid1] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 2 2 32 32 31 0 0 0 31 1 31 30 0 S/sleep S/sleep
3 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 1 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
6 ZOMBIE 1 2 2 1 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
7 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
9 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 4 27 26 0 S/sleep S/sleep
10 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 4 27 26 0 S/sleep S/sleep
11 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 4 27 26 0 S/sleep S/sleep
12 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 4 27 26 0 S/sleep S/sleep
13 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 4 27 26 0 S/sleep S/sleep
14 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 4 27 26 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
[mid2] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 1 2 2 63 63 62 1 0 0 61 1 62 59 0 S/sleep S/sleep
3 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 1 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
6 ZOMBIE 1 2 2 1 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
7 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
9 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 4 57 56 0 S/sleep S/sleep
10 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 4 57 56 0 S/sleep S/sleep
11 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 4 57 56 0 S/sleep S/sleep
12 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 4 57 56 0 S/sleep S/sleep
13 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 4 57 56 0 S/sleep S/sleep
14 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 4 57 56 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
[mid3] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 2 2 2 65 65 64 2 0 0 62 1 64 60 0 S/sleep S/sleep
3 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 1 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
6 ZOMBIE 1 2 2 1 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
7 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
9 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
10 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
11 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
12 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
13 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
14 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
[mid4] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 3 2 2 67 67 66 3 0 0 63 1 66 61 0 S/sleep S/sleep
3 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 1 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
6 ZOMBIE 1 2 2 1 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
7 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
9 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
10 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
11 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
12 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
13 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
14 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
test_workload_mix: waiting children... started=13
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 15
wait #7 -> 9
wait #8 -> 10
wait #9 -> 11
wait #10 -> 12
wait #11 -> 13
wait #12 -> 14
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 4 2 2 69 69 68 4 0 0 64 1 68 62 0 S/sleep S/sleep
test_workload_mix: done


预测全是S/sleep -> 冷启动问题 -> 解决之后结果如上，没有比lab-10更优，反而IO 等待时间更长了。

修改：Wakeup 入队策略：在 mlfq.c 里，mlfq_on_wakeup() 现在会选择 “L0 队列长度最短的 CPU” 来入队（而不是固定用当前 CPU）。
Markov burst 分档阈值：在 proc.c 里把 burst 分类改成：
S：run_ticks == 1
M：run_ticks == 2..4
L：run_ticks >= 5

之后的结果：

1. test_cpu:

run ./test_workload_cpu test_cpu 111 222 333

======== test start  ========

test_workload_cpu: start (N=8)
[mid] entries=10
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 14 14 13 0 0 0 13 1 13 12 0 S/sleep S/sleep
3 RUNNABLE 1 1 0 0 1 2 1 1 0 0 0 1 1 0 0 S/expire S/expire
4 RUNNABLE 1 2 7 6 2 3 2 1 1 0 0 2 1 0 0 S/expire S/higher
5 RUNNABLE 1 1 2 2 1 2 1 1 0 0 0 3 1 0 0 S/expire S/expire
6 RUNNABLE 1 1 3 3 1 2 1 1 0 0 0 4 1 0 0 S/expire S/expire
7 RUNNABLE 1 2 8 4 2 3 2 1 1 0 0 5 1 0 0 S/expire S/higher
8 RUNNABLE 1 1 5 5 1 2 1 1 0 0 0 6 1 0 0 S/expire S/expire
9 RUNNABLE 1 1 6 6 1 2 1 1 0 0 0 7 1 0 0 S/expire S/expire
10 RUNNABLE 1 1 7 7 1 2 1 1 0 0 0 8 1 0 0 S/expire S/expire
test_workload_cpu: waiting children... started=8
wait #0 -> 4
wait #1 -> 7
wait #2 -> 9
wait #3 -> 5
wait #4 -> 6
wait #5 -> 10
wait #6 -> 8
wait #7 -> 3
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 3 1 22 22 21 0 0 0 21 1 21 20 0 S/sleep S/sleep
test_workload_cpu: done

======== test sucess ========

2. test_io:

run ./test_workload_io test_io 111 222 333

======== test start  ========

test_workload_io: start (N=8 rounds=30)
[mid] entries=10
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 34 34 33 0 0 0 33 1 33 32 0 S/sleep S/sleep
3 ZOMBIE 0 0 0 0 31 31 31 0 0 0 30 1 30 29 0 S/sleep S/sleep
4 ZOMBIE 0 0 0 0 31 31 31 0 0 0 30 1 30 29 0 S/sleep S/sleep
5 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1 30 29 0 S/sleep S/sleep
6 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1 30 29 0 S/sleep S/sleep
7 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1 30 29 0 S/sleep S/sleep
8 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1 30 29 0 S/sleep S/sleep
9 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1 30 29 0 S/sleep S/sleep
10 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 1 30 29 0 S/sleep S/sleep
test_workload_io: waiting children... started=8
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 10
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 36 36 35 0 0 0 35 1 35 34 0 S/sleep S/sleep
test_workload_io: done

======== test sucess ========

3. test_mix:
   
run ./test_workload_mix test_mix 111 222 333

======== test start  ========

test_workload_mix: start (cpu=6 io=6 burst=1)
[mid1] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 34 34 33 0 0 0 33 2 33 32 0 S/sleep S/sleep
3 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 2 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
6 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 4 1 0 0 S/expire S/expire
8 ZOMBIE 0 0 3 3 1 1 1 0 0 0 0 5 0 0 0 S/sleep S/sleep
9 SLEEPING 0 1 3 3 29 29 29 1 0 0 28 5 28 26 0 S/sleep S/sleep
10 SLEEPING 0 4 3 3 32 32 32 4 0 0 28 5 31 25 0 S/sleep S/sleep
11 RUNNABLE 0 1 3 3 27 28 27 0 0 0 27 5 27 26 0 S/sleep S/sleep
12 SLEEPING 0 5 3 3 30 30 30 2 0 0 28 5 29 26 0 S/sleep S/sleep
13 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 5 27 26 0 S/sleep S/sleep
14 SLEEPING 0 2 3 3 29 29 29 1 0 0 28 5 28 26 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 5 24 23 0 S/sleep S/sleep
[mid2] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 1 0 0 65 65 64 1 0 0 63 2 64 61 0 S/sleep S/sleep
3 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 2 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
6 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 4 1 0 0 S/expire S/expire
8 ZOMBIE 0 0 3 3 1 1 1 0 0 0 0 5 0 0 0 S/sleep S/sleep
9 SLEEPING 0 1 3 3 60 60 60 1 0 0 59 5 59 57 0 S/sleep S/sleep
10 SLEEPING 0 11 3 3 67 67 67 8 0 0 59 5 66 56 0 S/sleep S/sleep
11 RUNNABLE 0 1 4 3 57 58 57 0 0 0 57 5 57 56 0 S/sleep S/sleep
12 SLEEPING 0 9 3 3 64 64 64 5 0 0 59 5 63 57 0 S/sleep S/sleep
13 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 5 57 56 0 S/sleep S/sleep
14 RUNNABLE 0 2 3 3 59 60 59 1 0 0 58 5 59 56 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 5 24 23 0 S/sleep S/sleep
[mid3] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 2 0 0 67 67 66 2 0 0 64 2 66 62 0 S/sleep S/sleep
3 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 2 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
6 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 4 1 0 0 S/expire S/expire
8 ZOMBIE 0 0 3 3 1 1 1 0 0 0 0 5 0 0 0 S/sleep S/sleep
9 ZOMBIE 0 1 3 3 62 62 62 1 0 0 60 5 61 58 0 S/sleep S/sleep
10 RUNNABLE 0 11 3 3 68 69 68 8 0 0 60 5 68 57 0 S/sleep S/sleep
11 RUNNABLE 0 1 5 3 58 59 58 0 0 0 58 5 58 57 0 S/sleep S/sleep
12 RUNNABLE 0 9 3 3 65 66 65 5 0 0 60 5 65 58 0 S/sleep S/sleep
13 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 5 58 57 0 S/sleep S/sleep
14 RUNNABLE 0 2 3 3 61 62 61 1 0 0 60 5 61 58 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 5 24 23 0 S/sleep S/sleep
[mid4] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 3 0 0 69 69 68 3 0 0 65 2 68 63 0 S/sleep S/sleep
3 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 2 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
6 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 4 1 0 0 S/expire S/expire
8 ZOMBIE 0 0 3 3 1 1 1 0 0 0 0 5 0 0 0 S/sleep S/sleep
9 ZOMBIE 0 1 3 3 62 62 62 1 0 0 60 5 61 58 0 S/sleep S/sleep
10 ZOMBIE 0 11 3 3 69 69 69 8 0 0 60 5 68 57 0 S/sleep S/sleep
11 SLEEPING 0 1 6 3 60 60 60 0 0 0 60 5 59 59 0 S/sleep S/sleep
12 ZOMBIE 0 9 3 3 66 66 66 5 0 0 60 5 65 58 0 S/sleep S/sleep
13 RUNNING 0 0 6 3 60 60 59 0 0 0 59 5 59 58 0 S/sleep S/sleep
14 ZOMBIE 0 2 3 3 62 62 62 1 0 0 60 5 61 58 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 5 24 23 0 S/sleep S/sleep
test_workload_mix: waiting children... started=13
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 10
wait #8 -> 11
wait #9 -> 12
wait #10 -> 13
wait #11 -> 14
wait #12 -> 15
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 1 4 0 0 70 70 69 4 0 0 65 2 69 63 0 S/sleep S/expire
test_workload_mix: done

效果还是不好，再次改进：把 **mlfq_on_wakeup() 的“选 CPU 负载指标”**从仅 L0_len 改为更合理的 L0_len + L1_len + L2_len + running(0/1)，并且加一个“小阈值”：差距不大就留在本核，避免抖动。

run ./test_workload_mix test_mix 111 222 333

======== test start  ========

test_workload_mix: start (cpu=6 io=6 burst=1)
[mid1] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 1 0 0 35 35 34 1 0 0 33 1 34 31 0 S/sleep S/sleep
3 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
4 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
6 ZOMBIE 1 1 2 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
7 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
8 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 4 1 0 0 S/expire S/expire
9 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 5 27 26 0 S/sleep S/sleep
10 SLEEPING 0 0 3 3 28 28 28 0 0 0 28 5 27 27 0 S/sleep S/sleep
11 SLEEPING 0 0 3 3 28 28 28 0 0 0 28 5 27 27 0 S/sleep S/sleep
12 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 5 27 26 0 S/sleep S/sleep
13 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 5 27 26 0 S/sleep S/sleep
14 SLEEPING 0 0 3 3 28 28 28 0 0 0 28 5 27 27 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 5 24 23 0 S/sleep S/sleep
[mid2] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 2 0 0 66 66 65 2 0 0 63 1 65 61 0 S/sleep S/sleep
3 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
4 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
6 ZOMBIE 1 1 2 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
7 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
8 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 4 1 0 0 S/expire S/expire
9 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 5 57 56 0 S/sleep S/sleep
10 SLEEPING 0 0 3 3 59 59 59 0 0 0 59 5 58 58 0 S/sleep S/sleep
11 SLEEPING 0 0 3 3 59 59 59 0 0 0 59 5 58 58 0 S/sleep S/sleep
12 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 5 57 56 0 S/sleep S/sleep
13 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 5 57 56 0 S/sleep S/sleep
14 SLEEPING 0 0 3 3 59 59 59 0 0 0 59 5 58 58 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 5 24 23 0 S/sleep S/sleep
[mid3] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 3 0 0 68 68 67 3 0 0 64 1 67 62 0 S/sleep S/sleep
3 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
4 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
6 ZOMBIE 1 1 2 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
7 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
8 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 4 1 0 0 S/expire S/expire
9 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 5 58 57 0 S/sleep S/sleep
10 ZOMBIE 0 0 3 3 61 61 61 0 0 0 60 5 60 59 0 S/sleep S/sleep
11 RUNNING 0 0 3 3 61 61 60 0 0 0 60 5 60 59 0 S/sleep S/sleep
12 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 5 58 57 0 S/sleep S/sleep
13 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 5 58 57 0 S/sleep S/sleep
14 RUNNABLE 0 0 3 3 60 61 60 0 0 0 60 5 60 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 5 24 23 0 S/sleep S/sleep
[mid4] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 4 0 0 70 70 69 4 0 0 65 1 69 63 0 S/sleep S/sleep
3 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
4 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
6 ZOMBIE 1 1 2 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
7 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
8 ZOMBIE 1 1 2 2 2 2 2 1 0 0 0 4 1 0 0 S/expire S/expire
9 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 5 59 58 0 S/sleep S/sleep
10 ZOMBIE 0 0 3 3 61 61 61 0 0 0 60 5 60 59 0 S/sleep S/sleep
11 ZOMBIE 0 0 3 3 61 61 61 0 0 0 60 5 60 59 0 S/sleep S/sleep
12 SLEEPING 0 0 6 3 60 60 60 0 0 0 60 5 59 59 0 S/sleep S/sleep
13 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 5 59 58 0 S/sleep S/sleep
14 ZOMBIE 0 0 3 3 61 61 61 0 0 0 60 5 60 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 5 24 23 0 S/sleep S/sleep
test_workload_mix: waiting children... started=13
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 10
wait #7 -> 11
wait #8 -> 12
wait #9 -> 14
wait #10 -> 15
wait #11 -> 9
wait #12 -> 13
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 5 0 0 72 72 71 5 0 0 66 1 71 64 0 S/sleep S/sleep
test_workload_mix: done

======== test sucess ========

把 wakeup 选核的负载指标改成“L0 加权更大”（2*L0 + L1 + L2 + running），让 I/O 唤醒更偏向选择 L0 更空的 CPU

run ./test_workload_mix test_mix 111 222 333

======== test start  ========

test_workload_mix: start (cpu=6 io=6 burst=1)
[mid1] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 34 34 33 0 0 0 33 1 33 32 0 S/sleep S/sleep
3 ZOMBIE 1 2 2 2 3 3 3 1 1 0 0 1 1 0 0 S/expire S/higher
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
6 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
8 ZOMBIE 0 0 3 3 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
9 SLEEPING 0 0 3 3 28 28 28 0 0 0 28 4 27 27 0 S/sleep S/sleep
10 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 4 27 26 0 S/sleep S/sleep
11 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 4 27 26 0 S/sleep S/sleep
12 SLEEPING 0 0 3 3 28 28 28 0 0 0 28 4 27 27 0 S/sleep S/sleep
13 SLEEPING 0 0 3 3 28 28 28 0 0 0 28 4 27 27 0 S/sleep S/sleep
14 RUNNABLE 0 0 3 3 27 28 27 0 0 0 27 4 27 26 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
[mid2] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 1 0 0 65 65 64 1 0 0 63 1 64 61 0 S/sleep S/sleep
3 ZOMBIE 1 2 2 2 3 3 3 1 1 0 0 1 1 0 0 S/expire S/higher
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
6 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
8 ZOMBIE 0 0 3 3 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
9 RUNNING 0 0 3 3 59 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
10 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 4 57 56 0 S/sleep S/sleep
11 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 4 57 56 0 S/sleep S/sleep
12 RUNNABLE 0 0 3 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
13 RUNNABLE 0 0 3 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
14 RUNNABLE 0 0 4 3 57 58 57 0 0 0 57 4 57 56 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
[mid3] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 2 0 0 67 67 66 2 0 0 64 1 66 62 0 S/sleep S/sleep
3 ZOMBIE 1 2 2 2 3 3 3 1 1 0 0 1 1 0 0 S/expire S/higher
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
6 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
8 ZOMBIE 0 0 3 3 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
9 RUNNABLE 0 0 3 3 60 61 60 0 0 0 60 4 60 59 0 S/sleep S/sleep
10 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
11 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
12 RUNNABLE 0 0 3 3 60 61 60 0 0 0 60 4 60 59 0 S/sleep S/sleep
13 RUNNABLE 0 0 3 3 60 61 60 0 0 0 60 4 60 59 0 S/sleep S/sleep
14 RUNNABLE 0 0 5 3 58 59 58 0 0 0 58 4 58 57 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
[mid4] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 3 0 0 69 69 68 3 0 0 65 1 68 63 0 S/sleep S/sleep
3 ZOMBIE 1 2 2 2 3 3 3 1 1 0 0 1 1 0 0 S/expire S/higher
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
6 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 3 2 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
8 ZOMBIE 0 0 3 3 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
9 ZOMBIE 0 0 3 3 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
10 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
11 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
12 ZOMBIE 0 0 3 3 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
13 ZOMBIE 0 0 3 3 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
14 RUNNABLE 0 0 6 3 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
15 ZOMBIE 0 0 3 3 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
test_workload_mix: waiting children... started=13
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 11
wait #8 -> 12
wait #9 -> 13
wait #10 -> 15
wait #11 -> 10
wait #12 -> 14
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 4 0 0 71 71 70 4 0 0 66 1 70 64 0 S/sleep S/sleep
test_workload_mix: done

======== test sucess ========

修改： running 权重提高：避免把 wakeup 任务塞给“正在跑用户进程”的 CPU。

// 新建进程入队：优先放到最空的 CPU。
// 与 wakeup 不同，这里不做本核偏好；否则 fork 风暴会把所有子进程压在父进程所在核，
// 在 per-CPU runqueue + 仅偷 L2 的条件下，会显著拉高其他进程的 ready wait。

run ./test_workload_mix test_mix 111 222 333

======== test start  ========

test_workload_mix: start (cpu=6 io=6 burst=1)
[mid1] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 1 1 33 33 32 0 0 0 32 2 32 31 0 S/sleep S/sleep
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
9 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 4 28 28 0 S/sleep S/sleep
10 RUNNABLE 0 0 2 2 28 29 28 0 0 0 28 4 28 27 0 S/sleep S/sleep
11 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 4 28 28 0 S/sleep S/sleep
12 RUNNABLE 0 0 2 2 28 29 28 0 0 0 28 4 28 27 0 S/sleep S/sleep
13 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 4 28 28 0 S/sleep S/sleep
14 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 4 28 28 0 S/sleep S/sleep
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
[mid2] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 1 2 1 64 64 63 1 0 0 62 2 63 60 0 S/sleep S/sleep
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
9 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
10 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
11 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
12 RUNNABLE 0 0 3 2 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
13 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
14 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 4 59 58 0 S/sleep S/sleep
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
[mid3] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 2 2 1 66 66 65 2 0 0 63 2 65 61 0 S/sleep S/sleep
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
9 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
10 SLEEPING 0 0 3 2 61 61 61 0 0 0 61 4 60 60 0 S/sleep S/sleep
11 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
12 RUNNABLE 0 0 4 2 60 61 60 0 0 0 60 4 60 59 0 S/sleep S/sleep
13 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
14 RUNNING 0 0 3 2 61 61 60 0 0 0 60 4 60 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
[mid4] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 3 2 1 68 68 67 3 0 0 64 2 67 62 0 S/sleep S/sleep
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
9 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
10 ZOMBIE 0 0 3 2 62 62 62 0 0 0 61 4 61 60 0 S/sleep S/sleep
11 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
12 RUNNING 0 0 5 2 62 62 61 0 0 0 61 4 61 60 0 S/sleep S/sleep
13 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
14 ZOMBIE 0 0 3 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 4 24 23 0 S/sleep S/sleep
test_workload_mix: waiting children... started=13
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 10
wait #8 -> 11
wait #9 -> 12
wait #10 -> 13
wait #11 -> 14
wait #12 -> 15
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 1 4 2 1 69 69 68 4 0 0 64 2 68 62 0 S/sleep S/expire
test_workload_mix: done

======== test sucess ========

修改：把 wakeup 的任务插入 L0 队列头部（而不是尾部），这样刚被唤醒的 I/O 任务更容易立刻得到 CPU

run ./test_workload_mix test_mix 111 222 333

======== test start  ========

test_workload_mix: start (cpu=6 io=6 burst=1)
[mid1] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 1 0 0 35 35 34 1 0 0 33 1 34 32 0 S/sleep S/sleep
3 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
4 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 2 0 0 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
6 ZOMBIE 1 2 1 1 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
7 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
9 SLEEPING 0 0 1 1 30 30 30 0 0 0 30 3 29 29 0 S/sleep S/sleep
10 RUNNABLE 0 0 1 1 29 30 29 0 0 0 29 3 29 28 0 S/sleep S/sleep
11 SLEEPING 0 0 1 1 30 30 30 0 0 0 30 3 29 29 0 S/sleep S/sleep
12 SLEEPING 0 0 1 1 30 30 30 0 0 0 30 3 29 29 0 S/sleep S/sleep
13 RUNNABLE 0 0 1 1 29 30 29 0 0 0 29 3 29 28 0 S/sleep S/sleep
14 SLEEPING 0 0 1 1 30 30 30 0 0 0 30 3 29 29 0 S/sleep S/sleep
15 ZOMBIE 0 0 1 1 25 25 25 0 0 0 24 3 24 23 0 S/sleep S/sleep
[mid2] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 2 0 0 66 66 65 2 0 0 63 1 65 62 0 S/sleep S/sleep
3 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
4 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 2 0 0 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
6 ZOMBIE 1 2 1 1 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
7 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
9 RUNNABLE 0 0 1 1 60 61 60 0 0 0 60 3 60 59 0 S/sleep S/sleep
10 RUNNABLE 0 0 2 1 59 60 59 0 0 0 59 3 59 58 0 S/sleep S/sleep
11 RUNNABLE 0 0 1 1 60 61 60 0 0 0 60 3 60 59 0 S/sleep S/sleep
12 RUNNABLE 0 0 1 1 60 61 60 0 0 0 60 3 60 59 0 S/sleep S/sleep
13 RUNNABLE 0 0 2 1 59 60 59 0 0 0 59 3 59 58 0 S/sleep S/sleep
14 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 1 1 25 25 25 0 0 0 24 3 24 23 0 S/sleep S/sleep
[mid3] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 3 0 0 68 68 67 3 0 0 64 1 67 63 0 S/sleep S/sleep
3 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
4 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 2 0 0 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
6 ZOMBIE 1 2 1 1 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
7 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
9 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
10 RUNNING 0 0 3 1 61 61 60 0 0 0 60 3 60 59 0 S/sleep S/sleep
11 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
12 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
13 RUNNABLE 0 0 3 1 60 61 60 0 0 0 60 3 60 59 0 S/sleep S/sleep
14 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 1 1 25 25 25 0 0 0 24 3 24 23 0 S/sleep S/sleep
[mid4] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 4 0 0 70 70 69 4 0 0 65 1 69 64 0 S/sleep S/sleep
3 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
4 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 2 0 0 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
6 ZOMBIE 1 2 1 1 3 3 3 1 1 0 0 2 1 0 0 S/expire S/higher
7 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 0 0 0 0 1 1 1 0 0 0 0 2 0 0 0 S/sleep S/sleep
9 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
10 ZOMBIE 0 0 3 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
11 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
12 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
13 ZOMBIE 0 0 4 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
14 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 1 1 25 25 25 0 0 0 24 3 24 23 0 S/sleep S/sleep
test_workload_mix: waiting children... started=13
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 10
wait #8 -> 11
wait #9 -> 12
wait #10 -> 13
wait #11 -> 14
wait #12 -> 15
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 1 6 0 0 71 71 70 5 0 0 65 1 70 64 0 S/sleep S/expire
test_workload_mix: done

======== test sucess ========


修改了：把 wakeup 选核相关参数集中到 mlfq.c 顶部一处：
MLFQ_WAKEUP_HYSTERESIS
MLFQ_WAKEUP_W_L0 / _W_L1 / _W_L2 / _W_RUNNING
把 mlfq_cpu_load_locked() 里的 2*L0 + L1 + L2 + 2*running 改为用上述宏计算


1. test_cpu:

run ./test_workload_cpu test_cpu 111 222 333

======== test start  ========

test_workload_cpu: start (N=8)
[mid] entries=10
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 14 14 13 0 0 0 13 2 13 12 0 S/sleep S/sleep
3 RUNNABLE 2 3 4 4 2 3 2 2 0 0 0 2 2 0 1 M/expire M/expire
4 RUNNABLE 2 4 7 6 3 4 3 2 1 0 0 3 2 0 1 M/expire S/higher
5 ZOMBIE 2 3 6 2 4 4 4 2 1 0 0 2 2 0 0 S/higher S/expire
6 RUNNING 1 2 8 6 2 2 1 1 0 0 0 4 1 0 0 S/expire S/expire
7 ZOMBIE 2 3 6 2 4 4 4 2 1 0 0 3 2 0 0 S/higher S/expire
8 RUNNABLE 1 1 3 3 1 2 1 1 0 0 0 5 1 0 0 S/expire S/expire
9 ZOMBIE 2 3 6 2 4 4 4 2 1 0 0 4 2 0 0 S/higher S/expire
10 RUNNABLE 1 1 4 4 1 2 1 1 0 0 0 6 1 0 0 S/expire S/expire
test_workload_cpu: waiting children... started=8
wait #0 -> 5
wait #1 -> 7
wait #2 -> 9
wait #3 -> 4
wait #4 -> 6
wait #5 -> 3
wait #6 -> 8
wait #7 -> 10
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 1 0 0 20 20 19 1 0 0 18 2 19 16 0 S/sleep S/sleep
test_workload_cpu: done

======== test sucess ========

2. test_io:

run ./test_workload_io test_io 111 222 333

======== test start  ========

test_workload_io: start (N=8 rounds=30)
[mid] entries=10
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 34 34 33 0 0 0 33 2 33 32 0 S/sleep S/sleep
3 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
4 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
5 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
6 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
7 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
8 ZOMBIE 0 0 0 0 31 31 31 0 0 0 30 2 30 29 0 S/sleep S/sleep
9 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
10 RUNNABLE 0 0 0 0 30 31 30 0 0 0 30 2 30 29 0 S/sleep S/sleep
test_workload_io: waiting children... started=8
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 10
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 1 0 0 33 33 33 0 0 0 33 1 32 32 0 S/sleep S/sleep
2 RUNNING 0 2 0 0 37 37 36 1 0 0 35 2 36 33 0 S/sleep S/sleep
test_workload_io: done

======== test sucess ========

3. test_mix:

run ./test_workload_mix test_mix 111 222 333

======== test start  ========

test_workload_mix: start (cpu=6 io=6 burst=1)
[mid1] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 1 1 5 5 5 0 0 0 5 1 4 4 0 S/sleep S/sleep
2 RUNNING 0 0 0 0 34 34 33 0 0 0 33 2 33 32 0 S/sleep S/sleep
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 0 0 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
6 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
7 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
9 SLEEPING 0 0 1 1 30 30 30 0 0 0 30 3 29 29 0 S/sleep S/sleep
10 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 4 28 28 0 S/sleep S/sleep
11 SLEEPING 0 0 1 1 30 30 30 0 0 0 30 3 29 29 0 S/sleep S/sleep
12 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 4 28 28 0 S/sleep S/sleep
13 SLEEPING 0 0 1 1 30 30 30 0 0 0 30 3 29 29 0 S/sleep S/sleep
14 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 4 28 28 0 S/sleep S/sleep
15 ZOMBIE 0 0 1 1 25 25 25 0 0 0 24 3 24 23 0 S/sleep S/sleep
[mid2] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 1 1 5 5 5 0 0 0 5 1 4 4 0 S/sleep S/sleep
2 RUNNING 0 1 0 0 65 65 64 1 0 0 63 2 64 61 0 S/sleep S/sleep
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 0 0 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
6 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
7 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
9 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
10 SLEEPING 0 0 2 2 60 60 60 0 0 0 60 4 59 59 0 S/sleep S/sleep
11 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
12 SLEEPING 0 0 2 2 60 60 60 0 0 0 60 4 59 59 0 S/sleep S/sleep
13 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
14 SLEEPING 0 0 2 2 60 60 60 0 0 0 60 4 59 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 1 1 25 25 25 0 0 0 24 3 24 23 0 S/sleep S/sleep
[mid3] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 1 1 5 5 5 0 0 0 5 1 4 4 0 S/sleep S/sleep
2 RUNNING 0 2 0 0 67 67 66 2 0 0 64 2 66 62 0 S/sleep S/sleep
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 0 0 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
6 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
7 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
9 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
10 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
11 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
12 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
13 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
14 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 1 1 25 25 25 0 0 0 24 3 24 23 0 S/sleep S/sleep
[mid4] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 1 1 5 5 5 0 0 0 5 1 4 4 0 S/sleep S/sleep
2 RUNNING 0 3 0 0 69 69 68 3 0 0 65 2 68 63 0 S/sleep S/sleep
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
4 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
5 ZOMBIE 1 1 0 0 2 2 2 1 0 0 0 2 1 0 0 S/expire S/expire
6 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 3 1 0 0 S/expire S/expire
7 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 3 0 0 0 S/sleep S/sleep
8 ZOMBIE 0 0 2 2 1 1 1 0 0 0 0 4 0 0 0 S/sleep S/sleep
9 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
10 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
11 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
12 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
13 ZOMBIE 0 0 1 1 61 61 61 0 0 0 60 3 60 59 0 S/sleep S/sleep
14 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 4 60 59 0 S/sleep S/sleep
15 ZOMBIE 0 0 1 1 25 25 25 0 0 0 24 3 24 23 0 S/sleep S/sleep
test_workload_mix: waiting children... started=13
wait #0 -> 3
wait #1 -> 4
wait #2 -> 5
wait #3 -> 6
wait #4 -> 7
wait #5 -> 8
wait #6 -> 9
wait #7 -> 10
wait #8 -> 11
wait #9 -> 12
wait #10 -> 13
wait #11 -> 14
wait #12 -> 15
[end] entries=2
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first mkvP mkvH mkvB pred act
1 SLEEPING 0 0 1 1 5 5 5 0 0 0 5 1 4 4 0 S/sleep S/sleep
2 RUNNING 1 4 0 0 70 70 69 4 0 0 65 2 69 63 0 S/sleep S/expire
test_workload_mix: done

======== test sucess ========