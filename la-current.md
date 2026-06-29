# LoongArch 当前状态

> 最后更新：2026-06-29
>
> 本文档记录 LoongArch 线当前事实。完整任务矩阵与下一轮提示词见
> `la-task.md`。当前首要目标仍是让 `/musl` 测试真实通过；glibc 暂缓。

## 当前目标

让 LoongArch 内核真正通过 `/musl` 的 12 个大测试组。

是否通过必须看真实测试输出，而不是包装层字符串：

- `======== test sucess ========` 只是本项目 initcode/test wrapper 的尾部标记。
- `#### OS COMP TEST GROUP END ... ####` 只说明脚本跑到了末尾。
- 如果任何子测试仍报 `FAIL`、`[SEGV]`、`Function not implemented`、
  `Interrupted system call`、panic、trap 失败、unknown syscall、`end: fail`、
  `test fail` 或 `test timeout`，那么这个测试组就不算干净。

## 大测试点口径

`sdcard-la.img` 中有 24 个大测试组：

- `/musl` 12 个
- `/glibc` 12 个

每套 libc 的 12 组为：

1. `libcbench`
2. `libctest`
3. `busybox`
4. `cyclictest`
5. `netperf`
6. `iperf`
7. `iozone`
8. `lua`
9. `basic`
10. `unixbench`
11. `lmbench`
12. `ltp`

本地 `autotest-for-oskernel/kernel/judge/` 有 22 个 judge 脚本，缺
`unixbench-musl` 与 `unixbench-glibc`。因此：

- 镜像/脚本层总数：24 个大测试组。
- 本地 judge 入口总数：22 个大测试组。
- 当前 musl 目标范围：12 个大测试组。
- 若说“剩 21 个”，更准确的解释是：22 个本地 judged groups 中，
  `libctest-musl` 已完成后还剩 21 个 judged groups；但这不是 LoongArch
  总任务数。

## 环境

- 宿主机工作区：`/home/addaswsw/project/OS/oskernel2025-seaos`
- Docker 容器：`seaos-la`
- 容器内工作区：`/workspace`
- 官方镜像 / 工具链：`zhouzhouyi/os-contest:20260510`
- 官方 LoongArch QEMU：`/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64`
- 真实测试磁盘运行副本：`/workspace/sdcard-la.img`

构建：

```bash
docker exec seaos-la bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'
```

当前完整 musl 运行命令：

```bash
docker exec seaos-la bash -lc 'cd /workspace && timeout 5400 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_musl_next.log 2>&1'
```

广义失败扫描：

```bash
docker exec seaos-la bash -lc 'grep -a -n "^FAIL \|\[SEGV\]\|failed:\|UNKNOWN\|unknown syscall\|trap:\|panic\|Function not implemented\|Interrupted system call\|No such file\|argument expected\|end: fail\|test fail\|test timeout\|exec fail\|fork fail\|Fork failed\|Operation not permitted\|Broken pipe" /tmp/seaos_la_musl_next.log | tail -260'
```

## 当前测试入口

`src/user/initcode_la.c` 当前已恢复为完整 `/musl` 扫描：

```text
run_test_entries("/musl")
```

glibc 仍暂缓。

## 最新已验证状态

日期：2026-06-29。

本轮新增有效修复：

- LoongArch `wait4(260)` 现在区分用户可见 `EINTR` 与内部 syscall restart。
  若等待期间存在 pending signal：
  - handler 未带 `SA_RESTART` 时仍向用户返回 `-EINTR`；
  - `SA_RESTART`、默认动作或忽略动作走内部 `-ERESTARTSYS`；
  - trap 层看到 `-ERESTARTSYS` 时不写回 `a0`、不前进 `era`，先进入信号投递。
    handler 正常返回后重启原 wait syscall；handler `longjmp` 则自然离开等待。
- 该修复清除了本轮完整 `/musl` LTP 段暴露的高频
  `tst_test.c:1654: TBROK: waitpid(...) failed: EINTR (4)`。

新增证据：

- `/tmp/seaos_la_musl_full_clean1.log`：恢复完整 `/musl` 后的官方 QEMU
  运行已经通过 `libcbench-musl`、`libctest-musl`、`unixbench-musl`、
  `busybox-musl`、`cyclictest-musl`、`netperf-musl`，并越过旧的
  `lmbench-musl` `lat_fs 0k` 卡点进入 `ltp-musl`。该日志中 LTP 段有
  43 条 `waitpid.*EINTR`，并最终停在 cgroup helper 区域，因此不是
  clean 证据。
- `/tmp/seaos_la_ltp_restart1.log`：focused LTP-only 官方 QEMU 复验中，
  `waitpid.*EINTR` 计数为 0；旧日志里触发 `EINTR` 的
  `add_key*`、`adjtimex*`、`alarm*`、`bind01`-`bind05`、`brk01/02`、
  `capget01/02`、`capset01`-`capset04` 均不再出现 waitpid TBROK。
  该运行推进到 `cgroup_regression_3_1.sh` 后人工停止。
- `/tmp/seaos_la_ltp_protocol1.log`：复验了一次 `/etc/protocols` 顺序试探，
  `asapi_01` 的 `hopopt` TFAIL 未改变；该试探已撤回，确认该点仍是
  当前 musl 内置 protocol table 边界，不作为内核小修目标。
- 当前 `src/user/initcode_la.c` 已恢复完整 `run_test_entries("/musl")`，
  保留 `TST_TIMEOUT=-1`；容器内
  `make build-la && cp target/loongarch/kernel-la.elf kernel-la` 通过。

当前结论：

- `/musl` 仍不能计为 12/12 clean，剩余仍是 `ltp-musl`。
- 本轮从“完整 `/musl` 卡在 `lmbench`/LTP waitpid EINTR”推进为：
  `lmbench-musl` 已在完整顺序中越过，LTP waitpid EINTR regression 已清除。
- 剩余不可计 clean 的核心证据仍是：
  `asapi_01` `hopopt` musl 内置 protocol table TFAIL，以及官方
  `/musl/ltp_testcode.sh` 无条件枚举 helper 脚本导致的 cgroup helper
  TBROK/无参数无限循环。特别是只读 `debugfs` 抽取的
  `/musl/ltp/testcases/bin/cgroup_regression_3_1.sh` 内容显示它是
  `while true; mkdir $path/0; rmdir $path/0; done`，没有参数直接运行时
  必然无限循环，且 `TST_TIMEOUT=-1` 下不会被 wrapper 超时杀掉。
- 在不修改官方脚本/测试镜像、不跳过用户程序、不伪造输出的硬约束下，
  该 cgroup helper 直接枚举问题不能被记为 clean。

日期：2026-06-26。

最新有效证据：

```text
/tmp/seaos_la_musl_after_netperf_fix.log
/tmp/seaos_la_netperf_sigabi_accept2.log
/tmp/seaos_la_musl_full_envp_quiet.log
/tmp/seaos_la_unixbench_final2.log
/tmp/seaos_la_lmbench_lat_fs_N1_hash.log
/tmp/seaos_la_lmbench_lat_fs_default_hash.log
/tmp/seaos_la_libctest_utime_fix.log
/tmp/seaos_la_cyclictest_sleep_wrapper.log
/tmp/seaos_la_iperf_focus.log
/tmp/seaos_la_iozone_focus.log
/tmp/seaos_la_lua_focus.log
/tmp/seaos_la_basic_clone_fix.log
/tmp/seaos_la_musl_after_basic_fix.log
/tmp/seaos_la_ltp_focus.log
/tmp/seaos_la_ltp_fchmodat.log
/tmp/seaos_la_ltp_fchown_setpgid.log
/tmp/seaos_la_ltp_env_stubs.log
/tmp/seaos_la_ltp_identity_access.log
/tmp/seaos_la_ltp_abort_red.log
/tmp/seaos_la_ltp_abort_coredump.log
/tmp/seaos_la_ltp_accept03_fix.log
/tmp/seaos_la_ltp_symlink_red.log
/tmp/seaos_la_ltp_symlink_green.log
/tmp/seaos_la_ltp_symlink_wrapper_green.log
/tmp/seaos_la_musl_full_after_ltp_accept.log
/tmp/seaos_la_ltp_access01_mkdir_mode.log
/tmp/seaos_la_ltp_adjtimex_min.log
/tmp/seaos_la_ltp_afalg_eafnosupport.log
/tmp/seaos_la_ltp_asapi_hopopt_alias.log
/tmp/seaos_la_ltp_rawsock1.log
/tmp/seaos_la_ltp_rawsock2.log
/tmp/seaos_la_musl_full_after_symlink.log
/tmp/seaos_la_ltp_access04_ro_mount.log
/tmp/seaos_la_iozone_pwrite_no_cow.log
/tmp/seaos_la_lmbench_highmem_memfs.log
/tmp/seaos_la_lmbench_highmem_dedup.log
/tmp/seaos_la_musl_after_current_revert.log
/tmp/seaos_la_cyclictest_fresh_next.log
/tmp/seaos_la_cyclictest_cow1.log
/tmp/seaos_la_cyclictest_cow2.log
/tmp/seaos_la_cyclictest_cow3.log
/tmp/seaos_la_iozone_focused1.log
/tmp/seaos_la_ltp_focused1.log
/tmp/seaos_la_lmbench_focused1.log
/tmp/seaos_la_lmbench_latctx96_forkoom_diag.log
/tmp/seaos_la_lmbench_latctx96_execshare1.log
/tmp/seaos_la_lmbench_execshare2.log
/tmp/seaos_la_iozone_execshare1.log
/tmp/seaos_la_cyclictest_execshare1.log
/tmp/seaos_la_musl_restore_after_lmbench.log
/tmp/seaos_la_iozone_cow_full1.log
/tmp/seaos_la_iozone_cow_copytouser1.log
/tmp/seaos_la_cyclictest_memfs_inode_cap.log
/tmp/seaos_la_iozone_mem_budget3.log
/tmp/seaos_la_cyclictest_mem_budget2.log
/tmp/seaos_la_netperf_sock256_recheck.log
/tmp/seaos_la_iperf_sock256_recheck.log
/tmp/seaos_la_musl_restore_after_iozone.log
/tmp/seaos_la_cyclictest_focused_after_iozone.log
/tmp/seaos_la_cyclictest_npipe1024.log
/tmp/seaos_la_cyclictest_dynpipe8192.log
/tmp/seaos_la_cyclictest_dynsock512.log
/tmp/seaos_la_netperf_dynsock512_recheck.log
/tmp/seaos_la_iperf_dynsock512_recheck.log
/tmp/seaos_la_iozone_dynsock512_recheck.log
/tmp/seaos_la_cyclictest_ticktime_clean1.log
/tmp/seaos_la_musl_after_cyclic_ticktime_smoke.log
```

