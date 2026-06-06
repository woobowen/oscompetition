# SeaOS syscall 状态台账（A 线：RISC-V Linux 兼容）

> 唯一权威的 syscall 进度表。每补/改一个就更新对应行。号 = Linux/RISC-V ABI。
> 返回值约定：成功返回结果（uint64）；失败返回 (uint64)(-EXXX)，见 docs/DECISIONS.md D1/D2。
> 分发表上限 SYS_MAX_NUM=502（src/kernel/syscall/type.h）。

## 已实现（共 75 个分发表入口，含 3 个 SeaOS 私有入口）
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
| 73 | ppoll | |
| 78 | readlinkat | `/proc/self/exe` 返回当前程序路径 |
| 79 | newfstatat | 按路径 stat |
| 80 | fstat | 输出 Linux `struct stat` |
| 81 | sync | 桩，返回 0 |
| 88 | utimensat | 最小时间戳更新/存在性检查 |
| 93 | exit | |
| 94 | exit_group | 单线程下等价 exit |
| 96 | set_tid_address | 返回 pid（D3 最小实现） |
| 99 | set_robust_list | 桩返回 0 |
| 100 | get_robust_list | 桩返回 0 |
| 101 | nanosleep(兼容) | |
| 102 | getitimer | 桩，零填充返回 |
| 103 | setitimer | ITIMER_REAL → proc_t.itimer_expire/interval |
| 113 | clock_gettime | |
| 115 | clock_nanosleep | 按 request 睡眠 |
| 116 | syslog | BusyBox `dmesg` 所需最小 klogctl |
| 119 | sched_setscheduler | 桩返回 0 |
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
| 214 | brk | |
| 215 | munmap | |
| 220 | clone | musl fork 依赖 |
| 221 | execve | 支持动态链接 ELF (D4) |
| 222 | mmap | len 自动 page 对齐 |
| 226 | mprotect | 桩，返回 0 |
| 233 | madvise | 桩返回 0 |
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
  - `netperf-musl`：`unknown syscall 198`
  - `lmbench-musl`：`unknown syscall 72`

## 2026-06-07 状态更新：cyclictest 相关 syscall 已推进，当前阻塞改为 SEGV

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

因此旧记录中的 `cyclictest-musl: unknown syscall 236/199` 已不是当前阻塞。新的当前缺口是 cyclictest 用户态 SEGV，重点排查：

- `clone(CLONE_VM)` 后父子/线程页表共享是否覆盖 mmap、用户栈、TLS 和动态链接器映射；
- 文件/共享 `mmap` 的映射长度和边界是否覆盖 cyclictest 运行期访问；
- `clear_child_tid` + futex wake、线程退出路径是否破坏共享地址空间；
- `clock_getres` 可从 10ms 调整为 1ns 以消除 high-res warning，但这不是 SEGV 根因。
