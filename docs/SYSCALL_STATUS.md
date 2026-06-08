# SeaOS syscall 状态台账（A 线：RISC-V Linux 兼容）

> 唯一权威的 syscall 进度表。每补/改一个就更新对应行。号 = Linux/RISC-V ABI。
> 返回值约定：成功返回结果（uint64）；失败返回 (uint64)(-EXXX)，见 docs/DECISIONS.md D1/D2。
> 分发表上限 SYS_MAX_NUM=502（src/kernel/syscall/type.h）。

## 已实现（共 101 个分发表入口，含 3 个 SeaOS 私有入口）
| 号 | 名 | 备注 |
|---|---|---|
| 4 | fork | SeaOS |
| 17 | getcwd | Linux/RISC-V ABI；写出当前工作目录 |
| 23 | dup | |
| 24 | dup3 | 支持 `O_CLOEXEC` 标志 |
| 25 | fcntl | 支持 `F_DUPFD`、`F_DUPFD_CLOEXEC`、`F_GETFD`、`F_SETFD` |
| 29 | ioctl | 默认 `-ENOTTY`；RTC 设备支持 `RTC_RD_TIME` |
| 34 | mkdir(at) | |
| 35 | unlink(at) | |
| 37 | link(at) | |
| 38 | renameat | 同文件系统重命名；支持目录重命名 |
| 43 | statfs | 最小 Linux `struct statfs` |
| 44 | fstatfs | 最小 Linux `struct statfs` |
| 46 | ftruncate | 最小兼容：有效 fd 返回 0 |
| 48 | faccessat | 路径存在性/可读可执行检查 |
| 49 | chdir | |
| 56 | open(at) | |
| 57 | close | |
| 59 | pipe2 | 阻塞管道；支持 `O_CLOEXEC` |
| 61 | getdents64 | 输出 Linux `struct linux_dirent64` |
| 62 | lseek | |
| 63 | read | |
| 64 | write | |
| 65 | readv | |
| 66 | writev | |
| 71 | sendfile | |
| 72 | pselect6 | Linux fd_set copyin/copyout；socket 使用真实 readiness |
| 73 | ppoll | |
| 78 | readlinkat | `/proc/self/exe` 返回当前程序路径 |
| 79 | newfstatat | 按路径 stat |
| 80 | fstat | 输出 Linux `struct stat` |
| 81 | sync | 桩，返回 0 |
| 88 | utimensat | 最小时间戳更新/存在性检查 |
| 93 | exit | |
| 94 | exit_group | 单线程下等价 exit |
| 96 | set_tid_address | 返回 pid（D3 最小实现） |
| 98 | futex | 最小 WAIT/WAKE 语义 |
| 99 | set_robust_list | 桩返回 0 |
| 100 | get_robust_list | 桩返回 0 |
| 101 | nanosleep(兼容) | |
| 102 | getitimer | 桩，零填充返回 |
| 103 | setitimer | ITIMER_REAL → proc_t.itimer_expire/interval |
| 113 | clock_gettime | |
| 114 | clock_getres | 最小兼容分辨率返回 |
| 115 | clock_nanosleep | 支持相对睡眠与 TIMER_ABSTIME 绝对睡眠 |
| 116 | syslog | BusyBox `dmesg` 所需最小 klogctl |
| 118 | sched_setparam | 最小兼容 |
| 119 | sched_setscheduler | 桩返回 0 |
| 120 | sched_getscheduler | 最小兼容 |
| 121 | sched_getparam | 最小兼容 |
| 122 | sched_setaffinity | 单核兼容 |
| 123 | sched_getaffinity | 单核 mask bit0=1 |
| 124 | sched_yield | 调用 proc_yield |
| 129 | kill | 最小 pid/signal 校验；有效目标返回成功 |
| 133 | rt_sigsuspend | 最小让出 CPU |
| 134 | rt_sigaction | 读 musl sigaction，存 handler/restorer |
| 135 | rt_sigprocmask | 桩，返回 0 |
| 139 | rt_sigreturn | 从用户栈恢复 signal frame |
| 144 | setgid | 桩，返回 0 |
| 146 | setuid | 桩，返回 0 |
| 160 | uname | |
| 165 | getrusage | 零填充桩 |
| 166 | umask | 桩，返回 0 |
| 169 | gettimeofday | |
| 172 | getpid | |
| 173 | getppid | |
| 174 | getuid | 返回 0 (root) |
| 175 | geteuid | 返回 0 (root) |
| 176 | getgid | 返回 0 |
| 177 | getegid | 返回 0 |
| 178 | gettid | 单线程 = pid |
| 179 | sysinfo | 零填充 112B，uptime 填入 |
| 198 | socket | AF_INET loopback，支持 STREAM/DGRAM |
| 199 | socketpair | AF_UNIX/SOCK_STREAM 最小 pipe-like 兼容 |
| 200 | bind | loopback/any IPv4，端口 0 自动分配 |
| 201 | listen | TCP listener，维护 accept 队列 |
| 202 | accept | 阻塞等待 TCP 连接；信号待处理时返回 `-EINTR` |
| 203 | connect | TCP 建立本机 socket pair；UDP 记录默认 peer |
| 204 | getsockname | 返回本地 IPv4 sockaddr |
| 205 | getpeername | 返回 peer IPv4 sockaddr |
| 206 | sendto | stream 写 peer RX ring；UDP 投递数据报 |
| 207 | recvfrom | stream/UDP 接收，支持 EOF 与源地址返回 |
| 208 | setsockopt | netperf 所需 option no-op 兼容 |
| 209 | getsockopt | 返回稳定 SNDBUF/RCVBUF/TCP_MAXSEG 等值 |
| 210 | shutdown | socket 半关闭，唤醒阻塞端 |
| 211 | sendmsg | 明确返回 `-EOPNOTSUPP` |
| 212 | recvmsg | 明确返回 `-EOPNOTSUPP` |
| 214 | brk | |
| 215 | munmap | |
| 220 | clone | musl fork/pthread 依赖；按 flag 区分 parent_tid、child_tid 与 clear_child_tid |
| 221 | execve | 支持动态链接 ELF (D4) |
| 222 | mmap | len 自动 page 对齐 |
| 226 | mprotect | 最小权限更新：已有映射按 prot 调整 PTE_R/W/X |
| 228 | mlock | 最小兼容，返回 0 |
| 233 | madvise | 桩返回 0 |
| 236 | get_mempolicy | 最小 NUMA default node 0 兼容 |
| 242 | accept4 | accept + `SOCK_CLOEXEC` |
| 260 | wait4 | |
| 276 | renameat2 | 无 flags 时转 `renameat`，其他 flags 返回 `-EINVAL` |
| 500/501/502 | schedstat/spawn/shutdown | SeaOS 私有 |