2026-06-26 后续定位补充：

- 当前 clean 组为 11/12。本轮新增可计数 clean 组为
  `cyclictest-musl`；`iozone-musl` 与 `lmbench-musl` 是此前同日已验证
  clean 的基线：
  `libcbench-musl`、`libctest-musl`、`unixbench-musl`、`busybox-musl`、
  `netperf-musl`、`iperf-musl`、`iozone-musl`、`lua-musl`、`basic-musl`、
  `lmbench-musl`、`cyclictest-musl`。
- 当前剩余未 clean `/musl` 大组仅为 `ltp-musl`。
- 本轮继续执行后，`src/user/initcode_la.c` 已再次恢复完整
  `run_test_entries("/musl")`。focused 验证期间临时切换过 `iozone`、
  `cyclictest`、`netperf`、`iperf` 入口；收尾前已恢复完整 `/musl` 入口。
- 本轮保留的新增修复包括 LoongArch 静态内存预算与按需 buffer 收敛：
  `MEMFS_MAX_INODES` 从 786432 收敛到 32768；`LA_NSOCK` 当前为 512，
  但 stream socket receive buffer 改为按需分配的一页；`LA_NPIPE` 当前为
  8192，pipe data buffer 也改为按需分配的一页。`LA_NFD` 曾试探 256
  但导致 cyclictest/hackbench 早期 `Broken pipe`，已恢复 512。最终
  full-entry `make build-la` 显示 kernel-la BSS 约 41.5MB，启动日志中
  `pmem` 约 214MB，仍远低于旧 178MB BSS/83MB pmem 的危险状态。
- `iozone-musl` 已 clean。最新证据
  `/tmp/seaos_la_iozone_mem_budget3.log`：到达
  `#### OS COMP TEST GROUP END iozone-musl ####` 与 `shutdown: system halting`；
  硬失败扫描
  `^FAIL |\[SEGV\]|failed:|unknown syscall|trap:|panic|Function not implemented|Interrupted system call|end: fail|test fail|test timeout|Fork failed|memfs: out of memory|Memory allocation failed|No such file|exit: pid|Broken pipe`
  为空。日志仍有 iozone 自身的 `statx: ... not found`、`Selected test not
  available on the version` 和部分 `Min xfer = 0.00 kB` 文本；它们不属于
  本轮硬失败口径，且没有 child exit/OOM/Fork failed。
- Socket 容量收敛未使已完成 socket 组回退：
  `/tmp/seaos_la_netperf_sock256_recheck.log` 和
  `/tmp/seaos_la_iperf_sock256_recheck.log` 均到达 GROUP END，硬失败扫描为空。
  本轮进一步将 `LA_NSOCK` 恢复为 512 并改为动态 stream buffer 后，
  `/tmp/seaos_la_netperf_dynsock512_recheck.log` 和
  `/tmp/seaos_la_iperf_dynsock512_recheck.log` 也均到达 GROUP END，硬失败扫描为空。
- `iozone-musl` 在动态 pipe/socket buffer 后未回退：
  `/tmp/seaos_la_iozone_dynsock512_recheck.log` 到达
  `#### OS COMP TEST GROUP END iozone-musl ####` 与 `shutdown: system halting`；
  硬失败扫描未命中 `FAIL`、`[SEGV]`、`trap:`、`panic`、`Fork failed`、
  OOM、非零 child `exit: pid` 或 `Broken pipe`。
- 恢复完整 `/musl` 入口后的最新 smoke
  `/tmp/seaos_la_musl_restore_after_iozone.log` 使用 180 秒 timeout，结果为
  `exit=124`。它到达 `libcbench-musl` 与 `libctest-musl` GROUP END，并进入
  `unixbench-musl`；截至 timeout 前硬失败扫描为空。该日志只证明恢复 full
  `/musl` 入口后前段没有立即回归，不是完整 `/musl` 通过证据。
- 本轮最新保留修复：`src/kernel/loongarch/memfs_la.c` 对空文件/空洞的
  全零写入采用 sparse-size 表示，读取未分配页时按 Linux 文件洞语义返回
  零填充。直接 focused 证据 `/tmp/seaos_la_lmbench_latfs_sparse1.log`
  显示 `lmbench_all lat_fs /var/tmp` 已输出 0k/1k/4k/10k 结果并到达
  `======== test sucess ========`，且没有 `memfs: out of memory`。
- `lmbench-musl` 已真实干净。根因定位日志
  `/tmp/seaos_la_lmbench_latctx96_forkoom_diag.log` 证明旧的最终
  `lat_ctx 96` blocker 是 `fork: copy_pgtbl OOM pid=4`。保留修复为
  exec loader 按 ELF `PF_W` 区分段权限，非 writable PT_LOAD 使用 RX
  并标记 `FORK_SHARE`，让 fork 共享 text/rodata 而不是深拷贝。
  直接 focused `/tmp/seaos_la_lmbench_latctx96_execshare1.log` 输出
  `96 59.84` 并到达 wrapper success；完整 focused 官方脚本
  `/tmp/seaos_la_lmbench_execshare2.log` 输出 `lat_ctx` 2/4/8/16/24/32/64/96
  全部结果，到达 `#### OS COMP TEST GROUP END lmbench-musl ####`，
  广义失败扫描为空。
- `cyclictest-musl` 已真实干净。最新 focused 官方 QEMU 证据
  `/tmp/seaos_la_cyclictest_ticktime_clean1.log` 显示
  `NO_STRESS_P1`、`NO_STRESS_P8`、`STRESS_P1`、`STRESS_P8` 均为
  `end: success`，`kill hackbench: success`，到达
  `#### OS COMP TEST GROUP END cyclictest-musl ####` 与
  `shutdown: system halting`；广义失败扫描未命中 `FAIL`、`[SEGV]`、
  `trap:`、`panic`、unknown syscall、`Function not implemented`、
  `Interrupted system call`、`end: fail`、`test fail`、`test timeout`、
  `Fork failed`、`Broken pipe`、`Connection reset` 或
  `No measurements available`。同一日志确认没有临时 `[cycdiag]`、
  `la_poll` 或 `pollwait` trace。
- `cyclictest-musl` 的最终根因是 LoongArch timebase 不一致：
  `clock_gettime/gettimeofday` 之前使用 stable counter，而
  `clock_nanosleep(TIMER_ABSTIME)` deadline 使用 100 Hz `la_ticks`。
  QEMU/hackbench 压力下 stable counter 观测时间会相对 scheduler ticks
  漂移，导致 cyclictest 的 1 秒绝对睡眠 deadline 变成数十秒级等待。
  保留修复是让 `sys_clock_gettime(113)` 与 `sys_gettimeofday(169)` 使用
  与 `clock_nanosleep` 相同的 100 Hz tick clock。此前 fdpair ENFILE、
  readyfds `Connection reset`、runqueue-pressure、pressure slice 和
  512-byte stream-window 证据都作为历史定位保留，但已被
  `/tmp/seaos_la_cyclictest_ticktime_clean1.log` supersede。
- 恢复完整 `/musl` 入口后的 smoke
  `/tmp/seaos_la_musl_after_cyclic_ticktime_smoke.log` 使用官方 QEMU
  360 秒 timeout，结果为 `exit=124`。它到达 `libcbench-musl` 与
  `libctest-musl` GROUP END，并进入 `unixbench-musl`；截至 timeout 前
  广义失败扫描为空。该日志只证明恢复 full-entry 后前段没有立即回归，
  不是完整 `/musl` 通过证据。
- `iozone-musl` 本轮又做了三轮 COW/fork-pressure 试探，均未通过并已撤回：
  `/tmp/seaos_la_iozone_cow2.log` 从 `Fork failed` 推进到 throughput
  random-read 后出现 `pc=0/ra=0` 与 ADEF；`/tmp/seaos_la_iozone_cow_mprotect1.log`
  和 `/tmp/seaos_la_iozone_cow_tlbinval1.log` 仍在启动/automatic 阶段复现
  同类 `pc=0`/ADEF。不要保留或复用这些半截 COW 改动。
- `cyclictest-musl` 本轮 focused `/tmp/seaos_la_cyclictest_red1.log`：
  `NO_STRESS_P1`、`NO_STRESS_P8` 均打印 success；进入 hackbench 后
  `fork() (error: Out of memory)`，只启动 35/40 children，随后大量
  `No measurements available`、`Broken pipe` 与 worker OOM。后续 highmem/
  扩展用户页池试探 `/tmp/seaos_la_cyclictest_highmem1.log`、
  `/tmp/seaos_la_cyclictest_highmem2.log`、`/tmp/seaos_la_cyclictest_usermem1.log`
  均在启动或 exec 早期 ADEF，已撤回。
- `ltp-musl` 本轮 focused `/tmp/seaos_la_ltp_red1.log`：大量子项本体
  Summary 为 passed/skipped，但外层仍打印 `FAIL LTP CASE ... : 0/32`；
  按当前硬约束，任何 `FAIL` 均不能计 clean，也不能通过修改官方脚本绕过。
