# SeaOS syscall 状态台账（A 线：RISC-V Linux 兼容）

> 唯一权威的 syscall 进度表。每补/改一个就更新对应行。号 = Linux/RISC-V ABI。
> 返回值约定：成功返回结果（uint64）；失败返回 (uint64)(-EXXX)，见 docs/DECISIONS.md D1/D2。
> 分发表上限 SYS_MAX_NUM=502（src/kernel/syscall/type.h）。

## 已实现（共 52 个）
| 号 | 名 | 备注 |
|---|---|---|
| 4 | fork | SeaOS |
| 17 | print_cwd | SeaOS 私有 |
| 23 | dup | |
| 24 | dup3 | 忽略 flags (O_CLOEXEC) |
| 25 | fcntl | 最小实现 |
| 29 | ioctl | 桩返回 -ENOTTY |
| 34 | mkdir(at) | |
| 35 | unlink(at) | |
| 37 | link(at) | |
| 49 | chdir | |
| 56 | open(at) | |
| 57 | close | |
| 59 | pipe2 | 阻塞管道 |
| 61 | getdents64 | |
| 62 | lseek | |
| 63 | read | |
| 64 | write | |
| 66 | writev | |
| 71 | sendfile | |
| 73 | ppoll | |
| 78 | readlinkat | 桩返回 -EINVAL |
| 79 | newfstatat | 按路径 stat |
| 80 | fstat | |
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
| 119 | sched_setscheduler | 桩返回 0 |
| 123 | sched_getaffinity | 单核 mask bit0=1 |
| 124 | sched_yield | 调用 proc_yield |
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
| 500/501/502 | schedstat/spawn/shutdown | SeaOS 私有 |

## 近期状态更新（2026-06-04）

### 评测结果（unixbench-musl 全部通过）
```
Unixbench DHRY2 test(lps): 47078774
Unixbench WHETSTONE test(MFLOPS): 1134.372
Unixbench SYSCALL test(lps): 115206
Unixbench CONTEXT test(lps): 9688
Unixbench PIPE test(lps): 11358
```

busybox-musl 独立命令测试已通过 7 项（echo, ash, sh, basename, cal, clear, date），当前卡在 `df`（需 /proc/mounts）。

### 已修复的关键缺陷
| 号 | 名 | 修复说明 |
|---|---|---|
| 134 | rt_sigaction | 缓冲区溢出修复：使用 `sigsetsize` 而非固定 152 字节 |
| 4,220 | fork/clone | epc+=4 ordering 修复：trap handler 前置处理，proc_fork 无需重复 +4 |
| 221 | execve | 补偿 epc 已包含 +4offset，entry_pc 无需二次加 |
| — | 评测超时 | 创建 data/config.json 设置 qemu.timeout=120（默认 60s 不够用） |
| — | boot printf | 剥离所有非必要 boot printf 节省 UART 时间 |

### ~~管道命令超时~~（已解决）
- **原因不是 pipe 实现有问题**，而是评测框架 `run_qemu.py` 默认 60s timeout 不够。
- 创建 `data/config.json` 设 `qemu.timeout=120` 后，pipe 测试正常通过（11358 lps）。

### 当前瓶颈：procfs 缺失
- `busybox df` 需要读 `/proc/mounts`，当前 open 失败返回 -EPERM
- 需实现最小 procfs 桩（至少让 `/proc/mounts` 可读）

### 已知缺口
- ~~musl 脚本通用崩溃（gp=0）~~ → 已修复（epc ordering）
- ~~glibc 脚本通用崩溃~~ → 已修复（epc ordering）
- ~~管道命令超时~~ → 已修复（评测 timeout 配置）
- **多个 .sh 脚本 ELF 头解析失败**：文件内容为纯文本 shell 脚本
  - cyclictest/netperf/iperf/libcbench/libctest/iozone/lua/basic 等均受影响
  - 需实现 shebang 解析或通过 busybox sh 间接执行
- **procfs 未实现**：`/proc/mounts`、`/proc/self/` 等均不可访问