## 近期状态更新（2026-06-06）

### 评测结果（unixbench-musl 与 busybox-musl 已通过）
```
Unixbench DHRY2 test(lps): 45818474
Unixbench WHETSTONE test(MFLOPS): 1148.887
Unixbench SYSCALL test(lps): 102425
Unixbench CONTEXT test(lps): 3052
Unixbench PIPE test(lps): 12801
Unixbench SHELL1 test(lpm): 1
Unixbench SHELL8 test(lpm): 1
Unixbench SHELL16 test(lpm): 1
#### OS COMP TEST GROUP END unixbench-musl ####
#### OS COMP TEST GROUP END busybox-musl ####
```

正式 docker 评测中：
- UnixBench 脚本定义的 27 个输出项全部出现，全部数值大于 0，组内无 `ERROR/unknown syscall/panic/fail`。
- `busybox_cmd.txt` 定义的 55 条命令全部输出 `success`，组内无 `fail/ERROR/unknown syscall/panic`。

### 已修复的关键缺陷
| 号 | 名 | 修复说明 |
|---|---|---|
| 134 | rt_sigaction | 缓冲区溢出修复：使用 `sigsetsize` 而非固定 152 字节 |
| 4,220 | fork/clone | epc+=4 ordering 修复：trap handler 前置处理，proc_fork 无需重复 +4 |
| 221 | execve | 补偿 epc 已包含 +4offset，entry_pc 无需二次加 |
| 17 | getcwd | 替换原 SeaOS 私有 `print_cwd` 行为，兼容 Linux `getcwd(buf, size)` |
| 38/276 | renameat/renameat2 | 支持 BusyBox `mv test_dir test` |
| 43/44 | statfs/fstatfs | 支持 BusyBox `df`/statvfs |
| 48 | faccessat | 支持 `which` 和 shell 路径检查 |
| 61 | getdents64 | 改为 Linux dirent64，兼容 `ls/find/ps` |
| 80 | fstat | 改为 Linux stat，兼容 BusyBox `stat` |
| 88 | utimensat | 支持 `touch` |
| 116 | syslog | 支持 BusyBox `dmesg` |
| 129 | kill | 支持 BusyBox `kill $!` 的最小语义 |
| — | procfs | 最小 in-memory procfs 支持 `/proc/mounts`、`/proc/meminfo`、`/proc/uptime`、`/proc/stat`、`/proc/self/*`、`/proc/<pid>/*` |
| — | RTC | `/dev/rtc`、`/dev/rtc0` 支持 `RTC_RD_TIME`，`hwclock` 通过 |
| — | FD_CLOEXEC | `openat/dup3/fcntl/pipe2/exec` 维护 close-on-exec |
| — | 评测超时 | `data/config.json` 设置 `qemu.timeout=3600` |
| — | boot printf | 剥离所有非必要 boot printf 节省 UART 时间 |

