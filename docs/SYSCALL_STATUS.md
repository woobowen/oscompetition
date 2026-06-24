# SeaOS syscall 状态台账（A 线：RISC-V Linux 兼容）

> 唯一权威的 syscall 进度表。每补/改一个就更新对应行。号 = Linux/RISC-V ABI。
> 返回值约定：成功返回结果（uint64）；失败返回 (uint64)(-EXXX)，见 docs/DECISIONS.md D1/D2。
> 分发表上限 SYS_MAX_NUM=502（src/kernel/syscall/type.h）。

## 已实现（共 135 个分发表入口，含 3 个 SeaOS 私有入口）
| 号 | 名 | 备注 |
|---|---|---|
| 4 | fork | SeaOS |
| 17 | getcwd | Linux/RISC-V ABI；写出当前工作目录 |
| 39 | umount2 | 最小兼容卸载入口；当前镜像路径返回成功边界 |
| 40 | mount | 最小兼容挂载入口；记录 memfs 只读 remount 边界 |
| 23 | dup | |
| 24 | dup3 | 支持 `O_CLOEXEC` 标志 |
| 25 | fcntl | 支持 `F_DUPFD`、`F_DUPFD_CLOEXEC`、`F_GETFD`、`F_SETFD` |
| 29 | ioctl | 默认 `-ENOTTY`；RTC 设备支持 `RTC_RD_TIME` |
| 34 | mkdir(at) | |
| 35 | unlink(at) | |
| 36 | symlinkat | memfs 符号链接；支持 access/open 跟随、readlinkat 读取和 ELOOP 环检测 |
| 37 | link(at) | |
| 38 | renameat | 同文件系统重命名；支持目录重命名 |
| 43 | statfs | 最小 Linux `struct statfs` |
| 44 | fstatfs | 最小 Linux `struct statfs` |
| 46 | ftruncate | 最小兼容：有效 fd 返回 0 |
| 48 | faccessat | 路径存在性/权限检查；memfs 支持 uid/gid/mode、symlink 跟随和 EROFS/ELOOP/ENOTDIR |
| 53 | fchmodat | memfs 更新 mode；其他有效路径保持最小成功边界 |
| 54 | fchownat | memfs 更新 uid/gid；其他有效路径保持最小成功边界 |
| 49 | chdir | |
| 56 | open(at) | 支持 Linux `O_PATH` 为 closeable path-only fd |
| 57 | close | |
| 59 | pipe2 | 阻塞管道；支持 `O_CLOEXEC` |
| 61 | getdents64 | 输出 Linux `struct linux_dirent64` |
| 62 | lseek | |
| 63 | read | |
| 64 | write | |
| 65 | readv | |
| 66 | writev | |
| 67 | pread64 | 按给定 offset 读取并恢复 fd offset |
| 68 | pwrite64 | 按给定 offset 写入并恢复 fd offset |
| 71 | sendfile | |
| 72 | pselect6 | Linux fd_set copyin/copyout；socket 使用真实 readiness |
| 73 | ppoll | |
| 78 | readlinkat | memfs symlink 读取；`/proc/self/exe` 返回当前程序路径 |
| 79 | newfstatat | 按路径 stat；已对已知 BusyBox applet 名提供 bounded stat 兼容 |
| 80 | fstat | 输出 Linux `struct stat` |
| 81 | sync | 桩，返回 0 |
| 82 | fsync | 最小兼容：有效 fd 返回 0 |
| 83 | fdatasync | 最小兼容：有效 fd 返回 0 |
| 88 | utimensat | 最小时间戳更新/存在性检查；兼容 `futimens(fd, NULL pathname)` |
| 89 | acct | 已注册；进程 accounting 未配置，返回 `-ENOSYS` 供 LTP 正确 TCONF |
| 93 | exit | |
| 94 | exit_group | 单进程等价 `exit`；`CLONE_THREAD`/`CLONE_VM` 组内 sibling 通过 pending self-exit 退出 |
| 96 | set_tid_address | 返回 pid（D3 最小实现） |
| 98 | futex | 最小 WAIT/WAKE/WAIT_BITSET；支持粗粒度 timeout，超时返回 `-ETIMEDOUT` |
| 99 | set_robust_list | 记录每线程 robust futex list head/len，退出时用于 owner-death 标记 |
| 100 | get_robust_list | 返回当前或指定 pid 的 robust futex list head/len |
| 101 | nanosleep(兼容) | |
| 102 | getitimer | ITIMER_REAL current/interval snapshot；暂不支持 VIRTUAL/PROF |
| 103 | setitimer | ITIMER_REAL → proc_t.itimer_expire/interval；old_value 返回兼容 alarm 的剩余秒数 |
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
| 129 | kill | 最小 pid/signal 校验；记录 `SI_USER`/sender pid |
| 130 | tkill | 最小线程 signal 兼容；记录 `SI_TKILL`/sender tgid |
| 131 | tgkill | 最小线程组 signal 兼容；记录 `SI_TKILL`/sender tgid |
| 133 | rt_sigsuspend | 最小让出 CPU |
| 134 | rt_sigaction | 读 musl sigaction，存 handler/restorer |
| 135 | rt_sigprocmask | 桩，返回 0 |
| 137 | rt_sigtimedwait | 最小 pending-signal 等待；返回已记录的 `si_code`/`si_pid` |
| 139 | rt_sigreturn | 从用户栈恢复 signal frame |
| 143 | setregid | 最小 real/effective GID 状态；root 可切换，非 root 仅可保留已有 id |
| 144 | setgid | 最小 real/effective GID 状态 |
| 145 | setreuid | 最小 real/effective UID 状态；root 可切换，非 root 仅可保留已有 id |
| 146 | setuid | 最小 real/effective UID 状态 |
| 147 | setresuid | 最小 real/effective UID 状态；saved uid 不建模 |
| 149 | setresgid | 最小 real/effective GID 状态；saved gid 不建模 |
| 153 | times | 返回进程时间结构，供基础测试读取 |
| 154 | setpgid | 最小进程组兼容入口 |
| 157 | setsid | 返回调用进程 pid 作为最小 session id |
| 160 | uname | |
| 163 | getrlimit | 支持 `RLIMIT_NOFILE` |
| 164 | setrlimit | 支持 `RLIMIT_NOFILE`；验证用户指针但不改变静态 fd 表 |
| 165 | getrusage | 零填充桩 |
| 166 | umask | 桩，返回 0 |
| 169 | gettimeofday | |
| 171 | adjtimex | 最小 `timex` 查询/校验：返回 `TIME_OK`，无真实调时 |
| 172 | getpid | `CLONE_THREAD` 成员返回 thread-group leader pid |
| 173 | getppid | |
| 174 | getuid | 返回当前最小 credential uid |
| 175 | geteuid | 返回当前最小 credential euid |
| 176 | getgid | 返回当前最小 credential gid |
| 177 | getegid | 返回当前最小 credential egid |
| 178 | gettid | 返回当前线程 id；单线程 = pid |
| 179 | sysinfo | 零填充 112B，uptime 填入 |
| 194 | shmget | SysV SHM 最小段分配，供 glibc/ltp 探测 |
| 195 | shmctl | SysV SHM `IPC_STAT`/`IPC_RMID` 最小兼容 |
| 196 | shmat | SysV SHM 映射到用户地址空间 |
| 197 | shmdt | SysV SHM detach，清理 attach 记录 |
| 198 | socket | AF_INET loopback，支持 STREAM/DGRAM |
| 199 | socketpair | AF_UNIX/SOCK_STREAM 最小 pipe-like 兼容 |
| 200 | bind | loopback/any IPv4，端口 0 自动分配 |
| 201 | listen | TCP listener，维护 accept 队列 |
| 202 | accept | 阻塞等待 TCP 连接；信号待处理时返回 `-EINTR`；UDP 返回 `-EOPNOTSUPP`，`O_PATH` fd 返回 `-EBADF` |
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
| 214 | brk | `CLONE_VM` heap grow 同步 live sibling 页表和 `heap_top`；shrink 仍是最小当前线程语义 |
| 215 | munmap | `addr` 必须页对齐；`len` 按 Linux 语义向上页对齐；`CLONE_VM` live siblings 同步清 PTE 并单次释放 PA |
| 216 | mremap | 最小兼容；收缩/同尺寸返回原地址，增长返回 `-ENOMEM` |
| 217 | add_key | 已注册；Linux key retention service 未支持，返回 `-ENOSYS` |
| 219 | keyctl | 已注册；Linux key retention service 未支持，返回 `-ENOSYS` |
| 220 | clone | musl fork/pthread 依赖；按 flag 区分 parent_tid、child_tid 与 clear_child_tid |
| 221 | execve | 支持动态链接 ELF (D4) |
| 222 | mmap | len 自动 page 对齐；lazy fault 在 `CLONE_VM` live siblings 间复用/同步同 VA 的 PA |
| 226 | mprotect | 最小权限更新：已有映射按 prot 调整 PTE_R/W/X |
| 227 | msync | 最小兼容：校验参数后返回 0 |
| 228 | mlock | 最小兼容，返回 0 |
| 233 | madvise | 桩返回 0 |
| 236 | get_mempolicy | 最小 NUMA default node 0 兼容 |
| 242 | accept4 | accept + `SOCK_CLOEXEC`；沿用 accept errno 语义 |
| 260 | wait4 | Returns `-ECHILD` when no matching child exists; supports minimal `SA_RESTART` restart after signal handlers by restoring the original syscall arguments. |
| 261 | prlimit64 | 支持当前进程 `RLIMIT_NOFILE` |
| 276 | renameat2 | 无 flags 时转 `renameat`，其他 flags 返回 `-EINVAL` |
| 278 | getrandom | 非阻塞伪随机字节，支持 Linux flags 子集 |
| 283 | membarrier | 单核最小兼容；支持 query 和 no-op barrier |
| 500/501/502 | schedstat/spawn/shutdown | SeaOS 私有 |

