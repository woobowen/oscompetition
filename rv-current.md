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

## 当前后续瓶颈

前两个测试组之后，当前日志显示后续测试仍有独立缺口：

| 后续测试组 | 当前现象 | 初步方向 |
|---|---|---|
| cyclictest-musl | `unknown syscall 236`、`unknown syscall 199` | 236 多半是 `membarrier`，199 是 `socketpair`；需补调度/IPC 兼容 |
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
