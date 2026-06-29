# CLAUDE.md — SeaOS LoongArch 工作记忆

> 本文件由 Claude Code / agent 每次会话自动加载。它记录 LoongArch（B 线）的
> 当前事实、命令、约束和下一个调试目标。全项目规则在 `AGENTS.md`；详细
> 交接文档在 `docs/LOONGARCH_MUSL_HANDOFF.md`；开发日志在 `la-current.md`。

## 2026-06-27 当前覆盖说明

本文件下方较早的 `libctest/pthread_cancel` 聚焦记录已经过期；当前权威状态
以 `la-current.md` 与 `la-task.md` 为准。当前 LoongArch `/musl` clean 组为：
`libcbench-musl`、`libctest-musl`、`unixbench-musl`、`busybox-musl`、
`netperf-musl`、`iperf-musl`、`iozone-musl`、`lua-musl`、`basic-musl`、
`lmbench-musl`、`cyclictest-musl`。
剩余未 clean 组仅为 `ltp-musl`。
`lmbench-musl` 已由 sparse-zero memfs hole 与只读 ELF 段 fork-share 推进为
clean，不要再从旧 `lat_fs` 或 `lat_ctx 96` blocker 开始。`cyclictest-musl`
已由 LoongArch tick-timebase 修复推进为 clean：
`/tmp/seaos_la_cyclictest_ticktime_clean1.log` 中 `NO_STRESS_P1/P8` 与
`STRESS_P1/P8` 全部 `end: success`，`kill hackbench: success`，到达
`#### OS COMP TEST GROUP END cyclictest-musl ####` 和 `shutdown: system halting`，
广义失败扫描为空。根因是 `clock_gettime/gettimeofday` 曾使用 stable counter，
而 `clock_nanosleep(TIMER_ABSTIME)` deadline 使用 100 Hz `la_ticks`；QEMU
压力下两套时间源漂移，1 秒 cyclictest sleep 被调度成数十秒级等待。旧的
fdpair ENFILE、readyfds `Connection reset`、runqueue-pressure、pressure
time-slice 和 stream-window 试探均已被该 clean 证据 supersede，不要再从
这些方向开始。`iozone-musl` 本轮由 LoongArch memfs/socket 静态容量收敛推进为
clean：`/tmp/seaos_la_iozone_mem_budget3.log` 到达
`#### OS COMP TEST GROUP END iozone-musl ####`，硬失败扫描为空。为避免 socket
容量收敛回退，`/tmp/seaos_la_netperf_sock256_recheck.log` 与
`/tmp/seaos_la_iperf_sock256_recheck.log` 均已复核 clean。COW 与 highmem
user-page 试探仍为拒绝方向，不要从这些半截实验继续。恢复完整 `/musl`
入口后的 `/tmp/seaos_la_musl_restore_after_iozone.log` 在 180 秒 timeout 前
到达 `libcbench-musl`/`libctest-musl` GROUP END 并进入 `unixbench-musl`，
硬失败扫描为空；它只是 smoke，不是完整 `/musl` 通过证据。
`netperf/iperf/iozone`
已用动态 buffer 版本重跑回归：
`/tmp/seaos_la_netperf_dynsock512_recheck.log`、
`/tmp/seaos_la_iperf_dynsock512_recheck.log`、
`/tmp/seaos_la_iozone_dynsock512_recheck.log` 均到达 GROUP END，硬失败扫描为空。
`ltp-musl` 已完成 focused 定位并推进
`fchmodat/fchownat/setpgid/setuid` setup blockers、`abort01`、
`accept01/02/03`、`symlinkat(36)` ENOSYS，以及 `access02` 的
`file_x`/`symlink_x` X_OK 执行路径；本轮 `access04` errno cases 在
`/tmp/seaos_la_ltp_access04_ro_mount.log` 中 TPASS。最新 focused LTP
进一步证明 `access01/access02/access03/access04` 和 `adjtimex01/02/03`
均为 `: 0`，AF_ALG socket 已按不支持返回 `EAFNOSUPPORT` 并转为
TCONF。本轮继续推进 raw IPv6/sendmsg/ancillary 层：
`/tmp/seaos_la_ltp_rawdeliver2.log` 显示 `asapi_02` 12 个断言全部
TPASS，`asapi_03` 的 `IPV6_RECVPKTINFO set-get` 与 receive 均 TPASS；
`acct(89)`、`add_key(217)`、`keyctl(219)` 已从 unknown syscall 改为显式
`-ENOSYS`。`/tmp/seaos_la_ltp_ancillary1.log` 进一步显示 `asapi_03`
18 个断言全部 TPASS，包括 `IPV6_RECVHOPLIMIT/RTHDR/HOPOPTS/DSTOPTS/
TCLASS` 与 2292 ancillary options。若继续 LTP，应从 kernel config、
shell helper 缺失、`hopopt` libc 表边界、`ask_password.sh`/
`assign_password.sh` 等后续环境/ABI 缺口开始；asapi 早期 RAW socket
`EINVAL`、`IPV6_CHECKSUM` offset 语义、ICMP6_FILTER raw delivery、
`IPV6_RECVPKTINFO`/`sendmsg` 和 IPv6 ancillary receive options 已推进。
`hopopt` protocol-0 lookup 已定位为当前 musl 内置协议表边界，不是内核或
runtime stub 小修目标。
2026-06-27 focused LTP 继续推进环境层：
`/tmp/seaos_la_ltp_mktemp1.log` 证明 `/bin/mktemp` 运行期 wrapper 生效，
`ar01.sh` 不再因 `mktemp` `No such file` 直接中断，而是推进到
`sh: out of range` / `timeout need to be >= 1 ()`；该层已被后续
`/tmp/seaos_la_ar01_min_ar10.log` supersede。`/tmp/seaos_la_ltp_kconfig1.log` 证明运行期
`KCONFIG_PATH=/etc/seaos-kconfig` 生效，`acct02` 与 `aslr01` 不再
`Cannot parse kernel .config`，而是分别因 `CONFIG_BSD_PROCESS_ACCT=n`
和 `CONFIG_HAVE_ARCH_MMAP_RND_BITS=n` 正常走 TCONF。该阶段的
`ar01.sh` shell/timeout 与 `arping01.sh` 缺口已被后续 ar/arping 证据
supersede；`ltp-musl` 当前仍因 `hopopt`、`ask_password.sh`/
`assign_password.sh` 和后续 ABI/environment 缺口不 clean。恢复完整入口后的
`/tmp/seaos_la_musl_after_ltp_kconfig_smoke.log` 在 180 秒 timeout 前到达
`libcbench-musl`/`libctest-musl` GROUP END 并进入 `unixbench-musl`，
广义失败扫描为空；它只是 full-entry smoke，不是完整 `/musl` 通过证据。
后续 `/tmp/seaos_la_arping01_clean1.log` 与
`/tmp/seaos_la_ltp_after_arping_fix1.log` 证明 `arping01.sh` 已 TPASS；
保留修复为 socket fd duplicate refcount、最小 AF_PACKET `sockaddr_ll`
与 synthetic ARP reply。后续 `/tmp/seaos_la_ar01_min_ar10.log` 与
`/tmp/seaos_la_ltp_after_ar10.log` 证明 `ar01.sh` 已 20/20 TPASS；
保留修复为 LoongArch memfs `statx` mode、`O_APPEND`、移除错误的
BusyBox `ar` fallback，以及 runtime `/bin/ar` -> `/tmp/ar` helper。
后续 `/tmp/seaos_la_ltp_after_password2.log` 已证明 `ask_password.sh` 与
`assign_password.sh` ret=0，`/tmp/seaos_la_brk01_fix1.log` 已证明 direct
`brk01` TPASS。本轮继续推进 bind 前段：
`/tmp/seaos_la_bind02_fix1.log`、`/tmp/seaos_la_bind03_fix1.log`、
`/tmp/seaos_la_bind04_fix3.log`、`/tmp/seaos_la_bind05_fix1.log` 分别证明
direct `bind02`/`bind03`/`bind04`/`bind05` clean；
`/tmp/seaos_la_ltp_after_bind05_fix1.log` 证明 focused LTP 顺序中
`bind01`-`bind05` 均为真实 TPASS/ret 0。`ltp-musl` 仍不 clean：已知
`asapi_01` `hopopt` libc 表边界仍 TFAIL，`bind06` 因
`CONFIG_USER_NS`/`CONFIG_NET_NS` 缺失 TCONF，后续 proc/cgroup 环境仍有
`/proc/sys/kernel/pid_max` 与 `/proc/self/mounts` TBROK。
恢复完整入口后的 `/tmp/seaos_la_musl_after_bind_smoke1.log` 在 360 秒 timeout 前到达
`libcbench-musl`/`libctest-musl` GROUP END 并进入 `unixbench-musl`，
广义失败扫描为空；它仍只是 full-entry smoke。
本轮继续推进 capability/proc/cgroup 层：
`/tmp/seaos_la_capability_fix4.log` 证明 direct `capget01`/`capget02` 与
`capset01`-`capset04` 全部 Summary `failed 0`/`broken 0`，广义失败扫描为空；
`/tmp/seaos_la_ltp_after_killall_fix1.log` 证明这些 case 在 focused LTP 顺序中
真实 TPASS/ret 0，旧 `/proc/sys/kernel/pid_max`、`/proc/self/mounts`、
`rmdir not found`、`killall not found` 层已推进。保留修复为最小
LoongArch `capget(90)`/`capset(91)`、`mmap(PROT_NONE)` 权限语义、
runtime `/proc/sys/kernel/pid_max`/`/proc/self/mounts`，以及 `rmdir`/`killall`
BusyBox wrappers。`ltp-musl` 仍不 clean：下一层是 `asapi_01` `hopopt`、
`bind06` namespace-config TCONF，以及 cgroup controller/helper 缺口
（memory/base controller TCONF、helper `must call tst_run` TBROK、
`controller not defined`、`Number of subgroups must be possitive integer`）。
恢复完整入口后的 `/tmp/seaos_la_musl_after_capability_smoke1.log` 在 360 秒
timeout 前到达 `libcbench-musl`/`libctest-musl` GROUP END 并进入
`unixbench-musl`，硬失败扫描为空；它不是完整 `/musl` 通过证据。
本轮继续推进 LTP optional-syscall 探测层：LoongArch 已把
`eventfd2(19)`、`epoll_create1(20)`、`inotify_init1(26)`、
`signalfd4(74)`、`timerfd_create(85)`、`perf_event_open(241)`、
`fanotify_init(262)`、`memfd_create(279)`、`bpf(280)`、
`userfaultfd(282)`、`io_uring_setup(425)`、`open_tree(428)`、
`fsopen(430)`、`fspick(433)`、`pidfd_open(434)` 与
`memfd_secret(447)` 注册为显式 `-ENOSYS`，避免 LTP fd-creation 探测
落入 dispatcher `UNKNOWN`。`/tmp/seaos_la_ltp_enosys_only1.log` 证明
旧 `accept03` 附近的 `UNKNOWN #...` 串消失；`ar01.sh` 仍为 20/20
TPASS，`arping01.sh` 仍 TPASS，`broken_ip-*` 能继续推进到 TPASS/TCONF。
同一日志再次确认 `asapi_01` 的 `hopopt` TFAIL 和 cgroup helper
TBROK 仍是当前 LTP 阻塞：`cgroup_fj_common.sh`/`cgroup_lib.sh` 被官方
wrapper 直接执行时报 `must call tst_run`，`cgroup_fj_function.sh` 无参数时报
`controller not defined`，`cgroup_fj_stress.sh` 无参数时报
`Number of subgroups must be possitive integer`，随后进入
`cgroup_regression_3_1.sh` helper。试探 `TST_TIMEOUT=30` 会让 `ar01.sh`、
`arping01.sh` 与 `broken_ip-*` 回归 `timeout need to be >= 1 ()`，已撤回；
保留 `TST_TIMEOUT=-1`。恢复完整入口后的
`/tmp/seaos_la_musl_after_enosys_stub_smoke1.log` 到达 libcbench/libctest
GROUP END 并进入 unixbench，输出 DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/
SPAWN/EXECL，硬失败扫描为空；仍不是完整 `/musl` pass 证据。当前
`/musl` 大组计数不变：11/12 clean，剩余 `ltp-musl`。

