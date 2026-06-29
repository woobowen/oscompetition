# LoongArch musl 交接文档

> 更新日期：2026-06-29
>
> 目的：为下一位开发者/Agent 在 LoongArch 线上真正跑通 `/musl` 测试提供一个干净的起点。

## 2026-06-29 continuation 更新

当前权威状态仍以根目录 `la-current.md` 和 `la-task.md` 为准。本轮没有新增
可计数 clean 大测试组，`/musl` 仍为 11/12 clean，剩余 `ltp-musl`。
但完整 `/musl` 顺序已越过旧 `lmbench-musl` `lat_fs 0k` 卡点并进入 LTP；
本轮修复了随后暴露的 LTP `waitpid(...)=EINTR` regression。

保留修复：

- `wait4(260)` pending signal 路径区分用户可见 `-EINTR` 与内部
  `-ERESTARTSYS`。
- `SA_RESTART`、默认动作和忽略动作走内部 restart；trap 层遇到
  `-ERESTARTSYS` 时不写回 `a0`、不前进 `era`，先投递信号。
- handler 正常返回后重启原 wait syscall；handler `longjmp` 则自然离开等待。

新增证据：

- `/tmp/seaos_la_musl_full_clean1.log`：完整 `/musl` 官方 QEMU 顺序通过
  `libcbench-musl`、`libctest-musl`、`unixbench-musl`、`busybox-musl`、
  `cyclictest-musl`、`netperf-musl`，并越过旧 `lmbench-musl` 卡点进入
  `ltp-musl`；该 LTP 段有 43 条 `waitpid.*EINTR`，不是 clean。
- `/tmp/seaos_la_ltp_restart1.log`：focused LTP-only 官方 QEMU 中
  `waitpid.*EINTR` 计数为 0；`add_key*`、`adjtimex*`、`alarm*`、
  `bind01`-`bind05`、`brk01/02`、`capget01/02`、`capset01`-`capset04`
  不再出现 waitpid TBROK。
- `/tmp/seaos_la_ltp_protocol1.log`：`/etc/protocols` 顺序试探仍不能修复
  `asapi_01` `hopopt` TFAIL，相关改动已撤回；该点继续按 musl 内置
  protocol table 边界记录。

继续 LTP 时不要再从 `waitpid/EINTR` 或旧 `lmbench lat_fs` 开始。当前真实
边界仍是 `asapi_01` `hopopt` libc-table boundary、`bind06`
namespace-config TCONF、cgroup controller/helper blocker。只读 `debugfs`
抽取的 `/musl/ltp/testcases/bin/cgroup_regression_3_1.sh` 证明该 helper
无参数时是无限 `mkdir/rmdir` 循环；官方 `/musl/ltp_testcode.sh` 又会把
`ltp/testcases/bin/*` 全部作为独立 case 执行。在禁止改脚本/镜像、跳过
helper 或伪造输出的硬约束下，该点不能被计为 clean。

## 2026-06-26 当前覆盖说明