### ~~管道命令超时~~（已解决）
- **原因不是 pipe 实现有问题**，而是评测框架 `run_qemu.py` 默认 60s timeout 不够。
- `data/config.json` 设 `qemu.timeout=3600` 后，完整 UnixBench + BusyBox 有足够时间跑完。

### ~~当前瓶颈：procfs 缺失~~（已解决）
- `busybox df` 读取 `/proc/mounts` 已通过。
- `ps/free/uptime` 需要的 procfs 文本和目录扫描也已具备最小兼容输出。

### 已知缺口
- ~~musl 脚本通用崩溃（gp=0）~~ → 已修复（epc ordering）
- ~~glibc 脚本通用崩溃~~ → 已修复（epc ordering）
- ~~管道命令超时~~ → 已修复（评测 timeout 配置）
- ~~procfs 未实现：`/proc/mounts`、`/proc/self/` 等均不可访问~~ → 已修复到 BusyBox 所需最小面。
- 后续测试当前缺口（历史记录，2026-06-07 已更新）：
  - ~~`cyclictest-musl`：`unknown syscall 236`、`unknown syscall 199`~~ → 已推进补齐，当前阻塞为用户态 SEGV
  - ~~`netperf-musl`：`unknown syscall 198`~~ → 已通过，见下方 2026-06-07 netperf 状态更新
  - ~~`lmbench-musl`：`unknown syscall 72`~~ → `pselect6(72)` 已补齐；第五组 lmbench 已在 2026-06-08 状态更新中收敛到 `GROUP END` 与 36/36 解析

## 2026-06-07 历史记录：cyclictest 相关 syscall 已推进，阻塞曾改为 SEGV

本轮围绕第三项 `cyclictest-musl` 已接入/补充的 Linux/RISC-V ABI 面包括：