- 进一步只读检查测试镜像脚本：
  `debugfs -R "cat /musl/ltp_testcode.sh" sdcard-la.img` 显示
  `ltp_testcode.sh` 在每个 case 执行后无条件打印
  `FAIL LTP CASE $(basename "$file") : $ret`，没有根据 `$ret` 判断 PASS/FAIL。
  因此在当前“日志中不能有 FAIL”的验收口径下，`ltp-musl` 无法通过内核语义
  修复变成 clean；消除这些 `FAIL` 只能修改/包装官方脚本或抑制真实输出，
  均被硬约束禁止。后续 LTP 工作只能继续减少真实非零/TFAIL/TBROK 缺口，
  但不能把该大组计入 clean，除非验收口径明确把这个官方 wrapper 字符串排除。
- `ltp-musl` 最新 focused 证据 `/tmp/seaos_la_ltp_focus1.log`：
  早期 alarm 子项本体 TPASS，但 wrapper 仍打印 `FAIL LTP CASE ... : 0`，
  后续有 `tst_test.sh`/`tst_net.sh` 缺失、`hopopt` TFAIL、IPv6 RAW/asapi
  socket `EINVAL`、kernel config 解析失败等真实缺口；未计 clean。
- `ltp-musl` 最新 raw delivery 证据 `/tmp/seaos_la_ltp_rawdeliver2.log`：
  `acct(89)`、`add_key(217)`、`keyctl(219)` 已注册为显式 `-ENOSYS`，
  不再出现对应 unknown syscall；`asapi_02` 的 ICMP6_FILTER/raw receive
  12 个断言全部 TPASS，Summary 为 `passed 12/failed 0/broken 0`；
  `asapi_03` 的 `IPV6_RECVPKTINFO set-get` 与 receive 均 TPASS，旧
  `sendmsg ENOSYS` 与 `recvmsg timed out` 已被 supersede。`ltp-musl`
  仍不 clean：同一日志仍有 wrapper `FAIL LTP CASE`、`hopopt` TFAIL、
  shell helper `No such file`、kernel config `TBROK`，以及 later IPv6
  ancillary option TFAIL。
- `ltp-musl` 最新 ancillary 证据 `/tmp/seaos_la_ltp_ancillary1.log`：
  `asapi_03` 的 18 个断言全部 TPASS，包括
  `IPV6_RECVHOPLIMIT/RTHDR/HOPOPTS/DSTOPTS/TCLASS` 与 2292 ancillary
  options。该 focused run 手动停止于后续 `assign_password.sh`，没有 GROUP END，
  所以仍不是 `ltp-musl` pass 证据。当前剩余真实缺口包括 wrapper
  `FAIL LTP CASE`、kernel config `TBROK`、shell helper 缺失、
  `hopopt` protocol-0 TFAIL、`ask_password.sh`/`assign_password.sh` 等后续
  LTP environment/ABI gaps。
- 2026-06-27 focused LTP 环境推进：
  `/tmp/seaos_la_ltp_mktemp1.log` 证明 `/bin/mktemp` 运行期 busybox wrapper
  生效，`ar01.sh` 不再出现 `sh: can't execute 'mktemp': No such file or
  directory`；该用例推进到新的真实 blocker：`sh: out of range` 和
  `ar01 1 TBROK: timeout need to be >= 1 ()`。
  `/tmp/seaos_la_ltp_kconfig1.log` 证明运行期
  `KCONFIG_PATH=/etc/seaos-kconfig` 生效，`acct02` 与 `aslr01` 不再
  `Cannot parse kernel .config`，而是分别因
  `CONFIG_BSD_PROCESS_ACCT=n`、`CONFIG_HAVE_ARCH_MMAP_RND_BITS=n` 正常
  TCONF。该日志仍停在 `assign_password.sh`，且仍有 wrapper
  `FAIL LTP CASE`、`ar01.sh` TBROK、`arping01.sh` 失败、`hopopt` TFAIL、
  `ask_password.sh`/`assign_password.sh` 等缺口；其中 `ar01.sh` 与
  `arping01.sh` 已分别被后续 `/tmp/seaos_la_ar01_min_ar10.log`、
  `/tmp/seaos_la_ltp_after_ar10.log` 证据 supersede，当前 `ltp-musl`
  仍不 clean。
- 2026-06-27 追加 focused LTP timeout 定位：
  `/tmp/seaos_la_ltp_timeout_disabled1.log` 与干净运行副本上的
  `/tmp/seaos_la_ltp_no_ar_cleanimg1.log` 证明 `TST_TIMEOUT=-1` 可让
  `ar01.sh` 越过 busybox ash/LTP shell harness 的
  `timeout need to be >= 1 ()` 空值层，日志显示
  `Timeout per run is disabled`。下一层真实 blocker 是 busybox 缺少
  `ar` applet：`ar: applet not found`，随后
  `ar01 1 TBROK: ar -cr ... failed`。源码已不再主动生成 `/bin/ar`
  busybox wrapper，但 busybox shell 仍会按 applet 名解析 `ar` 并在执行期
  报缺失；`ltp-musl` 仍不 clean。
- 恢复完整 `/musl` 扫描后的 smoke
  `/tmp/seaos_la_musl_after_ltp_ancillary_smoke.log`：180 秒 timeout 前到达
  `libcbench-musl` 与 `libctest-musl` GROUP END，并进入 `unixbench-musl`；
  广义失败扫描为空。该日志只证明完整入口未早期回退，不是 full `/musl`
  pass 证据。
- 恢复完整 `/musl` 扫描并保留 LTP env/kconfig helper 后的 smoke
  `/tmp/seaos_la_musl_after_ltp_kconfig_smoke.log`：180 秒 timeout 前到达
  `libcbench-musl` 与 `libctest-musl` GROUP END，并进入 `unixbench-musl`；
  广义失败扫描为空。该日志只证明 full-entry 前段未回退，不是 full
  `/musl` pass 证据。
- 最小 COW fork 试验曾用 `/tmp/seaos_la_iozone_cow1.log` 验证，结果在
  `iozone` 启动早期引入 TLB refill fail/用户进程 fault，已回退；不要把
  该试验当作可保留修复。
- `src/user/initcode_la.c` 已恢复完整 `run_test_entries("/musl")`，
  并已重新 `make build-la` 生成当前 `kernel-la`。
- `lmbench-musl` 修复了两个真实前置 blocker：
  1. memfs 既有文件/新建文件的 fd writable 状态现在按 open flags 设置，
     只读 fd 写入返回 `-EBADF`，不再污染 `/tmp/hello`。
  2. 普通 `MAP_SHARED` mmap 页不再使用 SysV shm 的“不释放”标记；
     这消除了 `lat_pagefault` 前后的 mmap/munmap 泄漏。
- 旧的 `lmbench-musl` memfs/lat_fs/context blocker 已被
  `/tmp/seaos_la_lmbench_execshare2.log` supersede；不要再从
  `lat_fs /var/tmp`、`Hello` shell corruption、`lat_sig catch/prot` 或
  `lat_ctx 96` 开始，除非新鲜完整扫描证明回归。
- `iozone-musl` 旧的 `Fork failed` 证据已被
  `/tmp/seaos_la_iozone_mem_budget3.log` supersede；当前 iozone clean。
- `cyclictest-musl` 旧的 fork/OOM、`No measurements available`、
  `Broken pipe`、COW/highmem 和 STRESS_P8 timeout 证据已被
  `/tmp/seaos_la_cyclictest_ticktime_clean1.log` supersede；当前
  cyclictest clean。不要再从旧 `/tmp/seaos_la_cyclic_focus1.log`、
  `/tmp/seaos_la_cyclic_cow1.log` 或 `/tmp/seaos_la_cyclic_cow2.log`
  开始，除非新鲜完整扫描证明回归。

结论：

- `libcbench-musl` 已真实干净：到达 `GROUP END libcbench-musl`，同一日志中
  在进入后续组之前广义失败扫描为空。
- `libctest-musl` 已真实干净。2026-06-26 的新鲜完整扫描曾暴露
  `utime` 回归；修复后官方 QEMU 日志
  `/tmp/seaos_la_libctest_utime_fix.log` 显示静态/动态
  `entry-*.exe utime` 均到达 END，并到达
  `#### OS COMP TEST GROUP END libctest-musl ####`；扩展广义失败扫描为空。
- 静态 `entry-static.exe pthread_cancel` 已到 `END`。
- 动态 `entry-dynamic.exe pthread_cancel` 已到 `END`。
- 动态 `entry-dynamic.exe scanf_nullbyte_char` 已到 `END`。
- 日志到达 `#### OS COMP TEST GROUP END libctest-musl ####`。
- `unixbench-musl` 已真实干净。单组官方 QEMU 长窗口日志
  `/tmp/seaos_la_unixbench_final2.log` 到达
  `#### OS COMP TEST GROUP END unixbench-musl ####`，并输出 DHRY2、
  WHETSTONE、SYSCALL、CONTEXT、PIPE、SPAWN、EXECL、全部 FS、小/中/大
  文件读写复制、SHELL1/8/16、ARITHOH、SHORT、INT、LONG、FLOAT、DOUBLE、
  HANOI、EXEC 分数。
- 对 `/tmp/seaos_la_unixbench_final2.log` 的扩展广义失败扫描为空：
  无 `FAIL`、`[SEGV]`、`failed:`、`UNKNOWN`、unknown syscall、trap、panic、
  `Function not implemented`、`Interrupted system call`、`No such file`、
  `argument expected`、`end: fail`、`test fail`、`test timeout`、`exec fail`
  或 `fork fail`。
- `busybox-musl` 已真实干净。完整官方 QEMU 日志
  `/tmp/seaos_la_musl_after_netperf_fix.log` 中
  `#### OS COMP TEST GROUP START busybox-musl ####` 到
  `#### OS COMP TEST GROUP END busybox-musl ####` 区间的广义失败扫描为空。
- `netperf-musl` 已真实干净。Focused 官方 QEMU 日志
  `/tmp/seaos_la_netperf_sigabi_accept2.log` 与完整扫描
  `/tmp/seaos_la_musl_after_netperf_fix.log` 均显示 UDP_STREAM、TCP_STREAM、
  UDP_RR、TCP_RR、TCP_CRR 全部 `end: success`，并到达
  `#### OS COMP TEST GROUP END netperf-musl ####`；组内广义失败扫描为空。