下方 `libctest/pthread_cancel` 聚焦记录是历史状态。当前权威状态以仓库根目录
`la-current.md` 和 `la-task.md` 为准：LoongArch `/musl` 已有最新证据的
clean 组为 `libcbench-musl`、`libctest-musl`、`unixbench-musl`、
`busybox-musl`、`netperf-musl`、`iperf-musl`、`lua-musl`、`basic-musl`、
`lmbench-musl`、`iozone-musl`、`cyclictest-musl`。本次 continuation 新增可计数
clean 组为 `cyclictest-musl`；`iozone-musl` 与 `lmbench-musl` 是此前同日已验证
clean 的基线。当前 `/musl` 只剩 `ltp-musl` 未 clean。保留的 lmbench
前置修复包括：
memfs 对空文件/空洞的
全零写入采用 sparse-size 表示，读取未分配页时返回零填充。直接 focused
日志 `/tmp/seaos_la_lmbench_latfs_sparse1.log` 显示
`lmbench_all lat_fs /var/tmp` 已输出 0k/1k/4k/10k 结果并到达 wrapper
success，且没有 `memfs: out of memory`。官方 focused
`/tmp/seaos_la_lmbench_sparse1.log` 曾越过 `lat_fs`，但 2400 秒超时在
最后的 `lat_ctx 96`，未到 GROUP END。后续
`/tmp/seaos_la_lmbench_latctx96_forkoom_diag.log` 证明该 blocker 是
`fork: copy_pgtbl OOM pid=4`；保留的 LoongArch exec 修复现在按 ELF
`p_flags` 将只读 PT_LOAD 段映射为 user RX，并使用现有 fork-share/refcount
路径共享 text/rodata。`/tmp/seaos_la_lmbench_latctx96_execshare1.log`
显示 direct `lat_ctx 96` 输出 `96 59.84` 并到达 wrapper success；
`/tmp/seaos_la_lmbench_execshare2.log` 显示官方 focused
`lmbench_testcode.sh` 到达 GROUP END，`lat_ctx` 覆盖 2/4/8/16/24/32/64/96，
广义失败扫描为空。
`iozone-musl` 旧 focused 日志 `/tmp/seaos_la_iozone_focus1.log` 的
`Fork failed` blocker 已被 `/tmp/seaos_la_iozone_mem_budget3.log` supersede：
该日志到达 GROUP END 与 `shutdown: system halting`，硬失败扫描为空，
所以当前 iozone clean。保留的 iozone 修复是 LoongArch 静态 BSS 内存预算：
`MEMFS_MAX_INODES=32768`、`LA_NSOCK=256`，并保留 `LA_NFD=512`。该组合把
kernel-la BSS 从约 178MB 降至约 39MB，pmem 从约 83MB 提升至约 216MB。
`/tmp/seaos_la_netperf_sock256_recheck.log` 与
`/tmp/seaos_la_iperf_sock256_recheck.log` 证明 socket 组未因 `LA_NSOCK=256`
回退。后续为了推进 `cyclictest`，`LA_NSOCK` 已恢复为 512，但 stream socket
receive buffer 改为按需分配的一页；pipe metadata 上限为 8192，pipe data
buffer 同样按需分配。本轮动态 buffer 版本的
`/tmp/seaos_la_netperf_dynsock512_recheck.log`、
`/tmp/seaos_la_iperf_dynsock512_recheck.log` 与
`/tmp/seaos_la_iozone_dynsock512_recheck.log` 均到达 GROUP END，硬失败扫描为空。
`cyclictest-musl` 已由 LoongArch timebase 修复推进为 clean。最新 focused
官方 QEMU 证据 `/tmp/seaos_la_cyclictest_ticktime_clean1.log` 显示
`NO_STRESS_P1/P8` 与 `STRESS_P1/P8` 全部 `end: success`，
`kill hackbench: success`，到达
`#### OS COMP TEST GROUP END cyclictest-musl ####` 与
`shutdown: system halting`，广义失败扫描为空。根因是
`clock_gettime/gettimeofday` 曾使用 stable counter，而
`clock_nanosleep(TIMER_ABSTIME)` deadline 使用 100 Hz `la_ticks`；QEMU
压力下二者漂移，使 cyclictest 的 1 秒绝对 sleep deadline 变成数十秒级等待。
保留修复是让 `sys_clock_gettime(113)` 与 `sys_gettimeofday(169)` 使用同一个
100 Hz tick clock。旧 `/tmp/seaos_la_cyclictest_memfs_inode_cap.log`、
`/tmp/seaos_la_cyclictest_socketpair_type.log`、
`/tmp/seaos_la_cyclictest_sched_diag1.log` 和 pressure/window 试探均已
supersede；不要再从这些旧 STRESS_P8 方向开始，除非新鲜扫描证明回归。
本轮继续验证了该判断：`/tmp/seaos_la_iozone_cow2.log`、
`/tmp/seaos_la_iozone_cow_mprotect1.log`、`/tmp/seaos_la_iozone_cow_tlbinval1.log`
均出现真实 `pc=0`/ADEF 用户故障，COW 实验已撤回；`/tmp/seaos_la_cyclictest_red1.log`
显示 `NO_STRESS_P1/P8` 已 success，但 hackbench 仍 fork/OOM；
`/tmp/seaos_la_cyclictest_highmem1.log`、
`/tmp/seaos_la_cyclictest_highmem2.log` 和
`/tmp/seaos_la_cyclictest_usermem1.log` 证明直接使用
`0x90000000..` 或 `0x10000000..0x18000000` 作为用户页池会在启动/exec
早期 ADEF，相关 highmem/扩展池代码已撤回。
本轮追加 `/tmp/seaos_la_iozone_highuser1.log`、
`/tmp/seaos_la_iozone_highuser2.log` 与
`/tmp/seaos_la_iozone_highuser3.log` 复核 highmem user-page 方向：前两者在
`pmem_init` 触摸 high RAM/KVA 时 ADEF；第三者可启动并推进 iozone
throughput initial writers，但在 `la_pmem_alloc_user_page()` 清零 high KVA 时
kernel trap，最终 `test fail`。这些 highmem 补丁已撤回。
`ltp-musl` 已完成 focused 定位并
推进 `fchmodat/fchownat/setpgid/setuid` setup blockers；本轮进一步修复
`abort01` wait-status/core bit，以及 `accept01/02/03`、`accept4_01`
所需 errno/socket 语义；后续 `/tmp/seaos_la_ltp_access04_ro_mount.log`
显示 `access04` errno cases 已 TPASS。本轮 further focused LTP 证明
`access01/access02/access03/access04` 和 `adjtimex01/02/03` 均 `: 0`，
AF_ALG 已转为 TCONF。`ltp-musl` 仍不 clean。本轮追加
`/tmp/seaos_la_ltp_rawsock1.log`、`/tmp/seaos_la_ltp_rawsock2.log`、
`/tmp/seaos_la_ltp_sendmsg1.log`、`/tmp/seaos_la_ltp_rawdeliver2.log` 与
`/tmp/seaos_la_ltp_ancillary1.log`：
早期 IPv6 RAW `socket(10,3,58/159)=EINVAL` 已清除，`asapi_01`
`IPV6_CHECKSUM` offset 19/20/66 错误语义已从 TFAIL 变为 TPASS，
`sendmsg ENOSYS` 层已清除，`asapi_02` 12 个断言全 TPASS，
`asapi_03` 的 18 个断言全 TPASS，包括 `IPV6_RECVPKTINFO`、
`IPV6_RECVHOPLIMIT/RTHDR/HOPOPTS/DSTOPTS/TCLASS` 和 2292 options。
下一批真实缺口从 kernel config、shell helper、`hopopt` libc-table
boundary、`ask_password.sh`/`assign_password.sh` 和后续 ABI/environment
coverage 开始；
`hopopt` protocol-0 lookup 已定位为当前 musl 内置协议表边界，不是内核或
runtime stub 小修目标；`symlinkat(36)` ENOSYS、
`access02` memfs/tmpdir shebang exec fault、`access04` errno、
`access01` result accounting、`adjtimex(171)` ENOSYS 和 AF_ALG `EINVAL`
已推进，不再是当前起点。本轮 `/tmp/seaos_la_ltp_focus1.log` 仍显示
wrapper `FAIL LTP CASE`、shell helper、`hopopt` protocol-0 lookup、
IPv6 RAW/asapi socket 和 kernel config 缺口；该日志已被 rawsock1/2
对 asapi 早期 socket/checksum 层的更新证据 supersede。本轮
`/tmp/seaos_la_ltp_red1.log` 进一步确认：部分子项 Summary 为 passed/skipped
时外层仍打印 `FAIL LTP CASE ... : 0/32`；按当前硬约束不能把它计为 clean，
也不能改官方脚本绕过。只读抽取镜像脚本
`debugfs -R "cat /musl/ltp_testcode.sh" sdcard-la.img` 进一步确认：
该 wrapper 在每个 case 后无条件打印
`FAIL LTP CASE $(basename "$file") : $ret`，即使 `$ret=0`。因此在当前
“真实通过日志不能有 FAIL”的口径下，`ltp-musl` 不能作为下一轮可直接计数的
clean 组；除非验收口径明确排除该官方 wrapper 字符串，否则只能继续减少
真实非零/TFAIL/TBROK 缺口。恢复后的最新完整 `/musl` smoke
`/tmp/seaos_la_musl_restore_after_ltp_rawsock.log` 已到达 `libcbench-musl`、
`libctest-musl` GROUP END 并进入 `unixbench-musl`，在 CONTEXT 后 180 秒
timeout；未命中 `FAIL`、`[SEGV]`、`trap:`、`panic`、`unknown syscall` 或
`Function not implemented`，但包含既有非零 child exit 调试行，因此不是
full pass 证据。2026-06-27 追加 focused LTP 证据：
`/tmp/seaos_la_ltp_mktemp1.log` 显示 `/bin/mktemp` runtime wrapper 生效，
`ar01.sh` 不再因 `mktemp` `No such file` 直接中断，而是推进到
`sh: out of range` / `timeout need to be >= 1 ()` TBROK。
`/tmp/seaos_la_ltp_kconfig1.log` 显示
`KCONFIG_PATH=/etc/seaos-kconfig` 生效，`acct02` 与 `aslr01` 不再
`Cannot parse kernel .config`，而是按 `CONFIG_BSD_PROCESS_ACCT=n` 与
`CONFIG_HAVE_ARCH_MMAP_RND_BITS=n` 正常 TCONF。恢复 full-entry 后的
`/tmp/seaos_la_musl_after_ltp_kconfig_smoke.log` 在 180 秒 timeout 前到达
`libcbench-musl`/`libctest-musl` GROUP END 并进入 `unixbench-musl`，
广义失败扫描为空；它不是完整 `/musl` pass 证据。后续追加
`/tmp/seaos_la_ltp_timeout_disabled1.log` 与干净运行副本上的
`/tmp/seaos_la_ltp_no_ar_cleanimg1.log`，证明 `TST_TIMEOUT=-1` 可让
`ar01.sh` 越过 busybox ash/LTP shell harness 的
`timeout need to be >= 1 ()` 空值层，下一层真实 blocker 是 busybox 缺少
`ar` applet：`ar: applet not found` 和
`ar01 1 TBROK: ar -cr ... failed`。后续
`/tmp/seaos_la_arping01_clean1.log` 与
`/tmp/seaos_la_ltp_after_arping_fix1.log` 证明 `arping01.sh` 已 TPASS；
保留修复为 socket fd duplicate refcount、最小 AF_PACKET `sockaddr_ll`
与 synthetic ARP reply。后续 `/tmp/seaos_la_ar01_min_ar10.log` 与
`/tmp/seaos_la_ltp_after_ar10.log` 证明 `ar01.sh` 已 20/20 TPASS；
保留修复为 LoongArch memfs `statx` mode、`O_APPEND`、移除错误的
BusyBox `ar` fallback，以及 runtime `/bin/ar` -> `/tmp/ar` helper。
后续 `/tmp/seaos_la_ltp_after_password2.log` 与
`/tmp/seaos_la_brk01_fix1.log` 证明 password helpers 和 direct `brk01`
已推进；本轮 `/tmp/seaos_la_bind02_fix1.log`、
`/tmp/seaos_la_bind03_fix1.log`、`/tmp/seaos_la_bind04_fix3.log` 与
`/tmp/seaos_la_bind05_fix1.log` 进一步证明 direct `bind02`-`bind05`
clean。`/tmp/seaos_la_ltp_after_bind05_fix1.log` 证明 focused LTP 顺序中
`bind01`-`bind05` 真实 TPASS/ret 0；同一日志仍保留 `asapi_01`
`hopopt` TFAIL，`bind06` 因 `CONFIG_USER_NS`/`CONFIG_NET_NS` 缺失 TCONF，
后续还有 `/proc/sys/kernel/pid_max` 与 `/proc/self/mounts` proc/cgroup
环境 TBROK。恢复 full-entry 后的
`/tmp/seaos_la_musl_after_bind_smoke1.log` 到达 libcbench/libctest GROUP END
并进入 unixbench，360 秒 timeout 前硬失败扫描为空；它不是完整 `/musl`
pass 证据。下一优先级是继续处理 `ltp-musl`：从 `asapi_01` 的
`hopopt` libc-table boundary、`bind06` namespace config TCONF 和后续
proc/cgroup 环境缺口开始。不要再从 `ar01.sh`、`arping01.sh`、bind01-05、
cyclictest STRESS_P8、pthread_cancel、lua、basic、LTP 初始 setup ENOSYS、
`abort01` 或 `accept01/02/03` 开始，除非新鲜完整扫描显示回归；也不要再从
lmbench 旧 `lat_fs`/`lat_ctx` blockers 开始，除非出现新鲜回归证据。
后续 `/tmp/seaos_la_capability_fix4.log` 证明 direct `capget01`/`capget02`
与 `capset01`-`capset04` 全部 failed/broken 为 0，广义失败扫描为空。
`/tmp/seaos_la_ltp_after_killall_fix1.log` 证明这些 capability cases 在
focused LTP 顺序中真实 TPASS/ret 0，旧 `/proc/sys/kernel/pid_max`、
`/proc/self/mounts`、`rmdir not found` 和 `killall not found` 层已推进。
保留修复为 LoongArch `capget(90)`/`capset(91)` 最小 ABI、`mmap(PROT_NONE)`
权限语义、runtime proc stubs，以及 `rmdir`/`killall` BusyBox wrappers。
恢复完整入口后的 `/tmp/seaos_la_musl_after_capability_smoke1.log` 到达
libcbench/libctest GROUP END 并进入 unixbench，360 秒 timeout 前硬失败扫描为空；
它不是完整 `/musl` pass 证据。下一优先级仍是 `ltp-musl`：`asapi_01`
`hopopt`、`bind06` namespace config TCONF，以及 cgroup controller/helper
缺口（memory/base controller TCONF、helper `must call tst_run` TBROK、
`controller not defined`、`Number of subgroups must be possitive integer`）。
后续 `/tmp/seaos_la_ltp_enosys_only1.log` 证明 LoongArch optional
fd-creation/feature-probe syscalls 已从 dispatcher `UNKNOWN` 改为显式
`-ENOSYS`：eventfd/epoll/signalfd/timerfd/pidfd/fanotify/inotify/
userfaultfd/perf/io_uring/bpf/fsopen/fspick/open_tree/memfd 等探测不再刷
`UNKNOWN #...`。同一日志确认 `ar01.sh` 仍 20/20 TPASS，`arping01.sh`
仍 TPASS，`broken_ip-*` 能推进到 TPASS/TCONF；仍保留 `asapi_01`
`hopopt` TFAIL 和 cgroup helper TBROK。`TST_TIMEOUT=30` 试探日志
`/tmp/seaos_la_ltp_enosys_timeout30_1.log` 使 ar/arping/broken_ip 回归
`timeout need to be >= 1 ()`，已撤回；当前 initcode 继续保留
`TST_TIMEOUT=-1`。恢复完整入口后的
`/tmp/seaos_la_musl_after_enosys_stub_smoke1.log` 到达 libcbench/libctest
GROUP END，进入 unixbench 并输出 DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/
SPAWN/EXECL，硬失败扫描为空；它不是完整 `/musl` pass 证据。当前
`/musl` 计数仍为 11/12 clean，剩余 `ltp-musl`。