## 当前优先级

- 目标：让 LoongArch 内核真正通过所有 `/musl` 测试。
- 不要把 `======== test sucess ========` 或 `#### OS COMP TEST GROUP END ... ####`
  这类包装层输出当作通过的证据。
- 只有当一个测试的真实子测试既没有 `FAIL`、没有 `[SEGV]`、没有
  `Function not implemented`、没有 `Interrupted system call`、没有 panic/trap
  失败、也没有 unknown syscall 时，才算真正通过。
- 当前聚焦：完整 `/musl` 扫描入口；下一步只剩 `ltp-musl`。使用
  `/tmp/seaos_la_ltp_enosys_only1.log` 作为起点，定位仍未解决的
  `asapi_01` `hopopt` libc 表边界、`bind06` namespace-config TCONF，以及
  cgroup controller/helper 缺口。不要再从 `ar01.sh`、`arping01.sh`、
  password/keyctl helper、direct `brk01`、`bind01`-`bind05`、`capget/capset`、
  `/proc/sys/kernel/pid_max`、`/proc/self/mounts`、`rmdir`、`killall` 或
  LTP optional fd-creation `UNKNOWN` 开始，
  除非新鲜日志证明回归。
  不要再从 cyclictest、socketpair ENFILE、readyfds
  `Connection reset`、pressure `LA_TIME_SLICE=1`、512-byte stream window
  或旧 hackbench/STRESS_P8 timeout 开始，除非新鲜日志证明回归。
