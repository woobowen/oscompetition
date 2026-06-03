# SeaOS syscall 状态台账（A 线：RISC-V Linux 兼容）

> 唯一权威的 syscall 进度表。每补/改一个就更新对应行。号 = Linux/RISC-V ABI。
> 返回值约定：成功返回结果（uint64）；失败返回 (uint64)(-EXXX)，见 docs/DECISIONS.md D1/D2。
> 分发表上限 SYS_MAX_NUM=502（src/kernel/syscall/type.h）。

## 已实现
| 号 | 名 | 备注 |
|---|---|---|
| 4 | fork | SeaOS |
| 17 | print_cwd | SeaOS 私有 |
| 23 | dup | |
| 24 | dup3 | 忽略 flags (O_CLOEXEC) |
| 25 | fcntl | 最小实现 |
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
| 79 | newfstatat | 按路径 stat |
| 80 | fstat | |
| 93 | exit | |
| 94 | exit_group | 单线程下等价 exit |
| 96 | set_tid_address | 返回 pid（D3 最小实现） |
| 101 | nanosleep(兼容) | |
| 113 | clock_gettime | |
| 134 | rt_sigaction | 桩，返回 0 |
| 135 | rt_sigprocmask | 桩，返回 0 |
| 144 | setgid | 桩，返回 0 |
| 146 | setuid | 桩，返回 0 |
| 160 | uname | |
| 165 | getrusage | 零填充桩 |
| 166 | umask | 桩，返回 0 |
| 169 | gettimeofday | |
| 172 | getpid | |
| 173 | getppid | |
| 174 | getuid | 返回 0 (root) |
| 176 | getgid | 返回 0 |
| 178 | gettid | 单线程 = pid |
| 214 | brk | |
| 215 | munmap | |
| 220 | clone | musl fork 依赖 |
| 221 | execve | 支持动态链接 ELF (D4) |
| 222 | mmap | |
| 226 | mprotect | 桩，返回 0 |
| 260 | wait4 | |
| 500/501/502 | schedstat/spawn/shutdown | SeaOS 私有 |

## 进行中 / 下一个
| 号 | 名 | 状态 | 计划 |
|---|---|---|---|
| 99  | set_robust_list | 已实现 | 桩返回 0 |
| 100 | get_robust_list | 已实现 | 桩返回 0 |
| 102 | getitimer | 已实现 | 桩，零填充返回 |
| 103 | setitimer | 已实现 | ITIMER_REAL 写 proc_t.itimer_expire/interval |
| 115 | clock_nanosleep | 已实现 | 按 request 睡眠 |
| 123 | sched_getaffinity | 已实现 | 单核 mask bit0=1 |
| 124 | sched_yield | 已实现 | 调用 proc_yield |
| 134 | rt_sigaction | 已实现 | 读 musl sigaction(152B)，存 handler/restorer |
| 139 | rt_sigreturn | 已实现 | 从用户栈恢复 256B signal frame，清 sig_delivering |
| 175 | geteuid | 已实现 | 返回 0 (root) |
| 177 | getegid | 已实现 | 返回 0 |
| 179 | sysinfo | 已实现 | 零填充 112B，uptime 填入 |
| 233 | madvise | 已实现 | 桩返回 0 |
| 78  | readlinkat | 已实现 | 桩返回 -EINVAL |
| 29  | ioctl | 已实现 | 桩返回 -ENOTTY |

## 进行中 / 下一个
| 号 | 名 | 状态 | 计划 |
|---|---|---|---|
| 222 | mmap | len 对齐修复待验证 | 非页对齐 len 自动 round-up（已改代码，未重跑评测）|

## 已知缺口链（2026-06-04 评测结果）
- **musl 脚本通用崩溃**：`[SEGV] trap_id=15 sepc=0x1048a8 stval=0xfffffffffffff908`
  - 根因：mmap 拒绝非对齐 len → musl tp=-1 → TLS 访问无效地址
  - 修复代码已写入 sys_mmap，待重跑评测确认
- **glibc 脚本通用崩溃**：`[SEGV] trap_id=13 sepc=0xd10b0 stval=0xfffffffffffffeb8`
  - 类似 TLS 初始化问题，mmap 修复后可能同步解决
- **多个 .sh 脚本 ELF 头解析失败**：文件内容为纯文本 shell 脚本，需要 shebang 解析或通过 shell 解释器执行
  - cyclictest/netperf/iperf/libcbench/libctest/iozone/lua/basic 等均受影响