## 一句话目标

让 SeaOS LoongArch 真正通过所有 `/musl` 测试。如果任何真实子测试仍然失败，不能把包装层成功字符串当作通过。

## 硬性约束

- 使用官方 Docker/QEMU 环境。
- 不修改 QEMU 版本。
- 不修改 `data/sdcard-rv.img.gz`、`data/sdcard-la.img.gz`、官方测试脚本或测试二进制。
- 不通过抑制日志、伪造输出、硬编码测试输出或跳过用户程序执行来隐藏失败。
- 源码/文档保持 UTF-8。
- LoongArch 改动应限制在 `src/kernel/loongarch/` 与 `src/user/initcode_la.c` 之下，除非确实需要改公共文件。
- 若修改了公共文件，必须显式评估 RISC-V 回退风险。

## 环境

- 宿主机仓库：`/home/addaswsw/project/OS/oskernel2025-seaos`
- Docker 容器：`seaos-la`
- 容器内仓库：`/workspace`
- QEMU：`/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64`
- 磁盘镜像：`/workspace/sdcard-la.img`

构建：

```bash
docker exec seaos-la bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'
```

运行：

```bash
docker exec seaos-la bash -lc 'cd /workspace && timeout 5400 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_musl_next.log 2>&1'; echo exit=$?
```