- glibc 工作有意推迟，等 musl 真正干净后再做。

## 仓库与环境

- 工作区：`/home/addaswsw/project/OS/oskernel2025-seaos`
- 迭代用 Docker 容器：`seaos-la`
- 容器内工作区：`/workspace`
- 官方镜像 / 工具链容器：`zhouzhouyi/os-contest:20260510`
- 容器内官方 QEMU 路径：`/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64`
- 比赛用 QEMU 版本不可更改。
- 不可修改 `data/sdcard-rv.img.gz`、`data/sdcard-la.img.gz`、官方测试脚本
  或测试二进制。

构建并复制可运行内核：

```bash
docker exec seaos-la bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'
```

用真实测试镜像跑当前 LoongArch 测试入口：

```bash
docker exec seaos-la bash -lc 'cd /workspace && timeout 5400 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_musl_next.log 2>&1'; echo exit=$?
```

检查失败：

```bash
docker exec seaos-la bash -lc 'grep -a -n "^FAIL \|\[SEGV\]\|failed:\|UNKNOWN\|unknown syscall\|trap:\|panic\|Function not implemented\|Interrupted system call\|No such file\|argument expected\|end: fail\|test fail\|test timeout\|exec fail\|fork fail\|Fork failed\|Operation not permitted\|Broken pipe" /tmp/seaos_la_musl_next.log | tail -260'
docker exec seaos-la bash -lc 'grep -a -n "#### OS COMP TEST GROUP START\|#### OS COMP TEST GROUP END\|run /musl/\|cyclictest\|iozone\|lmbench\|ltp" /tmp/seaos_la_musl_next.log'
```