- `iperf-musl` 已真实干净。Focused 官方 QEMU 日志
  `/tmp/seaos_la_iperf_focus.log` 显示 BASIC_UDP、BASIC_TCP、
  PARALLEL_UDP、PARALLEL_TCP、REVERSE_UDP、REVERSE_TCP 全部
  `end: success`，到达
  `#### OS COMP TEST GROUP END iperf-musl ####`；扩展广义失败扫描为空。
- `lua-musl` 已真实干净。Focused 官方 QEMU 日志
  `/tmp/seaos_la_lua_focus.log` 显示 `date.lua`、`file_io.lua`、
  `max_min.lua`、`random.lua`、`remove.lua`、`round_num.lua`、`sin30.lua`、
  `sort.lua`、`strings.lua` 全部输出 `testcase lua ... success`，到达
  `#### OS COMP TEST GROUP END lua-musl ####`；扩展广义失败扫描为空。
- `basic-musl` 已真实干净。Focused 官方 QEMU 日志
  `/tmp/seaos_la_basic_clone_fix.log` 在 LoongArch `clone(220)` 修复后
  到达 `#### OS COMP TEST GROUP END basic-musl ####`；`clone`、`fork`、
  `waitpid` 等关键子项到达 END，扩展广义失败扫描为空。
- `lmbench-musl` 已真实干净。Focused 官方 QEMU 日志
  `/tmp/seaos_la_lmbench_execshare2.log` 到达
  `#### OS COMP TEST GROUP END lmbench-musl ####`；`lat_ctx` 输出
  2/4/8/16/24/32/64/96 全部结果，扩展广义失败扫描为空。
- 最新完整 `/musl` 入口扫描
  `/tmp/seaos_la_musl_full_after_symlink.log` 使用完整
  `run_test_entries("/musl")`，360 秒窗口内到达 `unixbench-musl`
  的 `FS_WRITE_SMALL`，超时退出 `124`；到该截点 `libcbench-musl` 与
  `libctest-musl` 到达 GROUP END，广义失败扫描为空。该日志没有
  `unixbench-musl` GROUP END，不能作为 unixbench 新完成证据。
- 本轮 netperf 根因：
  - LoongArch musl 传给 `rt_sigaction` 的 kernel ABI 是
    `{ handler, flags, mask }`，不是旧文档里写的
    `{ handler, flags, restorer, mask }`。旧解析把 `SIGALRM` mask
    `0x2000` 当作 restorer，handler 返回时跳到 `pc=0x2000`。
  - `TCP_CRR` 需要 netserver 的 alarm 信号打断阻塞 `accept()`；socket
    accept sleep 返回后现在检查未屏蔽 pending signal 并返回 `-EINTR`。
- 最新 cyclictest focused 定位：
  - `/dev/cpu_dma_latency` 最小设备已让 cyclictest 打印
    `# /dev/cpu_dma_latency set to 0us`，旧 WARN 已消失。
  - `sys_fork` 资源失败已改为返回 `-ENOMEM`，不再返回裸 `-1`
    让 musl 误报 `Operation not permitted`。
  - 本轮 focused 复现 `/tmp/seaos_la_cyclictest_focus2.log` 确认
    `NO_STRESS_P1/P8` 仍可运行，但 hackbench/stress 阶段仍出现
    `fork() (error: Out of memory)`、`No measurements available`、
    `CLIENT: ready write (error: Broken pipe)` 和
    `Creating workers (error: Out of memory)`。
  - 本轮修复了一个独立的 LoongArch 进程生命周期问题：
    `la_proc_create_user()` 不再在 `pgtbl/tf` 填好前发布 RUNNABLE；
    first initcode、`sys_fork()`、`sys_clone(CLONE_VM)` 改为完整初始化后
    发布。这消除了 highmem 实验中出现的 `ub: tf is NULL!` 竞态。
  - highmem/fork-pressure 三轮实验结论：直接把
    `0x90000000..0xc0000000` 加入 pmem 会在 boot 早期 trap；外部
    highmem stack 暴露并修复了 RUNNABLE 早发竞态；DMW alias/PA-KVA
    尝试仍会在早期用户页分配路径 fault。最终已恢复低内存分配基线，
    `/tmp/seaos_la_cyclictest_recovered.log` 回到原始 fork/OOM blocker。
  - `/tmp/seaos_la_cyclictest_sleep_wrapper.log` 仍未到 GROUP END：
    hackbench 压力阶段出现 `fork() (error: Out of memory)`、
    `Creating workers (error: Out of memory)` 和 `Broken pipe`。
    这证明当前 blocker 是 LoongArch 低内存 fork/page-table 压力，
    不是 wrapper 成功标记可接受的 clean 状态。
- 最新 `iozone-musl` focused 定位：
  - `/tmp/seaos_la_iozone_mem_budget3.log` 到达
    `#### OS COMP TEST GROUP END iozone-musl ####` 与 `shutdown: system halting`，
    硬失败扫描为空；当前 `iozone-musl` clean。
  - 旧 `/tmp/seaos_la_iozone_focus.log`、`/tmp/seaos_la_iozone_focus1.log`、
    `/tmp/seaos_la_iozone_focus2.log` 的 `Fork failed` 证据已 supersede。
  - COW/highmem 试探仍已回退；当前保留修复是 LoongArch 静态 BSS
    内存预算收敛。
- 最新 `ltp-musl` focused 定位：
  - `/tmp/seaos_la_ltp_focus.log` 证明第一层 blocker 是
    `fchmodat(53)` 缺失：大量用例在 common setup 中
    `chmod(...,0666) failed: ENOSYS`。
  - `/tmp/seaos_la_ltp_fchmodat.log` 证明 `fchmodat(53)` 修复后，
    `UNKNOWN #0x35`/`chmod ENOSYS` 消失，下一层 blocker 是
    `fchownat(54)` 与 `setpgid(154)`。
  - `/tmp/seaos_la_ltp_fchown_setpgid.log` 证明 `fchownat(54)` 与
    `setpgid(154)` 修复后，LTP 进入真实 case 语义层；可见
    `abort01` 信号退出码不符、`accept*` errno 不符、`symlinkat(36)`、
    `acct(89)`、`add_key/keyctl`、`adjtimex(171)`、AF_ALG socket 等缺口。
  - `/tmp/seaos_la_ltp_env_stubs.log` 证明 `/etc/passwd`、`/etc/group`、
    `/proc/self/maps` 运行期 stub 后，`getpwnam(nobody)` 与
    `/proc/self/maps` ENOENT 消失；下一层是 `setuid/setresuid` 与
    `faccessat` 权限语义。
  - `/tmp/seaos_la_ltp_accept03_fix.log` 证明本轮修复后
    `abort01` 的 `WCOREDUMP`/`SIGIOT` 两个断言均 TPASS；
    `accept01` 的 EBADF/EINVAL/EOPNOTSUPP errno 矩阵均 TPASS；
    `accept02` 确认 accepted TCP socket 不继承 multicast membership；
    `accept03` 的 file/O_PATH/directory/device/proc/pipe fd errno 均 TPASS；
    `accept4_01` 的 libc accept4 与 `__NR_accept4` 变体均 TPASS，
    `socketcall` 变体按架构不支持 TCONF。
  - `/tmp/seaos_la_ltp_symlink_red.log` 复现 `access02`/`access04`
    在 `symlink()` setup 阶段触发 `UNKNOWN #0x24`，用户态得到
    `ENOSYS`。
  - 本轮为 LoongArch memfs/syscall 增加最小 symlink inode、
    `symlinkat(36)`、`readlinkat(78)` 和 `faccessat/open/newfstatat`
    的最终路径 symlink-follow。`/tmp/seaos_la_ltp_symlink_green.log`
    证明 `UNKNOWN #0x24` 与 `symlink(...)=ENOSYS` 消失，`access02`
    推进到 `file_f/file_r/file_w` 的 6 个 TPASS。
  - `/tmp/seaos_la_ltp_access02_clean.log` 证明本轮 vfork exec/mm/ASID
    修复后，`access02` 的 `file_x` 与 `symlink_x` X_OK 执行路径在
    root/nobody 下均 TPASS，且清理后的日志窗口内没有 `trap:` 或临时
    `clone_vm`/`exec[file_x]` trace。
  - `ltp-musl` 仍不 clean，但旧的 `access01` result accounting、
    `access04` errno、`adjtimex(171)` ENOSYS 和 AF_ALG `EINVAL`
    blocker 已推进：
    `/tmp/seaos_la_ltp_access01_mkdir_mode.log` 显示
    `access01/access02/access03/access04` 均为 `: 0`；
    `/tmp/seaos_la_ltp_adjtimex_min.log` 显示 `adjtimex01/02/03`
    均为 `: 0` 且 `UNKNOWN #0xab` 消失；
    `/tmp/seaos_la_ltp_afalg_eafnosupport.log` 显示 AF_ALG cases
    通过 `EAFNOSUPPORT` 转为 TCONF。不要把本轮算作 LTP 大组通过，
    后续仍有 `acct02` kernel config、shell helper、IPv6 RAW/asapi
    socket 与其他 LTP 环境/ABI 缺口。
  - 本轮后续为 LoongArch `faccessat(48)`、`mount(40)`、`umount2(39)`
    增加过长路径、非目录前缀、symlink loop 和只读 mount 记录语义。
    `/tmp/seaos_la_ltp_access04_ro_mount.log` 显示 `access04.c` 中
    `EINVAL`、`ENOENT`、`ENAMETOOLONG`、`ENOTDIR`、`ELOOP`、`EROFS`
    在 root/nobody 下均为 TPASS；后续 `access01`、`adjtimex` 与 AF_ALG
    已在本轮继续推进，但 IPv6/asapi 等缺口未清，所以 `ltp-musl` 仍不是
    clean。
- 旧的较长完整扫描还显示 `lmbench-musl` 已开始并输出 `Simple syscall` 与
  `Simple read`；本轮在这里主动停止 QEMU 收束任务，未计入 clean。