扫描：

```bash
docker exec seaos-la bash -lc 'grep -a -n "^FAIL \|\[SEGV\]\|failed:\|UNKNOWN\|unknown syscall\|trap:\|panic\|Function not implemented\|Interrupted system call\|No such file\|argument expected\|end: fail\|test fail\|test timeout\|exec fail\|fork fail\|Fork failed\|Operation not permitted\|Broken pipe" /tmp/seaos_la_musl_next.log | tail -260'
docker exec seaos-la bash -lc 'grep -a -n "#### OS COMP TEST GROUP START\|#### OS COMP TEST GROUP END\|run /musl/\|cyclictest\|iozone\|lmbench\|ltp" /tmp/seaos_la_musl_next.log'
```

## 当前状态

当前 `src/user/initcode_la.c` 应保持完整 `/musl` 扫描：

```text
run_test_entries("/musl")
```

当前 clean 组为：

- `libcbench-musl`
- `libctest-musl`
- `unixbench-musl`
- `busybox-musl`
- `netperf-musl`
- `iperf-musl`
- `iozone-musl`
- `lua-musl`
- `basic-musl`
- `lmbench-musl`
- `cyclictest-musl`

剩余未 clean 组为：

- `ltp-musl`

关键最新证据：

- `/tmp/seaos_la_cyclictest_ticktime_clean1.log`：`cyclictest-musl` 到达
  GROUP END/shutdown，四个 cyclictest 子项和 `kill hackbench` 均 success，
  广义失败扫描为空，所以 clean。