| 号 | 名 | 当前语义 |
|---|---|---|
| 46 | ftruncate | 最小兼容：有效 fd 返回 0 |
| 98 | futex | 最小 WAIT/WAKE 语义，用于 musl pthread/clone 线程等待 |
| 114 | clock_getres | 当前返回 `{0, 10000000}`；会触发 cyclictest high-res warning，后续可改为 `{0, 1}` 降噪 |
| 118 | sched_setparam | 最小兼容 |
| 119 | sched_setscheduler | 最小兼容，返回 0 |
| 120 | sched_getscheduler | 最小兼容 |
| 121 | sched_getparam | 最小兼容 |
| 122 | sched_setaffinity | 单核兼容 |
| 123 | sched_getaffinity | 单核 mask bit0=1 |
| 199 | socketpair | AF_UNIX/SOCK_STREAM 最小 pipe-like 兼容 |
| 226 | mprotect | 已从空桩改为 PTE 权限更新，支持 pthread TLS/栈变为可写 |
| 228 | mlock | 最小兼容，返回 0 |
| 236 | get_mempolicy | 最小 NUMA default node 0 兼容 |

当前正式 docker 日志确认：

```text
#### OS COMP TEST GROUP END unixbench-musl ####
======== test sucess ========
#### OS COMP TEST GROUP END busybox-musl ####
======== test sucess ========
#### OS COMP TEST GROUP START cyclictest-musl ####
...
[SEGV] pid=364 t=15 pc=0x000000000002f63c stval=0x0000003ffb031ff8
```

因此旧记录中的 `cyclictest-musl: unknown syscall 236/199` 已不是当前阻塞。当时新的缺口是 cyclictest 用户态 SEGV，后续已通过 `mprotect`、`clock_nanosleep(TIMER_ABSTIME)`、`clone child_tid` 等修复收敛；保留下列排查方向作为历史定位记录：

- `clone(CLONE_VM)` 后父子/线程页表共享是否覆盖 mmap、用户栈、TLS 和动态链接器映射；
- 文件/共享 `mmap` 的映射长度和边界是否覆盖 cyclictest 运行期访问；
- `clear_child_tid` + futex wake、线程退出路径是否破坏共享地址空间；
- `clock_getres` 可从 10ms 调整为 1ns 以消除 high-res warning，但这不是 SEGV 根因。

## 2026-06-07 状态更新：netperf-musl 已通过

本轮为第四项 `netperf-musl` 接入最小 AF_INET loopback socket 兼容层，并补齐 netperf 直接使用的 Linux/RISC-V syscall 面：

| 号 | 名 | 当前语义 |
|---|---|---|
| 72 | pselect6 | Linux fd_set copyin/copyout；socket 使用真实 readiness，非 socket fd 维持宽松兼容 |
| 198 | socket | 支持 `AF_INET`、`SOCK_STREAM`、`SOCK_DGRAM`，协议 `0/TCP/UDP` |
| 200 | bind | 支持 `127.0.0.1`、`0.0.0.0`，端口 0 自动分配 |
| 201 | listen | TCP listener 维护 accept 队列 |
| 202/242 | accept/accept4 | 阻塞等待连接，`accept4` 支持 `SOCK_CLOEXEC`；信号待处理时返回 `-EINTR` |
| 203 | connect | TCP 创建本机成对 connected socket；UDP 记录默认 peer |
| 204/205 | getsockname/getpeername | 返回本地/对端 IPv4 sockaddr |
| 206/207 | sendto/recvfrom | TCP 字节流与 UDP 数据报 loopback |
| 208/209 | setsockopt/getsockopt | netperf 所需 option no-op 或稳定整数返回 |
| 210 | shutdown | 支持 socket 半关闭和阻塞端唤醒 |
| 211/212 | sendmsg/recvmsg | 当前明确返回 `-EOPNOTSUPP`，实测 netperf 未进入该路径 |

正式 docker 串口日志确认前四项均到组尾：

```text
#### OS COMP TEST GROUP END unixbench-musl ####
======== test sucess ========
#### OS COMP TEST GROUP END busybox-musl ####
======== test sucess ========
#### OS COMP TEST GROUP END cyclictest-musl ####
======== test sucess ========
====== netperf UDP_STREAM end: success ======
====== netperf TCP_STREAM end: success ======
====== netperf UDP_RR end: success ======
====== netperf TCP_RR end: success ======
====== netperf TCP_CRR end: success ======
#### OS COMP TEST GROUP END netperf-musl ####
======== test sucess ========
```