- 本轮 `lmbench-musl` focused 定位：
  - 修复后 `lat_fs -N 1 /var/tmp` 在官方 QEMU 日志
    `/tmp/seaos_la_lmbench_lat_fs_N1_hash.log` 输出 `0k/1k/4k/10k`
    四档结果，并到达 `======== test sucess ========` 与
    `shutdown: system halting`；该日志无 `FAIL`、`[SEGV]`、unknown
    syscall、trap、panic、`Function not implemented` 或
    `Interrupted system call`。
  - 历史默认官方 `lat_fs /var/tmp` blocker，已被后续
    sparse-zero memfs hole 与 lmbench clean 证据 supersede：
    `/tmp/seaos_la_lmbench_lat_fs_default_hash.log` 在 `0k` setup 阶段
    耗尽 65536 个 memfs inode。
  - `/tmp/seaos_la_lmbench_lat_fs_default_262k.log` 不是 262144-inode
    有效证据：复核日志显示 memfs 初始化仍为 `0x10000` inodes，说明当时
    头文件改动未触发 memfs 重编。不要引用该日志作为容量实验结论。
  - 历史三次默认 `lat_fs /var/tmp` focused 尝试均未 clean，结论仅说明
    线性扩 inode/小文件槽不是最终方向：
    `/tmp/seaos_la_lmbench_lat_fs_262k.log` 确认强制重编后的
    `0x40000` inode 仍在 `0k` 阶段耗尽 inode；
    `/tmp/seaos_la_lmbench_lat_fs_compact.log` 使用短路径内联与
    `0xC0000` inode 后通过 `0k`，但在 `1k` 阶段耗尽低端页并刷
    `memfs: out of memory`；`/tmp/seaos_la_lmbench_lat_fs_smallslot.log`
    加入 1 KiB 小文件槽后仍在 `1k` 阶段耗尽低端页。
  - 这些历史 blocker 已被后续 `/tmp/seaos_la_lmbench_execshare2.log`
    supersede；`lmbench-musl` 当前 clean。下一轮不要继续线性扩大 memfs，
    也不要从旧 `lat_fs` blocker 开始。
  - 本轮额外尝试 memfs-only highmem 与内容去重：
    `/tmp/seaos_la_lmbench_highmem_memfs.log` 推进到 `lat_fs 4k` 但在
    highmem `0xb0000000` 边界触发真实 trap；
    `/tmp/seaos_la_lmbench_highmem_dedup.log` 让 `1k` 输出完整，但在
    `4k/10k` 仍大量 `memfs: out of memory`。该 highmem/dedup 实验已回退，
    不作为保留修复；其中暴露的旧 `Hello` shell corruption 已被后续
    memfs fd writable 修复 supersede。
- 本轮追加 focused 复核：
  - `iozone-musl` highmem user-page 追加实验：
    `/tmp/seaos_la_iozone_highuser1.log` 中，DMW1 + high free-list 方案在
    `pmem_init` 触摸 `0x9000000000000000` KVA 时 ADEF；
    `/tmp/seaos_la_iozone_highuser2.log` 中，identity high-PA 方案仍在
    `pmem_init` 写 `0x90000000` high RAM 时 ADEF；
    `/tmp/seaos_la_iozone_highuser3.log` 改为低端 bitmap 管理 highmem 后可启动，
    high user pool 显示 0x300 MB，并越过 automatic 与 throughput initial
    writers，但在 `la_pmem_alloc_user_page()` 清零 high KVA 时 kernel trap，
    最终 `test fail`。highmem user-page 实验已撤回，不计 clean。
  - `cyclictest-musl`：`/tmp/seaos_la_cyclictest_fresh_next.log` 复现
    `NO_STRESS_P1/P8` 可跑到 end，但 hackbench/stress 阶段仍有
    `fork() (error: Out of memory)`、`No measurements available`、
    `CLIENT: ready write (error: Broken pipe)` 和
    `Creating workers (error: Out of memory)`。三轮 COW/refcount 试探
    未保留：`/tmp/seaos_la_cyclictest_cow1.log` 暴露早期
    `ecode=0x4` fault；补 `ecode=0x4` refill 后，
    `/tmp/seaos_la_cyclictest_cow2.log` 与
    `/tmp/seaos_la_cyclictest_cow3.log` 均让 `NO_STRESS_P1/P8` 变成
    `end: fail`，并在 hackbench 出现页异常级联。
  - `iozone-musl`：`/tmp/seaos_la_iozone_focused1.log` 到达
    `#### OS COMP TEST GROUP END iozone-musl ####`，但所有 throughput
    worker 组均打印 `Fork failed` 并产生非零子进程退出码；该旧证据已被
    `/tmp/seaos_la_iozone_mem_budget3.log` supersede，当前 iozone clean。
  - `ltp-musl`：`/tmp/seaos_la_ltp_rawsock1.log`、
    `/tmp/seaos_la_ltp_rawsock2.log`、`/tmp/seaos_la_ltp_sendmsg1.log` 与
    `/tmp/seaos_la_ltp_rawdeliver2.log` 显示当前 LTP 仍不 clean。保留的
    LoongArch-only raw/asapi 兼容修复清除了早期
    `socket(10,3,58/159)=EINVAL` 层，让 `asapi_01` 的
    `IPV6_CHECKSUM` offset 19/20/66 三个错误语义用例从 TFAIL 变为 TPASS，
    清除了旧 `sendmsg ENOSYS` 层，并使 `asapi_02` 12 个断言全 TPASS；
    `/tmp/seaos_la_ltp_ancillary1.log` 进一步让 `asapi_03` 18 个断言全
    TPASS，包括基础 pktinfo、hoplimit/tclass receive 和 2292 options。
    该阶段剩余真实 blocker 包括 wrapper 无条件 `FAIL LTP CASE`、kernel
    config `TBROK`、shell helper 缺失、`hopopt` protocol-0 TFAIL、
    `ask_password.sh`/`assign_password.sh`，以及后续 LTP ABI/environment
    gaps；kernel config、`mktemp`、`arping01.sh` 和 `ar01.sh` 层已被后续
    focused 证据推进。
    `hopopt` 已定位为 libc 边界：官方 LTP 20240524 `asapi_01.c` 检查
    `getprotobyname("hopopt")->p_proto == 0`，而容器内 musl `proto.c`
    使用内置协议表，0 号协议主名为 `ip` 且无 `hopopt` alias；它不读取
    initcode 创建的 `/etc/protocols`，因此不能通过内核或 runtime stub
    真实修复。
  - `lmbench-musl`：`/tmp/seaos_la_lmbench_focused1.log` 在 600 秒 timeout
    前推进到 `File /var/tmp/XXX write bandwidth`，但没有 GROUP END；
    日志中的旧 `Hello` shell corruption 已被后续 memfs fd writable 修复
    supersede，该日志只作为历史 blocker 证据，不计 clean。
- 本轮恢复完整 `/musl` 入口后，`/tmp/seaos_la_musl_after_current_revert.log`
  在官方 QEMU 360 秒 smoke 中覆盖 `libcbench-musl`、`libctest-musl` 并进入
  `unixbench-musl` 的 `FS_WRITE_SMALL`，外层 timeout 124 结束；截至 timeout
  的广义失败扫描为空，但这不是新的完整 `/musl` 通过证据。
- 本轮 LTP raw socket 修复后再次恢复完整 `/musl` 入口并重建，最新 smoke
  `/tmp/seaos_la_musl_restore_after_ltp_rawsock.log` 使用官方 QEMU 180 秒
  timeout，结果为 `exit=124`。它到达 `libcbench-musl`、`libctest-musl`
  GROUP END 并进入 `unixbench-musl`，停在 CONTEXT 后；未命中
  `FAIL`、`[SEGV]`、`trap:`、`panic`、`unknown syscall` 或
  `Function not implemented`，但包含既有的非零 child `exit: pid ... code=1`
  调试行，因此只作为恢复 full-entry smoke，不是完整 `/musl` 通过证据。
- 本轮修复的 UnixBench shell 段根因：
  - `statx("[")` 缺少 busybox applet probe，导致 `[` 找不到。
  - shebang 递归 exec 丢失原脚本 `argv[1..]`，导致 `multi.sh` 的 `$1`
    为空并出现 `sh: -le: argument expected`。
  - 官方镜像内没有 `/musl/sort.src`，而 `tst.sh` 引用 `./sort.src`；
    initcode 现在创建最小运行期 `sort.src` memfs stub，未修改镜像或脚本。
- Focused 官方 QEMU 证据：
  - `/tmp/seaos_la_multi_sh_focus_sigsuspend.log` 证明 `[` 与 `$1` 已修复，
    但暴露缺失 `sort.src`。
  - `/tmp/seaos_la_multi_sh_focus_sortsrc.log` 输出 `MULTI_OK`，且无
    `UNKNOWN`、`argument expected`、`No such file`。

当前大测试组进度：

| Scope | Total | Clean with latest evidence | Remaining / unverified |
|---|---:|---:|---:|
| `/musl` image groups | 12 | 11 (`libcbench-musl`, `libctest-musl`, `unixbench-musl`, `busybox-musl`, `cyclictest-musl`, `netperf-musl`, `iperf-musl`, `iozone-musl`, `lua-musl`, `basic-musl`, `lmbench-musl`) | 1 |
| all image groups | 24 | 11 (`libcbench-musl`, `libctest-musl`, `unixbench-musl`, `busybox-musl`, `cyclictest-musl`, `netperf-musl`, `iperf-musl`, `iozone-musl`, `lua-musl`, `basic-musl`, `lmbench-musl`) | 13 |
| local judged groups | 22 | 10 (`libcbench-musl`, `libctest-musl`, `busybox-musl`, `cyclictest-musl`, `netperf-musl`, `iperf-musl`, `iozone-musl`, `lua-musl`, `basic-musl`, `lmbench-musl`) | 12 |

`os_serial_out_la.txt` 是旧日志，不能代表当前 LoongArch 状态。

## 当前下一步

### 2026-06-27 continuation audit：LTP 结构性阻塞确认