- `/tmp/seaos_la_iozone_mem_budget3.log`：`iozone-musl` 到达 GROUP END 和
  shutdown，硬失败扫描为空，所以 clean。
- `/tmp/seaos_la_ltp_after_bind05_fix1.log`：focused LTP 顺序中
  `bind01`-`bind05` 均真实 TPASS/ret 0；`ltp-musl` 仍有 wrapper
  `FAIL LTP CASE`、`asapi_01` `hopopt`、`bind06` namespace-config TCONF
  和后续 proc/cgroup ABI/environment blockers。
- `/tmp/seaos_la_lmbench_latfs_sparse1.log`：direct `lat_fs /var/tmp`
  通过，证明 sparse-zero memfs hole 修复有效。
- `/tmp/seaos_la_lmbench_execshare2.log`：官方 focused lmbench 到达
  GROUP END，`lat_ctx` 覆盖 2/4/8/16/24/32/64/96，广义失败扫描为空。

重要细节：

- `pthread_cancel` 是历史问题，当前 libctest-musl 已有干净证据；不要从
  pthread_cancel 或 libctest-only 入口开始，除非新鲜扫描证明回归。
- `GROUP END` 和 `test sucess` 仍只是 wrapper 标记；必须检查组内真实输出。
- QEMU 命令即便内核已打印 shutdown，也可能因宿主机超时而以 `124` 退出。
  要检查日志本身，而不只是命令退出码。