## 2026-06-24 状态更新：RISC-V LTP access/passwd/symlink 兼容

本轮为 LTP early access/adjtimex 路径补齐最小 Linux/RISC-V 兼容面：

| 号/模块 | 名 | 当前语义/修正 |
|---|---|---|
| 36 | symlinkat | 在 memfs 上创建符号链接；`access/open/stat` 默认跟随最终链接，最多 8 跳，环返回 `-ELOOP`。 |
| 39/40 | umount2/mount | 记录 memfs 只读 remount，`access(W_OK)` 对只读挂载点返回 `-EROFS`。 |
| 48 | faccessat | 校验非法 mode、空/过长/坏指针路径；memfs mode/uid/gid 权限返回 `EACCES/ENOTDIR/ELOOP/EROFS` 等真实 errno。 |
| 53/54 | fchmodat/fchownat | 对 memfs 文件更新 mode/uid/gid，供 LTP setup 后的 `access()` 权限检查使用。 |
| 78 | readlinkat | 读取 memfs symlink target；保留 `/proc/self/exe` 特例。 |
| 143/145/147/149 | set*id | 补最小 real/effective uid/gid 状态，使 root/nobody 场景不再全都表现为 root。 |
| 174-177 | get*id | 返回当前最小 credential 字段。 |
| memfs | `/etc/passwd`/`/etc/group` | 只读提供 root/nobody/nogroup 文本，供 libc `getpwnam("nobody")` 解析。 |