## 当前测试入口

`src/user/initcode_la.c` 当前应保持完整 `/musl` 扫描：

```text
run_test_entries("/musl")
```

不要恢复旧的 libctest-only 入口，除非新鲜完整扫描证明 libctest 回归。

## 历史 libctest 聚焦状态（已过期）

以下 2026-06-24 的 libctest/pthread_cancel 记录仅作为历史诊断上下文；
当前 clean/remaining 组与下一步以本文顶部、`la-current.md` 和 `la-task.md`
为准。

日期：2026-06-24。

最新一次运行：

```text
/tmp/seaos_la_libctest_timed_futex.log
command exit = 124 because host timeout killed QEMU after kernel printed shutdown
```

该日志中的重要结果：

```text
FAIL pthread_cancel [status 247]
FAIL pthread_cancel [status 247]
```

解读：

- 静态 `pthread_cancel` 仍然超时。
- 动态 `pthread_cancel` 仍然超时。
- `pthread_cond`、`pthread_cond_smasher`、`pthread_condattr_setclock` 和
  动态 `sem_init` 现在打印 `Pass!`。
- `open: lookup '/etc/passwd' FAILED` 会出现在日志里，但对应的测试仍然
  打印 `Pass!`；不要单独把这一行当作失败。
- 脚本到达了 `GROUP END libctest-musl` 并打印包装层 `test sucess`，但这
  不算真正通过，因为还有两个 `pthread_cancel` 失败。