## 已完成工作

### ABI 与 exec

- LoongArch syscall ABI 在 `src/kernel/loongarch/syscall.c` 中实现。
- `exec` 现在替换活动 trap frame 并通过常规 syscall 出口返回，而不是在 syscall 内部直接跳到用户态。
- PT_INTERP 加载与 musl 动态链接器路径回退已存在。
- `exec` 重置信号状态。

### 信号与线程

- `rt_sigaction` 解析 Linux 内核 ABI 布局。
- 信号投递构建用户信号帧，包含保存的寄存器、siginfo、ucontext、保存的 mask 与 trampoline。
- `rt_sigreturn` 从信号帧/ucontext 路径恢复用户态。
- `clone(CLONE_VM)` 与 `CLONE_SETTLS` 已存在，用于 musl pthreads。
- 线程退出时 `clear_child_tid` 被清零并通过 futex 唤醒。

### Futex

- 支持 `WAIT`、`WAKE`、`REQUEUE`、`CMP_REQUEUE`、`WAIT_BITSET`、`WAKE_BITSET`。
- 带超时的等待使用调度器 deadline。
- 带超时的 futex 修复了 `pthread_cond_smasher` 与 `sem_init`。

### FD、stat、time、resource limit 修复

- FD 表大小为 128。
- fd 条目记录 close-on-exec 与 nonblocking 状态。
- 改进了 `fcntl`、`dup`、`dup3`、`pipe2`、`socket`、`accept4`。
- `/dev/null` 与 `/dev/zero` 以简单设备形式存在。
- 为 musl 测试调整了 `fstat`、`newfstatat`、`statx`、`utimensat`。
- `RLIMIT_NOFILE` 是进程局部的，fork/clone 时继承。