语义边界：这不是完整 VFS symlink、Linux mount namespace、saved-id/capability 模型或用户数据库。符号链接当前只覆盖 memfs 节点；只读 remount 只记录当前 LTP 需要的 memfs mount point；credential 只建模 real/effective uid/gid。

验证：`make all` 在固定 docker 构建环境通过；固定 RV docker 于 2026-06-24 19:05:58 Asia/Shanghai 到达 `sys_shutdown` line 5268。最新 `ltp-musl` 中 `unknown syscall 36` 消失，`access02` 不再因 `symlink(...)=ENOSYS` TBROK，`access04` 的 `EINVAL/ENOENT/ENAMETOOLONG/ENOTDIR/ELOOP/EROFS` root+nobody 检查均 TPASS，`adjtimex02` 内部检查保持 TPASS。`access01` 仍有 LTP harness 子进程结果上报缺口，`access02` 仍因执行 `file_x` 的脚本/exec 语义 TFAIL，整组状态仍按真实失败记录在 `rv-current.md`。

## 2026-06-24 状态更新：RISC-V BusyBox applet stat 兼容

本轮补强 `newfstatat(79)`：当用户态对已知 BusyBox applet 路径做 stat 探测、但只读镜像里没有单独 applet inode 时，内核会返回真实 BusyBox 二进制的 Linux `struct stat` 元数据。该语义与既有 `faccessat(48)` applet 存在性兼容和 `execve(221)` applet fallback 对齐，避免 shell/`which` 在 exec 前的 stat 检查把可由 BusyBox 分发的命令误判为不存在。

语义边界：只对固定 allowlist 中的 BusyBox applet 名生效；不创建文件、不创建 symlink、不修改测试镜像。未知命令仍从 `newfstatat` 返回 `-ENOENT`，避免把所有缺失路径都伪装成 BusyBox。

验证：`make all` 在固定 docker 构建环境通过，随后固定 RV docker 复跑到 `sys_shutdown` 并枚举 24 组。最新严格扫描为 16 组 clean、8 组失败；`busybox-musl` 和 `busybox-glibc` 中 `which ls` 均为 success，`ltp-musl`/`ltp-glibc` 不再出现 `basename: not found`，但仍因真实 LTP 内部失败保持失败状态，详见 `rv-current.md`。