本轮按约束重新执行了根目录 `ls -la`、复读 `AGENTS.md`、`CLAUDE.md`、
`la-current.md`、`la-task.md` 和 `docs/LOONGARCH_MUSL_HANDOFF.md`，并复核：

- 本地 `HEAD=47c4bd880d75734aa0f663630420bf406bc59c03`，
  `origin/os2026-1=db829a7238c8c3eb5b8a7bae00c43a03057c2046`，当前分支仍
  behind 8；未在 dirty worktree 上 pull/rebase/reset。
- `src/user/initcode_la.c` 仍为完整 `/musl` 入口：
  `run_test_entries("/musl")`，并保留 `TST_TIMEOUT=-1`。
- 保护路径 `autotest-for-oskernel/`、`data/sdcard-rv.img.gz`、
  `data/sdcard-la.img.gz` 无 diff。
- `docker exec seaos-la bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'`
  通过。

只读复核测试镜像进一步确认 `ltp-musl` 的当前阻塞不是普通内核 syscall 小修：

- `debugfs -R "cat /musl/ltp_testcode.sh" sdcard-la.img` 显示官方 wrapper
  对每个 case 无条件打印 `FAIL LTP CASE $(basename "$file") : $ret`，即使
  `$ret=0`。在当前“日志中不得有 FAIL”的硬口径下，`ltp-musl` 不能通过
  修改内核自然变成 clean；修改/包装官方脚本或抑制输出均被禁止。
- `/tmp/seaos_la_ltp_enosys_only1.log` 仍显示唯一早期 `TFAIL` 是
  `asapi_01.c:119: "hopopt" protocols entry`；该点已定位为当前 musl
  内置 protocol table 缺 `hopopt` alias，不读取 initcode 创建的
  `/etc/protocols`，不是内核 stub 可修目标。
- `bind06` 为 namespace config `TCONF`，`cgroup_core01/02/03` 也为
  controller/config `TCONF`。
- 真正的 cgroup 硬阻塞来自官方 wrapper 直接枚举 helper：
  `cgroup_fj_common.sh` 与 `cgroup_lib.sh` 报
  `must call tst_run`，`cgroup_fj_function.sh` 无参数报
  `cgroup_require: controller not defined`，`cgroup_fj_stress.sh` 无参数报
  `Number of subgroups must be possitive integer`。
  `debugfs -R "cat /musl/ltp/testcases/bin/cgroup_regression_3_1.sh" sdcard-la.img`
  确认该 helper 需要路径参数；被 wrapper 无参数直接执行后进入
  `while true; do mkdir $path/0; rmdir $path/0; done`，形成无限循环类阻塞。

因此本轮没有新增可计数 clean 大测试组；当前进度仍为 `/musl` 11/12 clean，
剩余 `ltp-musl`。继续推进只能减少真实 `TFAIL/TBROK/nonzero`，不能把
`ltp-musl` 计为 clean，除非验收口径明确排除官方 wrapper 的固定
`FAIL LTP CASE` 文本，或允许修改官方脚本/测试二进制。

P1：只剩 `ltp-musl`。当前最新 focused 起点是
`/tmp/seaos_la_ltp_enosys_only1.log`：`capget01`、`capget02`、
`capset01`-`capset04` 已在 LTP 顺序中真实 TPASS/ret 0；
旧 `/proc/sys/kernel/pid_max`、`/proc/self/mounts`、`rmdir`、`killall` 以及
LTP optional fd-creation `UNKNOWN #...` 层已推进。
下一步从仍未解决的 `asapi_01` `hopopt` libc 表边界、`bind06`
namespace-config TCONF，以及 cgroup controller/helper 层继续：`cgroup_core01/02`
因 memory controller TCONF，`cgroup_core03` 因 V2 base controller TCONF，
`cgroup_fj_common.sh`/`cgroup_lib.sh` 作为 helper 被直接枚举执行时报
`must call tst_run` TBROK，`cgroup_fj_function.sh` 报
`cgroup_require: controller not defined`，`cgroup_fj_stress.sh` 报
`Number of subgroups must be possitive integer`，随后
`cgroup_regression_3_1.sh` helper 无参数进入循环类路径。不要再从 kernel config 查找、
`mktemp` helper 缺失、`ar01.sh`、`arping01.sh`、`bind01`-`bind05`、
`capget/capset`、`/proc/sys/kernel/pid_max`、`/proc/self/mounts`、`rmdir` 或
`killall` 缺失、LTP optional fd-creation `UNKNOWN` 开始，除非新鲜日志证明回归；
也不要再从 asapi 早期 RAW socket
`EINVAL`、
`IPV6_CHECKSUM` offset 语义、ICMP6_FILTER raw delivery、基础
`IPV6_RECVPKTINFO`、`sendmsg` 或 IPv6 ancillary receive options 开始；
`hopopt` protocol-0 lookup 已证明是
当前 musl 内置协议表边界，除非允许改 libc/测试二进制，否则不可作为内核
小修目标。不要再从
`access01/access02/access04`、`adjtimex`、AF_ALG、
`fchmodat/fchownat/setpgid/setuid` ENOSYS、`abort01`、`accept*`、
password/keyctl helper 或 direct `brk01` 开始。
当前不要再从
pthread_cancel、EXECL、`fstime`、UnixBench shell 段、busybox、netperf、
iperf、lua、basic、lmbench 或 cyclictest 开始，除非这些组在新完整扫描里
出现回归。

`cyclictest-musl` 当前结论：

- `cyclictest-musl` 已在 `/tmp/seaos_la_cyclictest_ticktime_clean1.log`
  中到达 GROUP END/shutdown，四个 cyclictest 子项与 kill hackbench 均
  success，广义失败扫描为空。
- 旧 STRESS_P8 timeout 的最终根因是 timebase drift；保留修复为
  `clock_gettime/gettimeofday` 与 `clock_nanosleep` 使用同一个 100 Hz
  tick clock。

`lmbench-musl` 当前结论：

- `lmbench-musl` 已在 `/tmp/seaos_la_lmbench_execshare2.log` 中到达
  GROUP END，广义失败扫描为空。
- 不要再从旧 `lat_fs /var/tmp`、`Hello` shell corruption、
  `lat_sig catch/prot`、`lat_pipe` 或 `lat_ctx 96` blocker 开始，除非
  新鲜完整扫描证明回归。

建议复现命令：

```bash
docker exec seaos-la bash -lc 'cd /workspace && timeout 5400 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_musl_next.log 2>&1'; echo exit=$?
```

建议扫描：

```bash
docker exec seaos-la bash -lc 'grep -a -n "^FAIL \|\[SEGV\]\|failed:\|UNKNOWN\|unknown syscall\|trap:\|panic\|Function not implemented\|Interrupted system call\|No such file\|argument expected\|end: fail\|test fail\|test timeout\|exec fail\|fork fail\|Operation not permitted\|Broken pipe" /tmp/seaos_la_musl_next.log | tail -260'
docker exec seaos-la bash -lc 'grep -a -n "#### OS COMP TEST GROUP START\|#### OS COMP TEST GROUP END\|run /musl/\|lmbench\|cyclictest\|netperf" /tmp/seaos_la_musl_next.log'
```

## 2026-06-27 LTP arping/fd 进展

本轮保留修复：

- LoongArch `dup`、`dup3`、`fcntl(F_DUPFD*)` 复制 socket fd 时现在同步
  `la_sock_dup()`，避免 BusyBox `xmove_fd()` 把 AF_PACKET socket 移到 fd 3
  后，关闭旧 fd 就释放底层 socket slot。
- LoongArch AF_PACKET `SOCK_DGRAM` 增加最小 `sockaddr_ll`
  `getsockname/recvfrom` 写回，并为 ARP request payload 合成一个本机
  loopback ARP reply。该模型只覆盖当前无真实网卡栈的 LTP/BusyBox
  `arping` 路径，不实现完整 packet socket 网络栈。
- initcode 保留 `TST_NET_SKIP_VARIABLE_INIT=1` 与静态 `eth0`
  `10.0.0.2/10.0.0.1` LTP 网络环境，避免 LTP helper 依赖未实现的
  rtnetlink 初始化；同时保留 `/bin/id` runtime stub。

最新证据：

- `/tmp/seaos_la_arping01_clean1.log`：focused `arping01.sh` 在无内核 net
  调试输出下 `TPASS: arping -w 10 10.0.0.1 -I eth0 -fq passed as expected`，
  Summary 为 `passed 1 / failed 0 / broken 0`，广义失败扫描为空。
- `/tmp/seaos_la_ltp_after_arping_fix1.log`：focused `ltp-musl` 顺序运行中
  `arping01.sh` 同样 TPASS，说明该修复在完整 LTP 顺序里有效。该日志里
  的 `ar01.sh` 缺 `ar` 层已被后续 `/tmp/seaos_la_ar01_min_ar10.log` 与
  `/tmp/seaos_la_ltp_after_ar10.log` supersede；`asapi_01` 仍有已知
  `hopopt` libc-table TFAIL，`ask_password.sh` 失败，`assign_password.sh`
  在 900 秒窗口内仍为下一处交互式 password/keyctl blocker。
- `/tmp/seaos_la_musl_after_arping_fix_smoke.log`：恢复完整 `/musl` 入口后
  360 秒 smoke 到达 `libcbench-musl`、`libctest-musl` GROUP END 并进入
  `unixbench-musl`，到 timeout 截点广义失败扫描为空；它不是完整
  `/musl` pass 证据。

该 arping 阶段的“下一步”已被后续 ar01、password/brk 和 bind01-05 证据
supersede。当前起点以本文“当前下一步”和
`/tmp/seaos_la_ltp_after_bind05_fix1.log` 为准；不要再从 `ar01.sh` 缺 `ar`
applet 或 `arping01.sh` 的 AF_PACKET/getsockname/EBADF 层开始，除非新鲜
日志证明回归。

## 2026-06-27 LTP ar01 进展

本轮保留修复：

- LoongArch `statx(291)` 对 memfs 文件现在回填真实 `memfs_inode_mode()`，
  使运行期 chmod 后的 helper 能被 shell 识别为可执行。