### 其他基础工作

- memfs 支持更大的可写文件、truncate、路径查找、时间戳。
- 存在 loopback TCP/UDP socket 层。
- 存在 `pselect6`/`ppoll` 的 readiness 基础实现。
- `mmap` 支持匿名与 MAP_PRIVATE 文件映射。

## 后续工作

### P0：LTP post-ancillary blockers

`cyclictest-musl` 已在 `/tmp/seaos_la_cyclictest_ticktime_clean1.log` 中
clean。当前 P0 只剩 `ltp-musl`，从
`/tmp/seaos_la_ltp_enosys_only1.log` 后的真实缺口开始：

- `hopopt` libc-table boundary
- `bind06` namespace config TCONF
- cgroup controller/helper gaps after pid_max/self-mounts/rmdir/killall and
  optional fd-creation `UNKNOWN` cleanup

不要再从 cyclictest STRESS_P8、socketpair ENFILE/readyfds、半截 COW、
direct highmem/pmem、`LA_NFD=256`、pressure `LA_TIME_SLICE=1` 或
512-byte stream window 调参开始，除非新鲜日志证明 cyclictest 回归。

### Closed：lmbench 默认 `lat_fs /var/tmp` 与 `lat_ctx 96`

`lat_fs /var/tmp` 已由 sparse-zero memfs hole 语义推进；最终
`lat_ctx 96` 的 fork 深拷贝 OOM 已由只读 ELF 段 fork-share 推进。
`/tmp/seaos_la_lmbench_execshare2.log` 是当前 clean 证据；后续不要再从
旧 `lat_fs`、`Hello` shell corruption、`lat_sig catch/prot`、
`lat_pipe` 或 `lat_ctx 96` blocker 开始，除非新鲜完整扫描证明回归。

### P1：LTP wrapper caveat and remaining ABI work

若回到 `ltp-musl`，从 `/tmp/seaos_la_ltp_enosys_only1.log` 后的真实缺口开始：

- `hopopt` libc-table boundary
- `bind06` namespace config TCONF
- cgroup controller/helper gaps after pid_max/self-mounts/rmdir/killall

2026-06-27 continuation audit：当前复核没有新增 clean 大组，`/musl` 仍为
11/12 clean。容器内 `make build-la && cp target/loongarch/kernel-la.elf kernel-la`
通过，完整 `/musl` 入口和 `TST_TIMEOUT=-1` 保持不变，保护路径无 diff。
只读 `debugfs -R "cat /musl/ltp_testcode.sh" sdcard-la.img` 确认官方 wrapper
无条件打印 `FAIL LTP CASE ... : $ret`，即使 ret 为 0。只读
`debugfs -R "cat /musl/ltp/testcases/bin/cgroup_regression_3_1.sh" sdcard-la.img`
确认该 helper 需要路径参数；被 wrapper 无参数直接执行时会进入无限
`mkdir/rmdir` 循环类路径。`/tmp/seaos_la_ltp_enosys_only1.log` 中当前真实
阻塞仍为 `asapi_01` `hopopt` TFAIL、cgroup helper `must call tst_run` /
`controller not defined` / `Number of subgroups must be possitive integer`；
`bind06` 和 cgroup controller/core 项主要是 TCONF。按当前硬约束，不得通过
改官方脚本、跳过 helper、抑制 `FAIL LTP CASE` 或伪造输出把 `ltp-musl`
计为 clean；继续工作只能减少真实非零/TFAIL/TBROK。

`hopopt` protocol-0 lookup remains a TFAIL, but official LTP checks
`getprotobyname("hopopt")->p_proto == 0` and this musl's built-in protocol
table names protocol 0 as `ip` with no `hopopt` alias. It does not read the
initcode-created `/etc/protocols`, so this is not a kernel-stub fix target
unless libc/test binaries/output become allowed, which they currently are not.

