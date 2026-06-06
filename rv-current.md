# SeaOS RISC-V 当前状态（2026-06-06 最新）

## 总体进度

**unixbench-musl 与 busybox-musl 已在正式 docker 评测命令中通过。** 当前重点已经从前两个测试组转到后续 `cyclictest-musl`、`netperf-musl`、`lmbench-musl` 的 syscall/网络/调度兼容缺口。

本轮验证命令：

```bash
docker run --rm \
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" \
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" \
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```

## 2026-06-07 最新状态：前两项保持通过，cyclictest 阻塞在用户态 SEGV

本轮正式 docker 命令运行后，根目录 `os_serial_out_rv.txt` 已正常产生内容，不是空日志状态。此前看到“没有输出/文件缺失”的高风险现象，主要与评测进程尚未结束、QEMU/日志文件仍被占用、以及根目录残留 `sdcard-*.img` 可能干扰解压有关；当前复现中串口日志已落到 `os_serial_out_rv.txt`。

当前 RISC-V 评测日志显示：

| 测试组 | 当前结论 | 证据 |
|---|---|---|
| unixbench-musl | 通过 | `#### OS COMP TEST GROUP END unixbench-musl ####` 后出现 `======== test sucess ========` |
| busybox-musl | 通过 | `#### OS COMP TEST GROUP END busybox-musl ####` 后出现 `======== test sucess ========` |
| cyclictest-musl | 未通过子项，但评测脚本组本身继续往后跑 | 四个子项均出现 `[SEGV] ... pc=0x000000000002f63c stval=0x0000003ffb031ff8` |

cyclictest 当前失败日志：

```text
====== cyclictest NO_STRESS_P1 begin ======
ERROR: WARN: stat /dev/cpu_dma_latency failedERROR: : No such file or directory
ERROR: WARN: ERROR: High resolution timers not available
[SEGV] pid=364 t=15 pc=0x000000000002f63c stval=0x0000003ffb031ff8 ...
====== cyclictest NO_STRESS_P1 end: fail ======
```

同样的 SEGV 出现在 `NO_STRESS_P8`、`STRESS_P1`、`STRESS_P8`。因此当前 cyclictest 的主阻塞不是 syscall 236/199 缺失，而是用户态地址 `0x0000003ffb031ff8` 访问失败，下一步应优先排查 `clone(CLONE_VM)` 线程页表共享、mmap 文件/共享映射、TLS/线程栈相关映射范围。

两个 cyclictest warning 的判断：

- `/dev/cpu_dma_latency`：`testsuits-for-oskernel/rt-tests-2.7/src/cyclictest/cyclictest.c` 中注释为 `use the /dev/cpu_dma_latency trick if it's there`，`stat` 失败后只打印 `WARN` 并 `return` 到主流程；这是 Linux PM QoS 低延迟优化接口，缺失会产生 warning，但不是当前 fail 的直接原因。
- `High resolution timers not available`：`check_timer()` 要求 `clock_getres(CLOCK_MONOTONIC)` 返回 `{0, 1}`；当前 `sys_clock_getres()` 返回 `{0, 10000000}`，所以触发 warning。该 warning 可通过把最小兼容返回值改为 1ns 消除，但当前实际终止 cyclictest 子项的是后续 SEGV。

后续测试补充观察：

| 后续测试组 | 当前现象 | 说明 |
|---|---|---|
| netperf-musl | `unknown syscall 198`，`getaddrinfo returned -11` | 198 为 socket，属于第四项之后的网络兼容缺口，不影响本轮前三项目标判断 |
| lmbench-musl | 已进入 `latency measurements` | 本轮未作为优先目标 |

公共文件风险提示：本轮为 cyclictest 已改动/接入 `src/kernel/syscall/type.h`、`src/kernel/syscall/syscall.c`、`src/kernel/syscall/sysfunc.c`、`src/kernel/proc/type.h`、`src/kernel/proc/proc.c`、`src/kernel/mem/uvm.c`、`src/kernel/fs/fs.c` 等公共路径。后续修 SEGV 时必须继续用正式 docker 命令确认 unixbench-musl 与 busybox-musl 不回退。

评测框架输出 `verdict: Accpted`；编译段显示 `make all` 成功，`kernel-rv` 和 `kernel-la` 均生成。`data/config.json` 当前设置 `"qemu.timeout": 3600`，没有超过一小时上限；docker 外层总耗时会因为镜像拷贝和收尾略大于 QEMU timeout。

## 最新评测输出摘要

`os_serial_out_rv.txt` 中前两个测试组的机器化校验结果：

| 测试组 | 期望项 | 实际成功项 | 失败/错误 |
|---|---:|---:|---:|
| unixbench-musl | 27 个 UnixBench 输出项 | 27 个，全部数值 > 0 | 0 个 `ERROR/unknown syscall/panic/fail` |
| busybox-musl | 55 条 `busybox_cmd.txt` 命令 | 55 条 `success` | 0 个 `fail/ERROR/unknown syscall/panic` |

关键输出：

