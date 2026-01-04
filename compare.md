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

======== test start  ========

test_workload_mix: start (cpu=6 io=6 burst=1)
[mid1] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
2 RUNNING 0 0 1 1 33 33 32 0 0 0 32 1
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 1
4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 1
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2
6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2
7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
9 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 3
10 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 3
11 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 3
12 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 3
13 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 3
14 SLEEPING 0 0 2 2 29 29 29 0 0 0 29 3
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 3
[mid2] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
2 RUNNING 0 0 1 1 63 63 62 0 0 0 62 1
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 1
4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 1
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2
6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2
7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
9 SLEEPING 0 0 2 2 59 59 59 0 0 0 59 3
10 RUNNING 0 0 2 2 59 59 58 0 0 0 58 3
11 RUNNABLE 0 0 2 2 58 59 58 0 0 0 58 3
12 RUNNABLE 0 0 2 2 58 59 58 0 0 0 58 3
13 RUNNABLE 0 0 2 2 58 59 58 0 0 0 58 3
14 RUNNABLE 0 0 2 2 58 59 58 0 0 0 58 3
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 3
[mid3] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
2 RUNNING 0 0 1 1 64 64 63 0 0 0 63 1
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 1
4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 1
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2
6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2
7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
9 RUNNING 0 0 2 2 60 60 59 0 0 0 59 3
10 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 3
11 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 3
12 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 3
13 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 3
14 RUNNABLE 0 0 2 2 59 60 59 0 0 0 59 3
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 3
[mid4] entries=15
 pid state lvl cpu wait_sum wait_max run ready ctx preExp preHigh yield sleep first
1 SLEEPING 0 0 0 0 33 33 33 0 0 0 33 1
2 RUNNING 0 1 1 1 66 66 65 1 0 0 64 1
3 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 1
4 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 1
5 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2
6 ZOMBIE 0 0 1 1 1 1 1 0 0 0 0 2
7 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
8 ZOMBIE 1 1 1 1 2 2 2 1 0 0 0 2
9 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 3
10 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 3
11 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 3
12 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 3
13 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 3
14 ZOMBIE 0 0 2 2 61 61 61 0 0 0 60 3
15 ZOMBIE 0 0 2 2 25 25 25 0 0 0 24 3
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
2 RUNNING 0 1 1 1 66 66 65 1 0 0 64 1
test_workload_mix: done

======== test sucess ========



lab-11:


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