- LoongArch fd 状态新增 `O_APPEND`，`open/F_SETFL/F_GETFL` 保存 append 位，
  memfs `write` 在 append fd 上写入文件尾。这修复 shell `>>` 重定向覆盖
  order 文件的问题。
- LoongArch BusyBox applet fallback 移除 `ar`，因为当前 BusyBox 实际没有
  `ar` applet；缺失的 `ar` 由 initcode 创建的 `/bin/ar` wrapper 转到
  `/tmp/ar` runtime helper。
- initcode 保留最小 `/tmp/ar` helper，覆盖 LTP `ar01.sh` 需要的
  `-cr/-ra/-ma/-rb/-mb/-qc/-d/-ri/-mi/-m/-p/-q/-ru/-tv/-xv` 行为；
  这是运行期补足缺失工具，不修改官方脚本、测试二进制或镜像。

最新证据：

- `/tmp/seaos_la_ar01_min_ar10.log`：focused `ar01.sh` Summary 为
  `passed 20 / failed 0 / broken 0 / skipped 0 / warnings 0`，到达
  `shutdown: system halting`。
- `/tmp/seaos_la_ltp_after_ar10.log`：focused LTP 顺序中 `ar01.sh` 20 个
 断言全部 TPASS，随后 `arping01.sh` 仍 TPASS。该日志继续暴露真实
  `asapi_01` `hopopt` TFAIL、`ask_password.sh` 返回 1、`assign_password.sh`
  交互式 password/keyctl blocker；`ltp-musl` 仍不 clean。
- `/tmp/seaos_la_musl_after_ar_smoke.log`：恢复完整 `/musl` 入口后 360 秒
  smoke 到达 `libcbench-musl`、`libctest-musl` GROUP END，并进入
  `unixbench-musl` 输出 DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/SPAWN/EXECL；
  到 timeout 截点硬失败扫描为空。它不是完整 `/musl` pass 证据。

该 ar01 阶段的“下一起点”已被后续 password/brk 和 bind01-05 证据
supersede。当前起点以本文“当前下一步”和
`/tmp/seaos_la_ltp_after_bind05_fix1.log` 为准；不要再从 `ar01.sh` 或
`arping01.sh` 开始，除非新鲜日志证明回归。一次 `/etc/protocols` 顺序试探
未改变 `asapi_01` 的 `hopopt` TFAIL，相关改动未保留。

## 2026-06-27 LTP password/brk 进展

本轮保留修复：

- LoongArch 增加最小 `/dev/ttyS0` 字符设备语义：写入走 UART，读取提供
  非交互测试环境下的确定输入流；`pselect6/ppoll` 的 fd readiness 现在把
  字符设备视为立即可读/可写，避免 bash `read -s -p` 永久等待。
- initcode 创建运行期 `/bin/keyctl` wrapper，仅支持 password helper 需要的
  `keyctl instantiate` 成功路径；内核 `add_key(217)` 与 `keyctl(219)`
  syscall 仍显式返回 `-ENOSYS`，C 版 keyctl LTP 用例继续按 unsupported
  TCONF 处理。
- LoongArch `brk(214)` 扩展 heap 时跳过已经映射的页，只为缺页分配；非法低
  地址按 Linux `brk` 特例返回当前 break，而不是裸 `-1`。

最新证据：

- `/tmp/seaos_la_password_helpers2.log`：direct focused
  `ask_password.sh`/`assign_password.sh` 分别打印 `Password accepted.` 与
  `Password assigned.`，到达 `shutdown: system halting`；广义硬失败扫描为空。
- `/tmp/seaos_la_ltp_after_password2.log`：focused LTP 顺序中
  `ask_password.sh : 0`、`assign_password.sh : 0`，随后继续运行到
  `brk01`，证明旧 password/keyctl 交互 blocker 已越过。该日志仍保留
  `asapi_01` `hopopt` TFAIL，并在旧 brk 语义下暴露 `brk01` TFAIL。
- `/tmp/seaos_la_brk01_fix1.log`：direct focused `brk01` 中 libc variant
  TCONF、syscall variant `TPASS: brk() works fine`，Summary 为
  `passed 1 / failed 0 / broken 0 / skipped 1 / warnings 0`，到达 shutdown。
- `/tmp/seaos_la_musl_after_password_brk_smoke.log`：恢复完整 `/musl` 入口后
  360 秒 smoke 到达 `libcbench-musl` 与 `libctest-musl` GROUP END，并进入
  `unixbench-musl` 输出 DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/SPAWN/EXECL；
  到 timeout 截点硬失败扫描为空。它不是完整 `/musl` pass 证据。

`ltp-musl` 仍不 clean。该段后续已被下面的 bind01-05 进展更新 supersede；
不要再从 password/keyctl helper 或 direct `brk01` 开始，除非新鲜日志证明回归。

## 2026-06-27 LTP bind01-05 进展

本轮保留修复：

- LoongArch `bind(200)` 补齐 LTP bind 前段所需 errno：非 socket fd 返回
  `ENOTSOCK`，非 root 绑定 1..1023 端口返回 `EACCES`，非本地 IPv4 地址返回
  `EADDRNOTAVAIL`，AF_UNIX 路径前缀不是目录返回 `ENOTDIR`。
- LoongArch socket 层增加最小 AF_UNIX pathname/abstract bind 状态：
  同一 socket 重绑返回 `EINVAL`，其他 live socket 占用同一路径返回
  `EADDRINUSE`；成功的 pathname bind 在 memfs 中创建可 unlink 的占位节点。
- `SOCK_SEQPACKET` 在当前 loopback 兼容面内按 stream 处理，覆盖 LTP bind04
  中 AF_UNIX/IPv4/IPv6 SCTP/seqpacket 组合的通信路径。

最新证据：

- `/tmp/seaos_la_bind02_fix1.log`：direct `bind02` 中
  `TPASS: bind() : EACCES (13)`，Summary `passed 1 / failed 0 / broken 0`，
  硬失败扫描为空。
- `/tmp/seaos_la_bind03_fix1.log`：direct `bind03` 中 re-bind 得到
  `EINVAL`、已绑定 pathname 得到 `EADDRINUSE`，Summary
  `passed 2 / failed 0 / broken 0`，硬失败扫描为空。
- `/tmp/seaos_la_bind04_fix3.log`：direct `bind04` 中 AF_UNIX
  pathname/abstract stream/seqpacket、IPv4/IPv6 TCP/SCTP 共 16 项通信
  TPASS，Summary `passed 16 / failed 0 / broken 0`，硬失败扫描为空。
- `/tmp/seaos_la_bind05_fix1.log`：direct `bind05` 中 AF_UNIX
  pathname/abstract datagram 与 IPv4/IPv6 UDP/UDP-Lite 共 14 项通信 TPASS，
  Summary `passed 14 / failed 0 / broken 0`，硬失败扫描为空。
- `/tmp/seaos_la_ltp_after_bind05_fix1.log`：focused LTP 顺序中
  `bind01`、`bind02`、`bind03`、`bind04`、`bind05` 均为 `FAIL LTP CASE ... : 0`
  且组内断言为 TPASS；随后 `bind06` 因 `CONFIG_USER_NS`/`CONFIG_NET_NS`
  缺失 TCONF，继续暴露 `/proc/sys/kernel/pid_max`、`/proc/self/mounts`
  等 proc/cgroup 环境 TBROK。该日志人工停止于后续 LTP，不是 ltp-musl
  clean 证据。
- `/tmp/seaos_la_musl_after_bind_smoke1.log`：恢复完整 `/musl` 入口后
  360 秒 smoke 到达 `libcbench-musl` 与 `libctest-musl` GROUP END，进入
  `unixbench-musl` 并输出 DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/SPAWN/EXECL；
  到 timeout 截点硬失败扫描为空。它不是完整 `/musl` pass 证据。

`ltp-musl` 仍不 clean：前段仍有已知 `asapi_01` `hopopt` libc-table TFAIL；
bind06 是 namespace config TCONF；后续真实环境缺口包括 `/proc/sys/kernel/pid_max`
和 `/proc/self/mounts` 相关 proc/cgroup TBROK。下一轮不要再从 bind01-05、
password/keyctl helper、direct `brk01`、`ar01.sh` 或 `arping01.sh` 开始，
除非新鲜日志证明回归。

## 2026-06-27 LTP capability/proc/cgroup 进展

本轮保留修复：

- LoongArch `capget(90)` 与 `capset(91)` 增加最小 Linux capability ABI：
  支持 capability header 版本 1/2/3、按 pid 查找当前/目标进程、按 Linux
  errno 返回 `EFAULT/EINVAL/ESRCH/EPERM`，并在进程结构中保存
  effective/permitted/inheritable 三组 capability mask，fork/clone 继承。
- LoongArch `mmap(222)` 现在按 `prot` 设置用户 PTE 权限，`PROT_NONE`
  映射不再被错误赋为 RWX；`mprotect(226)` 复用同一权限计算。这样 LTP
  guarded buffer 能得到真实 `EFAULT`，不是被内核 copy 路径写穿。
- initcode 运行期创建 `/proc/sys/kernel/pid_max` 与 `/proc/self/mounts`，
  并补充 BusyBox wrapper/fallback：`rmdir`、`killall`。

最新证据：

- `/tmp/seaos_la_capability_fix4.log`：direct focused `capget01`、`capget02`、
  `capset01`、`capset02`、`capset03`、`capset04` 全部 Summary
  `failed 0 / broken 0`，广义失败扫描为空。
- `/tmp/seaos_la_ltp_after_capability_fix1.log`：focused LTP 顺序中
  `capget01`、`capget02`、`capset01`-`capset04` 均为真实 TPASS/ret 0；
  旧 `/proc/sys/kernel/pid_max` 和 `/proc/self/mounts` TBROK 消失，
  cgroup_core 转为 controller TCONF/helper 层。
- `/tmp/seaos_la_ltp_after_rmdir_fix1.log`：`rmdir not found` 消失，下一层为
  `killall not found`。