语义边界：

- 只实现本机 loopback，不实现真实网卡、路由、IPv6、多播或 out-of-loopback 通信。
- socket pool 和缓冲区保持较小静态规模，避免回退 UnixBench/cyclictest 中的物理页压力。
- `SO_SNDBUF/SO_RCVBUF` 返回至少 `32000`，`TCP_MAXSEG` 返回 `1460`；其他 netperf 所需 option 多为 no-op。
- 历史说明：2026-06-07 netperf 收敛时第五项 `lmbench-musl` 尚未作为本轮通过标准；该状态已由下方 2026-06-08 lmbench 更新取代。

## 2026-06-08 status update: lmbench-musl reaches GROUP END and parses

The RISC-V musl target now passes the first five groups in the official docker run, and direct `judge_lmbench-musl.py` parsing reports 36/36 non-zero lmbench metric scores:

```text
#### OS COMP TEST GROUP END unixbench-musl ####
======== test sucess ========
#### OS COMP TEST GROUP END busybox-musl ####
======== test sucess ========
#### OS COMP TEST GROUP END cyclictest-musl ####
======== test sucess ========
#### OS COMP TEST GROUP END netperf-musl ####
======== test sucess ========
#### OS COMP TEST GROUP END lmbench-musl ####
======== test sucess ========
```

Current validation detail: the latest `os_serial_out_rv.txt` first-five group chunks contain no `ERROR:`, `unknown syscall`, `panic!`, `unexpected exception`, or `[SEGV]` markers. Direct lmbench judge parsing reports 36 items, 36 non-zero scores, and `score_sum=43.9642`.

Harness caveat: the fixed local docker command may still print a final JSON summary with `score: 0` and no parsed groups because `/cg/kernel.zip` resolves `testcase_dir` to `/coursegrader/testdata` and does not discover the mounted local `/cg/kernel/judge` scripts. This is a local parser-discovery artifact for that command, not evidence that lmbench failed. The authoritative evidence for this milestone is the generated serial log plus direct judge/parser checks.

New or updated syscall coverage for this milestone:

| No. | Name | Current semantics |
|---|---|---|
| 72 | pselect6 | fd_set copyin/copyout uses `(N_OPEN_FILE_PER_PROC + 63) / 64` words; socket and pipe readiness are checked by object state; regular files remain immediately ready. |
| 82 | fsync | Minimal compatibility: valid fd returns 0 because the current test filesystem has no per-fd flush state. |
| 83 | fdatasync | Same minimal compatibility as fsync. |
| 163 | getrlimit | Supports `RLIMIT_NOFILE`; returns the static per-process fd limit. |
| 164 | setrlimit | Accepts `RLIMIT_NOFILE` after validating the user pointer; does not resize the static fd table. |
| 227 | msync | Minimal compatibility: validates alignment/flags and returns 0 because current mmap has no file-backed dirty-page writeback. |
| 261 | prlimit64 | Supports current process `RLIMIT_NOFILE`; unsupported pid/resource combinations return normal errno. |

Resource note: `N_OPEN_FILE_PER_PROC` is 256. This is required for lmbench `lat_ctx ... 96`, which creates 96 pipes in one parent process and therefore needs at least 195 fd including stdin/stdout/stderr. The earlier 256-fd panic was traced to user physical-page exhaustion under the old 128M `ALLOC_END` linker limit plus unchecked `pmem_alloc(false)` in `uvm_copy_pgtbl`; `ALLOC_END` now matches the 1G QEMU RAM range and fork fails cleanly if page-copy allocation still fails.

Output note: `/dev/stderr` now writes bytes unchanged instead of prefixing every write with `ERROR: `. This restores normal Linux stderr semantics and lets lmbench's stderr metrics match the judge's baseline keys.