## 2026-06-24 状态更新：RISC-V robust futex owner-death

本轮将 `set_robust_list(99)` / `get_robust_list(100)` 从空成功桩推进为最小 Linux/RISC-V 兼容语义。每个 `proc_t` 记录 robust list 的 head 和长度；正常退出、线程退出和强制清理 descendants 时，会按 RISC-V LP64 `struct robust_list_head` 布局读取用户链表，给退出 TID 持有的 futex word 写入 `FUTEX_OWNER_DIED`，保留 `FUTEX_WAITERS` 位，并唤醒等待者。

语义边界：当前只接受 24 字节 robust-list head，链表遍历上限为 2048 项；遇到未映射或不可写用户地址时停止/跳过该项，避免退出路径 panic。该实现不包含 PI futex，也不是完整 Linux thread-group robust-futex 模型。

验证：`make all` 在固定 docker 构建环境通过，随后固定 RV docker 复跑到 `sys_shutdown` 并枚举 24 组。最新 `os_serial_out_rv.txt` 中 `libctest-glibc` 的 `pthread_robust_detach` 静态/动态子项均只出现 START/END，不再出现旧日志中的 `ETIMEDOUT` vs `EOWNERDEAD` 失败。`libctest-glibc` 整组仍因其他真实失败保持失败状态，详见 `rv-current.md`。

## 2026-06-24 状态更新：RV initcode 完整枚举 24 组

本轮按固定 docker 命令复跑，`os_serial_out_rv.txt` 已到 `sys_shutdown`，并确认 RV initcode 枚举到 `/musl` 12 组和 `/glibc` 12 组，共 24 组。`src/user/initcode.c` 的目录扫描不再把短 `getdentries64` 读当作 EOF，而是持续读取直到 `SYS_get_dentries <= 0`。

本轮主表同步到当前 `src/kernel/syscall/syscall.c` 分发表：共 126 个入口，其中 123 个 Linux/RISC-V ABI 或兼容入口，3 个 SeaOS 私有入口。

当前 RV 评测状态以 `rv-current.md` 为准。最新严格扫描为 16 组 clean、8 组失败；busybox 子项 `fail` 行和 LTP `FAIL LTP CASE` 行按真实失败记录，不再仅凭 `GROUP END` 判定成功。失败组包括 `libctest-glibc`、`netperf-glibc`、`unixbench-musl`、`lmbench-musl`、`ltp-musl`、`unixbench-glibc`、`lmbench-glibc`、`ltp-glibc`。

本轮新增或补强的 syscall/语义：

| 号 | 名 | 当前语义/修正 |
|---|---|---|
| 39/40 | umount2/mount | 基础测试所需最小挂载/卸载兼容入口。 |
| 53/54 | fchmodat/fchownat | 最小权限/属主修改兼容，避免路径探测落到 unknown syscall。 |
| 68 | pwrite64 | 按给定 offset 写入并恢复 fd offset。 |
| 153/154 | times/setpgid | 基础进程时间和进程组兼容入口。 |
| 194-197 | SysV SHM | `shmget/shmctl/shmat/shmdt` 最小段管理与映射。 |
| 283 | membarrier | 单核最小兼容，支持 query/no-op barrier。 |
| 多个文件 syscall | errno 边界 | syscall 出错返回从裸 `(uint64)-1` 收敛为 `-EXXX`，减少误报 `Operation not permitted`；剩余 `EBADF/EMFILE` pipe 失败仍按真实缺口记录。 |

## 2026-06-18 状态更新：glibc 压力组推进到 `lmbench-glibc`

本轮按固定 docker 命令复跑，RISC-V 串口日志已到 `sys_shutdown`。主表已同步到当前 `src/kernel/syscall/syscall.c` 分发表：共 111 个入口，其中 108 个 Linux/RISC-V ABI 或兼容入口，3 个 SeaOS 私有入口。

最终 `os_serial_out_rv.txt` 确认以下组均到 `GROUP END` 且随后出现 `test sucess`：

```text
unixbench-musl
busybox-musl
cyclictest-musl
netperf-musl
lmbench-musl
iperf-musl
unixbench-glibc
libcbench-glibc
libctest-glibc
busybox-glibc
cyclictest-glibc
netperf-glibc
lmbench-glibc
```

本轮新增或补强的 syscall/语义：