```text
#### OS COMP TEST GROUP START unixbench-musl ####
Unixbench DHRY2 test(lps): 45818474
Unixbench WHETSTONE test(MFLOPS): 1148.887
Unixbench SYSCALL test(lps): 102425
Unixbench CONTEXT test(lps): 3052
Unixbench PIPE test(lps): 12801
Unixbench SPAWN test(lps): 3
Unixbench EXECL test(lps): 39
Unixbench FS_WRITE_SMALL test(KBps): 23571
Unixbench FS_READ_SMALL test(KBps): 20280
Unixbench FS_COPY_SMALL test(KBps): 10556
Unixbench FS_WRITE_MIDDLE test(KBps): 91868
Unixbench FS_READ_MIDDLE test(KBps): 67958
Unixbench FS_COPY_MIDDLE test(KBps): 38879
Unixbench FS_WRITE_BIG test(KBps): 376362
Unixbench FS_READ_BIG test(KBps): 172742
Unixbench FS_COPY_BIG test(KBps): 120709
Unixbench SHELL1 test(lpm): 1
Unixbench SHELL8 test(lpm): 1
Unixbench SHELL16 test(lpm): 1
Unixbench ARITHOH test(lps): 3855689386
Unixbench SHORT test(lps): 76725370
Unixbench INT test(lps): 79704462
Unixbench LONG test(lps): 76740219
Unixbench FLOAT test(lps): 72443615
Unixbench DOUBLE test(lps): 79361832
Unixbench HANOI test(lps): 310404
Unixbench EXEC test(lps): 3
#### OS COMP TEST GROUP END unixbench-musl ####

#### OS COMP TEST GROUP START busybox-musl ####
...
testcase busybox df success
testcase busybox dmesg success
testcase busybox ps success
testcase busybox free success
testcase busybox hwclock success
...
testcase busybox find -name "busybox_cmd.txt" success
#### OS COMP TEST GROUP END busybox-musl ####
```

## 本轮关键修复

| 修复 | 主要文件 | 效果 |
|---|---|---|
| 最小 in-memory procfs | `src/kernel/fs/fs.c`, `src/kernel/fs/type.h`, `src/kernel/fs/method.h` | 支持 `/proc/mounts`、`/proc/meminfo`、`/proc/uptime`、`/proc/stat`、`/proc/self/*`、`/proc/<pid>/*`，使 `df/free/ps` 通过 |
| Linux `getdents64` 与 `stat` 输出 | `src/kernel/fs/fs.c`, `src/kernel/syscall/sysfunc.c` | 用户态目录项改为 Linux `struct linux_dirent64`，`fstat` 输出 Linux stat，兼容 BusyBox `ls/find/stat/ps` |
| busybox 所需 syscall | `src/kernel/syscall/type.h`, `src/kernel/syscall/syscall.c`, `src/kernel/syscall/sysfunc.c` | 补齐 `getcwd`、`renameat`、`statfs/fstatfs`、`faccessat`、`utimensat`、`syslog`、`kill`、`renameat2` 等最小实现 |
| `/dev/rtc` / `/dev/rtc0` | `src/kernel/fs/device.c`, `src/kernel/syscall/sysfunc.c` | `ioctl(RTC_RD_TIME)` 返回有效 RTC 时间，`hwclock` 通过 |
| FD_CLOEXEC 兼容 | `src/kernel/proc/type.h`, `src/kernel/proc/exec.c`, `src/kernel/syscall/sysfunc.c` | `openat/dup3/fcntl/pipe2/exec` 维护 close-on-exec，降低 shell 管道和子进程 fd 泄漏风险 |
| UnixBench shell 稳定性 | `src/kernel/proc/exec.c`, `src/kernel/syscall/sysfunc.c`, `src/kernel/proc/proc.c` | `looper 20 ./multi.sh 1/8/16` 能真实完成一次迭代并输出非零结果 |
| 容量与健壮性 | `src/kernel/proc/type.h`, `src/kernel/mem/type.h`, `src/kernel/fs/type.h` | 提升 `N_PROC`、`KERN_PAGES`、`N_FILE/N_PIPE/PIPE_SIZE`，避免 UnixBench/BusyBox 组合压力下资源提前耗尽 |

公共文件风险说明：本轮改动涉及 `src/kernel/syscall/type.h`、`src/kernel/syscall/syscall.c`、`src/kernel/syscall/sysfunc.c`、`src/kernel/proc/type.h`、`src/kernel/fs/type.h`、`src/kernel/mem/type.h` 等公共兼容/资源文件。风险是 syscall 表、Linux ABI 输出或资源上限回归；缓解方式是保持 RISC-V ABI 号不重排、不改 LoongArch 行为，并用正式 docker 命令完整重跑确认前两个测试组无隐藏失败。

## 当前后续瓶颈（历史记录，已被 2026-06-07 最新状态取代）

前两个测试组之后，旧日志曾显示后续测试仍有独立缺口。注意：`cyclictest-musl` 的 `unknown syscall 236/199` 已在 2026-06-07 前推进补齐，当前阻塞已变为用户态 SEGV，见本文顶部最新状态。

| 后续测试组 | 当前现象 | 初步方向 |
|---|---|---|
| cyclictest-musl | ~~`unknown syscall 236`、`unknown syscall 199`~~ | 已取代：当前为 `[SEGV] pc=0x2f63c stval=0x3ffb031ff8` |
| netperf-musl | `unknown syscall 198`，`getaddrinfo returned -11` | 198 是 socket；需补最小网络 syscall/loopback 语义 |
| lmbench-musl | `unknown syscall 72` | 72 是 `pselect6`；需补 select/poll 兼容 |

这些错误发生在 `busybox-musl` 结束之后，不影响当前已完成的 UnixBench + BusyBox 验收。

## 评测命令

```bash
# 构建
docker run --rm -v "E:\code\2_3_os\oskernel2026-seaos:/src" zhouzhouyi/os-contest:20260510 bash -c "cd /src && make all 2>&1"

# 完整评测
docker run --rm \
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" \
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" \
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```