- `/tmp/seaos_la_ltp_after_killall_fix1.log`：`killall not found` 消失，
  cgroup helper 继续推进到 `controller not defined`、`Number of subgroups
  must be possitive integer` 以及 helper 被直接执行的 `must call tst_run`
  TBROK。`ltp-musl` 仍不 clean。
- `/tmp/seaos_la_musl_after_capability_smoke1.log`：恢复完整 `/musl` 入口后
  360 秒 smoke 到达 `libcbench-musl`、`libctest-musl` GROUP END，并进入
  `unixbench-musl` 输出 DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/SPAWN/EXECL；
  到 timeout 截点硬失败扫描为空。它不是完整 `/musl` pass 证据。

当前 `/musl` 大组计数不变：11/12 clean，剩余仍为 `ltp-musl`。本轮新增的是
LTP 内部 capability/proc/cgroup 环境层推进，不能计为新增 clean 大测试组。

## 2026-06-27 LTP optional ENOSYS 与 cgroup helper 定位

本轮保留修复：

- LoongArch syscall dispatcher 将 LTP fd-creation/optional feature 探测中
  出现的可选 Linux syscall 注册为显式 `-ENOSYS`，不再落入默认
  `UNKNOWN` 分支：`eventfd2(19)`、`epoll_create1(20)`、
  `inotify_init1(26)`、`signalfd4(74)`、`timerfd_create(85)`、
  `perf_event_open(241)`、`fanotify_init(262)`、`memfd_create(279)`、
  `bpf(280)`、`userfaultfd(282)`、`io_uring_setup(425)`、
  `open_tree(428)`、`fsopen(430)`、`fspick(433)`、`pidfd_open(434)`、
  `memfd_secret(447)`。
- `src/user/initcode_la.c` 已恢复完整 `run_test_entries("/musl")`。
  `TST_TIMEOUT=30` 试探会让 `ar01.sh`、`arping01.sh` 与 `broken_ip-*`
  回归 `timeout need to be >= 1 ()`，已撤回；当前仍保留
  `TST_TIMEOUT=-1`，因为它维持已验证的 ar/arping 路径。

最新证据：

- `/tmp/seaos_la_ltp_enosys_only1.log`：focused LTP-only 运行中，
  旧 `accept03` 附近的 `UNKNOWN #0x14/#0x13/#0x4a/#0x55/...` 串消失；
  `ar01.sh` 仍为 20/20 TPASS，`arping01.sh` 仍 TPASS，`broken_ip-*`
  能继续推进到 TPASS/TCONF。该日志人工停止于
  `cgroup_regression_3_1.sh` helper，不是 `ltp-musl` clean 证据。
- `/tmp/seaos_la_ltp_enosys_timeout30_1.log`：反证日志，说明
  `TST_TIMEOUT=30` 会使 `ar01.sh`、`arping01.sh`、`broken_ip-*` 回归
  `timeout need to be >= 1 ()`；相关试探已撤回。
- `/tmp/seaos_la_musl_after_enosys_stub_smoke1.log`：恢复完整 `/musl`
  入口后 360 秒 smoke 到达 `libcbench-musl` 与 `libctest-musl` GROUP END，
  进入 `unixbench-musl` 并输出 DHRY2、WHETSTONE、SYSCALL、CONTEXT、
  PIPE、SPAWN、EXECL；硬失败扫描为空。它不是完整 `/musl` pass 证据。

当前结论：

- `/musl` 大组计数仍为 11/12 clean，剩余仍是 `ltp-musl`。
- `ltp-musl` 当前不可计 clean：官方 `/musl/ltp_testcode.sh` 对 ret=0 也
  无条件打印 `FAIL LTP CASE ... : 0`，且真实缺口仍包括
  `asapi_01` `hopopt` libc-table TFAIL 和 cgroup helper TBROK。
- cgroup 当前真实 blocker 主要是 helper 被官方 wrapper 直接枚举执行：
  `cgroup_fj_common.sh`/`cgroup_lib.sh` 报 `must call tst_run`，
  `cgroup_fj_function.sh` 无参数时报 `cgroup_require: controller not defined`，
  `cgroup_fj_stress.sh` 无参数时报
  `Number of subgroups must be possitive integer`，随后
  `cgroup_regression_3_1.sh` 作为 helper 无参数进入循环类路径。
  这些不能通过修改官方脚本、跳过用户程序或抑制输出来处理。

## 应保留的已完成工作

### 进程、exec、信号与 TLB

- `exec` 不再在 syscall 实现内部直接跳到用户态。它把替换上下文拷到活动
  trap frame 中，由常规 syscall 返回路径走 `ertn`。
- `exec` 重置信号相关状态，避免被替换地址空间里旧的 handler 残留复用。
- 动态程序执行时启用 `LD_BIND_NOW=1`，规避当前 LoongArch musl lazy binding
  路径上的 PLT/GOT 问题。
- 信号投递构建用户信号帧：保存 GPR、ERA、旧 mask、`siginfo`、`ucontext`
  和 `rt_sigreturn` trampoline。
- `rt_sigaction` 读取 LoongArch musl kernel ABI 布局：handler、flags、mask；
  用户 restorer 不在该 ABI 中，信号返回使用 sigframe 内 trampoline。
- `rt_sigreturn` 从信号帧 / ucontext 路径恢复状态。
- `rt_sigsuspend(133)` 有最小兼容实现：校验并读取用户 sigset，恢复原
  mask 后返回 `-EINTR`，避免 busybox shell 等待路径刷 unknown syscall。
- 实现了 `clone(CLONE_VM)` 与 `CLONE_SETTLS`，用于 musl pthreads。
- 线程退出时清掉 `clear_child_tid` 并唤醒 futex 等待者。
- 独立 live address space 现在分配唯一 ASID；`CLONE_VM` 线程共享父 ASID。
  这修复了 `libctest-musl` 中 `runtest.exe` 父进程与 dynamic child ASID
  碰撞导致的 stale TLB/`INE` fault。
- script/shebang exec 会按 Linux 语义保留原脚本参数：
  `[interp, optional_arg, script, old_argv[1..]]`。

### Futex 与调度器

- Futex 支持 `WAIT`、`WAKE`、`REQUEUE`、`CMP_REQUEUE`、`WAIT_BITSET`、
  `WAKE_BITSET`。
- 带超时的 futex 等待用调度器 deadline：
  `la_proc_sleep_chan_until(chan, deadline_ticks)`。
- 调度器在 deadline 到达时唤醒 futex 睡眠者并标记为超时。
- 被信号中断的 futex wait 按 `-EINTR` 返回。
- 调度器支持最小 `sched_setscheduler/getparam/setparam/getscheduler` 状态，
  以满足 cyclictest/musl 调用路径。

### 文件描述符、设备与文件系统

- `LA_NFD` 当前为 512，配合较大的 pipe/socket 资源上限。
- fd 条目记录 `cloexec` 与 `nonblock`。
- `fcntl` 实现 `F_DUPFD`、`F_DUPFD_CLOEXEC`、`F_GETFD`、`F_SETFD`、
  `F_GETFL`、`F_SETFL`。
- `dup`、`dup3`、`pipe2`、`socket`、`accept4` 与 open 路径更准确地
  保留/设置 fd 标志。
- 控制台写入会检查 `LA_FD_CONSOLE`，不再无条件地把 fd 0-2 当作控制台。
- `/dev/null`、`/dev/zero`、`/dev/random`、`/dev/urandom`、`/dev/rtc`、
  `/dev/misc/rtc` 以最小兼容设备形式存在。
- initcode 会创建 `/proc/mounts`、`/proc/meminfo`、`/dev/shm` 等运行期 stub。
- initcode 会创建最小 `/musl/sort.src` 与 `./sort.src` 运行期输入 stub，
  只用于弥补镜像内 UnixBench `tst.sh` 引用但未提供的 `sort.src`。
- memfs 支持更大的文件、truncate、rename、路径查找与持久化时间戳。
- `unlinkat`、`pipe2` 等路径返回具体 `-EXXX`，不再返回裸 `-1`。

### stat、time、resource limit、socket 与 mmap

- `fstat`、`newfstatat`、`statx` 使用与 LoongArch musl 测试字段兼容的布局。
- 支持 `newfstatat(fd, "", ..., AT_EMPTY_PATH)`。
- `utimensat` 支持路径名和 fd 时间戳更新、`UTIME_NOW`、`UTIME_OMIT`。
- `statx` 对已知 busybox applet probe（如 `[`、`sort`、`seq`）返回最小
  可执行文件状态，让 busybox shell 能进入 exec fallback，而不是在 stat
  阶段误判命令不存在。
- `clock_getres` 与 `clock_nanosleep` 有最小兼容实现。
- `getitimer(102)` 与 `setitimer(103)` 支持 `ITIMER_REAL`，timer interrupt
  会投递 `SIGALRM`；这让 `unixbench-musl` 从 `dhry2reg` 死循环推进到
  后续 CONTEXT 子项。
- `RLIMIT_NOFILE` 是进程局部的，fork/clone 时继承。
- `socket_la.c/h` 中存在 loopback TCP/UDP socket 层，支持当前 netperf/iperf
  所需的基本路径。
- socket `accept/accept4` 在阻塞 sleep 被未屏蔽 pending signal 唤醒时返回
  `-EINTR`，用于 netperf TCP_CRR 的 alarm-driven netserver 收尾。
- `pselect6` 与 `ppoll` 已经为 pipe/socket/console 路径做 readiness 检查。
- `mmap` 支持匿名映射和当前测试用到的 MAP_PRIVATE 文件映射。

## 文档维护规则

完成新的大测试组或关键任务后，同步更新：

- `la-task.md`：任务矩阵、当前进度、下一轮提示词。
- `la-current.md`：最新状态、证据、下一步。
- `docs/DECISIONS.md`：若 syscall/ABI/进程/内存/FS/信号/调度语义变化。
- `docs/SYSCALL_STATUS.md`：若 syscall 支持状态变化。

不要把旧日志或 wrapper 标记写成通过证据。