| 号 | 名 | 当前语义/修正 |
|---|---|---|
| 82 | fsync | 主表补记；有效 fd 最小成功返回。 |
| 83 | fdatasync | 主表补记；同 `fsync`。 |
| 98 | futex | 从 WAIT/WAKE 扩展为最小 WAIT/WAKE/WAIT_BITSET；带 timeout 的等待按 SeaOS tick 粗粒度超时并返回 `-ETIMEDOUT`。 |
| 130/131 | tkill/tgkill | 主表补记；最小线程 signal 兼容。 |
| 157 | setsid | 主表补记；返回调用者 pid 作为 session id。 |
| 163/164/261 | getrlimit/setrlimit/prlimit64 | 主表补记；支持当前进程 `RLIMIT_NOFILE` 的静态 fd 表语义。 |
| 215 | munmap | `addr` 页对齐校验，`len` 按 Linux 语义向上页对齐；`CLONE_VM` live siblings 同步清 PTE 并单次释放 PA。 |
| 227 | msync | 主表补记；最小兼容成功路径。 |
| 278 | getrandom | 主表补记；非阻塞伪随机字节。 |

相关非 syscall 分发表但影响 syscall 行为的生命周期修正：

- `CLONE_VM` 线程退出后由调度器后台回收线程壳，避免 pthread 压力测试耗尽进程槽。
- `clear_child_tid` 在正常退出和强制清理 descendants 时都会清零并 futex wake。
- 被 `proc_reparent()` 过继给 `proczero` 的孤儿 zombie 用 `reparented_to_init` 标记，后台只回收这些孤儿；initcode 直接启动的测试子进程仍由 `wait4` 正常收尾并打印 `test sucess/test fail`。
- `N_MMAP` 提高到 8192，避免 glibc pthread stack/guard 映射 churn 触发 `mmap_region_node` 枯竭。

仍保留的兼容噪声：

- glibc netperf 内部仍可能打印 netserver/control 失败文本；当前测评脚本仍给出组级 `test sucess`。

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
| — | procfs | 最小 in-memory procfs 支持 `/proc/mounts`、`/proc/meminfo`、`/proc/uptime`、`/proc/stat`、`/proc/self/*`、`/proc/<pid>/*`；`/proc/self/maps` 输出当前进程 heap/mmap/stack 元数据 |
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

## 2026-06-24 status update: wait4 errno/mask semantics

| No. | Name | Current semantics |
|---|---|---|
| 260 | wait4 | Returns `-ECHILD` when no matching child exists; only unblocked pending signals interrupt waits; `SIGCHLD` wakes and rescans for zombies. |

This closes the false `EPERM` userland decode caused by returning bare `(uint64)-1` from `wait4`, and prevents masked pending signals from spuriously becoming `EINTR`.

## 2026-06-24 status update: signal `siginfo_t` source metadata

| No. | Name | Current semantics |
|---|---|---|
| 129 | kill | Sets a pending signal and records `SI_USER` plus the sender pid/tgid for later delivery. |
| 130 | tkill | Sets a pending thread signal and records `SI_TKILL` plus the sender thread-group id. |
| 131 | tgkill | Validates tgid/tid relationship, sets the pending thread signal, and records `SI_TKILL` plus sender tgid. |
| 137 | rt_sigtimedwait | Consumes one pending matching signal and copies stored `si_code`/`si_pid` into the optional `siginfo_t`. |
| 172/178 | getpid/gettid | `getpid` returns the thread-group leader for `CLONE_THREAD` members; `gettid` remains the per-thread id. |

The retained implementation keeps the existing SeaOS RISC-V signal-frame layout. A Linux-shaped frame/VDSO experiment was rejected after it regressed `libctest-musl`; future static glibc cancellation work should implement signal-unwind compatibility explicitly instead of silently changing the frame contract.

## 2026-06-24 status update: minimal `SA_RESTART` for `wait4`

| No. | Name | Current semantics |
|---|---|---|
| 134 | rt_sigaction | Records `SA_RESTART` in the existing per-signal flags. |
| 139 | rt_sigreturn | Restores the existing SeaOS signal frame; for a restarted `wait4`, the frame already contains the original `ecall` PC and argument registers. |
| 260 | wait4 | If interrupted by a delivered signal whose handler has `SA_RESTART`, restarts with the original `pid/status/options` arguments instead of leaking `EINTR` to userland. |

Boundary: only `wait4` is whitelisted for restart in this iteration. Futex, accept, sleep, and other interruptible calls still return `-EINTR` so pthread cancellation and timeout-driven networking paths keep their existing behavior.

Verification: `make all` passed in the fixed docker build environment, and the fixed RV docker run reached `sys_shutdown` after all 24 groups. The latest `os_serial_out_rv.txt` has zero `waitpid(...,0) failed: EINTR` markers; the previous log had 37.
