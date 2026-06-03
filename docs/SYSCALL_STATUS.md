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
| 99 | set_robust_list | 待实现 | 桩返回 0，musl 线程初始化调用 |

## 已知缺口链
> 跑 dhry2reg 评测暴露的缺口：
- syscall 99 (set_robust_list)：musl 线程初始化调用，阻塞 dhry2reg 继续执行
- 后续缺口待补 set_robust_list 后再次评测暴露