## 应保留的 LoongArch 已完成工作

- `exec` 现在把新的 trap frame 拷到活动的 syscall trap frame 中，而不是在
  `sys_exec` 内部直接调用 `la_user_return()`。
- 信号投递现在构建一个更大的信号帧，包含 `siginfo`、`ucontext`、保存的
  信号 mask，以及给 `rt_sigreturn` 用的栈 trampoline。
- `rt_sigaction` 使用 Linux 内核 ABI 布局：
  `{ handler, flags, restorer, mask }`。
- FD 表从 32 扩大到 128 条。
- FD 状态现在记录 `FD_CLOEXEC` 和 `O_NONBLOCK`。
- `fcntl` 支持 `F_DUPFD`、`F_DUPFD_CLOEXEC`、`F_GETFD`、`F_SETFD`、
  `F_GETFL`、`F_SETFL`。
- `dup`、`dup3`、`pipe2`、`socket`、`accept4` 和 open 路径能更准确地
  保留或设置 fd 标志。
- 控制台写入现在要求 `LA_FD_CONSOLE`；关掉 stdout 再 dup 一个文件不再
  只按 fd 号写到 UART。
- `/dev/null` 与 `/dev/zero` 以简单字符设备形式支持。
- `fstat`、`newfstatat`、`statx` 在 libctest 使用的字段上具有
  LoongArch/musl 兼容的布局。
- `utimensat` 持久化 memfs 时间戳，支持 `UTIME_NOW`、`UTIME_OMIT`
  以及基于 fd 的 `futimens`。
- `RLIMIT_NOFILE` 是进程局部的，fork/clone 时继承。
- Futex 现在支持 `WAIT`、`WAKE`、`REQUEUE`、`CMP_REQUEUE`、
  `WAIT_BITSET`、`WAKE_BITSET`。
- 带超时的 futex 等待通过调度器 deadline 实现；这修复了
  `pthread_cond_smasher` 和 `sem_init` 的回归。

## 当前失败假设

剩下的 `pthread_cancel` 超时大概率出在以下某条路径：

- SIGCANCEL 投递的信号帧 / `ucontext` 布局与 musl 期望的 `SA_SIGINFO`
  格式不完全一致。
- musl 的 cancel handler 修改 `ucontext` 之后，`rt_sigreturn` 恢复的 mask
  或 PC 偏移有误。
- 某条取消路径上，睡眠中的线程被 `tgkill`/futex 唤醒后按普通唤醒返回，
  没有按 `-EINTR` 返回。
- 目标线程的 `clear_child_tid` 或线程退出唤醒不完整。

推荐的下一步：

1. 临时把 initcode 进一步收窄为只跑静态 `pthread_cancel`。
2. 在 `clone`、`tgkill`、信号投递、`rt_sigreturn`、futex 等待/唤醒/EINTR
   与 `la_proc_exit` 处加少量带守护条件的日志。
3. 确认目标线程是否真的收到 SIGCANCEL、进入 handler、从 `rt_sigreturn`
   返回、退出、唤醒 join 线程。
4. 同样处理动态 `pthread_cancel`。
5. 修好后立刻删掉临时诊断。

## 硬性约束

- 所有编辑过的文件保持 UTF-8。保留已有的中文注释。
- 不通过改包装层、抑制日志、伪造输出或跳过用户程序来隐藏真实失败。
- 不修改官方压缩镜像或官方测试。
- LoongArch 改动不能让 `kernel-rv` 回退。
- 如果改了 `src/kernel/loongarch/` 之外的公共/共享文件，必须显式注明
  RISC-V 的风险。

## 文档指针

- `AGENTS.md`：SeaOS 全项目规则与 RISC-V 主导的约定。
- `la-current.md`：LoongArch 当前开发日志与状态。
- `docs/LOONGARCH_MUSL_HANDOFF.md`：交给下一位 agent 或队友的交接文档。
- `docs/SYSCALL_STATUS.md`：syscall 状态表，可能滞后，musl 全绿后
  应该更新。
- `docs/DECISIONS.md`：设计决策；任何会改变 syscall/进程/内存语义的
  新兼容策略都要更新。