不要再从 `fchmodat/fchownat/setpgid/setuid` setup ENOSYS、`abort01`、
`accept01/02/03`、`symlinkat(36)` ENOSYS、旧 `access02 file_x` fault、
`access01` result accounting、`access04` errno、`adjtimex(171)` ENOSYS
或 AF_ALG `EINVAL` 开始，除非新鲜扫描证明回归。

### P3：更新文档/状态表

在拿到真实的通过证据后，更新：

- `CLAUDE.md`
- `la-current.md`
- 若 syscall 状态变化则更新 `docs/SYSCALL_STATUS.md`
- 若语义变化则更新 `docs/DECISIONS.md`

## 风险

- 信号帧 ABI 的 bug 可能让取消看起来工作正常，却破坏后续的信号测试。保持诊断打印最小化，修好后立刻删掉。
- futex 上的捷径可能让一个 pthread 测试通过、另一个破坏。倾向真实 Linux 兼容的返回行为：被信号中断返回 `-EINTR`，只有真正超时才返回 `-ETIMEDOUT`，正常唤醒才返回 `0`。
- 修改 `exec`、trap 返回或页表/TLB 代码可能重新引入 ADEF 类的错误。保持这些改动尽量小。
- 串口日志在 QEMU 下很慢。使用带守护条件的日志，修好后立刻删掉。

## 交给队友 Agent 的提示词

在新的 Agent 会话中使用以下提示词：

```text
你在 /home/addaswsw/project/OS/oskernel2025-seaos 中开发 SeaOS LoongArch。
先执行 ls -la 并说明目录扫描结果，然后阅读 AGENTS.md、CLAUDE.md、
la-current.md、la-task.md、docs/LOONGARCH_MUSL_HANDOFF.md。

目标是让 /musl 的 12 个大测试组真正通过。如果任何真实子测试仍然报告
FAIL、[SEGV]、panic、trap 失败、unknown syscall、Function not implemented、
Interrupted system call、end: fail、test fail、test timeout、Fork failed 或
Broken pipe，不能把 "test sucess" 或 "GROUP END" 这类包装层字符串当作通过。

使用官方 Docker 容器 seaos-la 和官方 QEMU
/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64。不修改 QEMU 版本、
测试镜像、官方测试或测试二进制。

当前 src/user/initcode_la.c 应保持完整 /musl 扫描：run_test_entries("/musl")。
当前 clean 组为 libcbench-musl、libctest-musl、unixbench-musl、busybox-musl、
cyclictest-musl、netperf-musl、iperf-musl、iozone-musl、lua-musl、basic-musl、
lmbench-musl。
剩余未 clean 组仅为 ltp-musl。

下一步只处理 LTP。从 /tmp/seaos_la_ltp_enosys_only1.log 后的
hopopt libc-table boundary、bind06 namespace-config TCONF，以及
cgroup controller/helper 缺口开始；
不要再从 ar01.sh、asapi 早期 RAW socket
EINVAL、IPV6_CHECKSUM offset 语义、ICMP6_FILTER/raw receive timeout、
sendmsg ENOSYS、IPV6_RECVPKTINFO receive 或 IPv6 ancillary receive options
开始，也不要再从 arping01.sh、password/keyctl helper、direct brk01 或
bind01-bind05、capget/capset、pid_max/self-mounts、rmdir/killall、
optional fd-creation UNKNOWN 开始，
除非新鲜日志证明回归。
hopopt protocol-0 lookup 已定位为当前 musl 内置协议表边界；
不要从 pthread_cancel、libctest setup、abort01、accept01/02/03、
access01/access04、adjtimex、AF_ALG、cyclictest STRESS_P8 或旧 access02
file_x fault 开始，除非新鲜完整扫描证明这些点回归。cyclictest 已由
/tmp/seaos_la_cyclictest_ticktime_clean1.log 证明 clean；iozone 已由
/tmp/seaos_la_iozone_mem_budget3.log 证明 clean；lmbench 已由
/tmp/seaos_la_lmbench_execshare2.log 证明 clean。

使用 apply_patch 做源码编辑。文件保持 UTF-8。不隐藏失败、不伪造测试输出。
LoongArch 改动尽量隔离，除非确实需要改公共文件，若改了需注明 RISC-V 风险。
```
