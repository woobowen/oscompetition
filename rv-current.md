# SeaOS RISC-V Current State (2026-06-24)

## Baseline Command

Current RISC-V evidence comes from the required docker command:

```powershell
docker run --rm `
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" `
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" `
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```

Before each rerun, remove only root-level temporary image files such as `sdcard-rv.img`, `sdcard-la.img`, `sdcard-rv.img.gz`, and `sdcard-la.img.gz` if they exist. Do not modify the protected grader images under `data/`.

Generated log: `os_serial_out_rv.txt`

The latest regenerated RISC-V log was written on 2026-06-24 04:02:31 Asia/Shanghai. It reached `sys_shutdown: powering off via SBI SRST` at line 4553 after all 24 groups were enumerated. This run was generated after the robust futex owner-death fix and the existing memfs `/tmp` compatibility change. The docker/QEMU container was stopped only after `sys_shutdown` had already appeared.

## Run Monitoring Rule

During long fixed-docker reruns, inspect `os_serial_out_rv.txt` about every 15 minutes instead of waiting blindly on the wrapper command:

- If `sys_shutdown: powering off via SBI SRST` appears, treat the run as complete, stop any stale docker/QEMU container if it remains alive, then analyze the log.
- If the file size and mtime do not change for one 15-minute polling interval and no `sys_shutdown` appears, treat the run as stuck and stop waiting so the log can be analyzed as a hang.
- Never treat wrapper `======== test end ========` or `GROUP END` as success by itself.

## Important Interpretation Rule

`======== test end    ========` is an initcode/test-wrapper marker. It only proves that the wrapper reached its own cleanup point. It is not proof that the testsuite really passed.

Real status must come from the actual `testsuits-for-oskernel/` program and script output. Internal markers such as `FAIL`, `[SEGV]`, `end: fail`, `Function not implemented`, `Interrupted system call`, `panic`, `test fail`, `test timeout`, `cp: not found`, pipe/open errors, file-descriptor exhaustion, or busybox testcase `fail` lines remain real defects even if a wrapper later prints its end marker.

## Current RV Evidence

RV initcode enumerates the full `/musl` 12 groups plus `/glibc` 12 groups. The current log still defers `unixbench`, `lmbench`, and `ltp` until after the shorter primary `/musl` and `/glibc` groups.

| Test group | Latest RV log marker | Real testsuite status notes |
|---|---|---|
| `libcbench-musl` | `GROUP END` line 154 | Clean in this log: no focused `FAIL`, `[SEGV]`, `end: fail`, timeout, panic, unknown syscall, or pipe/open marker in the group. |
| `libctest-musl` | `GROUP END` line 813 | Clean in this log: no focused failure marker in the group. Static and dynamic `pthread_robust_detach` both reach END. |
| `busybox-musl` | `GROUP END` line 1061 | Fails internally despite `GROUP END`: line 851 reports `testcase busybox which ls fail`, and line 869 reports `testcase busybox sh -c 'sleep 5' & ./busybox kill $! fail`. |
| `cyclictest-musl` | `GROUP END` line 1109 | Clean in this log. `/dev/cpu_dma_latency` warnings are optional cyclictest PM-QoS noise and are not treated as failures by themselves. |
| `netperf-musl` | `GROUP END` line 1171 | Clean in this log: all netperf subtests report success. |
| `iperf-musl` | `GROUP END` line 1297 | Clean in this log: no focused failure marker in the group. |
| `iozone-musl` | `GROUP END` line 1702 | Clean in this log: no focused failure marker in the group. |
| `lua-musl` | `GROUP END` line 1719 | Clean in this log: no focused failure marker in the group. |
| `basic-musl` | `GROUP END` line 1908 | Clean in this log: no focused failure marker in the group. |
| `libcbench-glibc` | `GROUP END` line 1997 | Clean in this log. The previous stdio `[SEGV]` remains gone after memfs initializes `/tmp`, `/var`, and `/var/tmp`. |
| `libctest-glibc` | `GROUP END` line 3425 | Fails internally: first visible failing case is `FAIL clocale_mbfuncs [status 1]` at line 3027; later failures include `pthread_cancel*`, locale/wide-char/stdio/regex cases, one `[SEGV]` in dynamic `daemon_failure` at line 3249, and timeouts. `pthread_robust_detach` no longer fails in either static or dynamic mode. |
| `busybox-glibc` | `GROUP END` line 3673 | Fails internally despite `GROUP END`: line 3463 reports `testcase busybox which ls fail`, and line 3481 reports `testcase busybox sh -c 'sleep 5' & ./busybox kill $! fail`. |
| `cyclictest-glibc` | `GROUP END` line 3723 | Fails internally despite `GROUP END`: line 3710 reports `Creating fdpair (error: Too many open files)`, and line 3722 reports `kill hackbench: fail`. |
| `netperf-glibc` | `GROUP END` line 3757 | Fails internally: all UDP/TCP stream and RR/CRR subtests report `end: fail`. The repeated netperf `[SEGV]` lines from the previous log did not recur. |
| `iperf-glibc` | `GROUP END` line 3884 | Clean in this log: no focused failure marker in the group. |
| `iozone-glibc` | `GROUP END` line 4289 | Clean in this log: no focused failure marker in the group. |
| `lua-glibc` | `GROUP END` line 4306 | Clean in this log: no focused failure marker in the group. |
| `basic-glibc` | `GROUP END` line 4494 | Clean in this log: no focused failure marker in the group. |
| `unixbench-musl` | `GROUP START` line 4501, wrapper `test fail` line 4504 | Fails before tests run: `/musl/unixbench_testcode.sh: line 5: can't create pipe: Bad file descriptor`. |
| `lmbench-musl` | `GROUP START` line 4509, timeout line 4514, `test fail` line 4516 | Fails: `/musl/lmbench_testcode.sh: line 21: cp: not found`, then timeout after `latency measurements`. |
| `ltp-musl` | `GROUP START` line 4521, wrapper `test fail` line 4524 | Fails before tests run: `/musl/ltp_testcode.sh: line 13: can't create pipe: No file descriptors available`. |
| `unixbench-glibc` | `GROUP START` line 4529, wrapper `test fail` line 4532 | Fails before tests run: `/glibc/unixbench_testcode.sh: line 5: can't create pipe: Bad file descriptor`. |
| `lmbench-glibc` | `GROUP START` line 4537, timeout line 4542, `test fail` line 4544 | Fails: `/glibc/lmbench_testcode.sh: line 21: cp: not found`, then partial file bandwidth output, then initcode timeout. |
| `ltp-glibc` | `GROUP START` line 4549, wrapper `test fail` line 4552 | Fails before tests run: `/glibc/ltp_testcode.sh: line 13: can't create pipe: Too many open files`. |

Focused checks in the latest RISC-V log:

```text
all_24_groups_seen=True
sys_shutdown=True
panic=False
unknown_syscall=False
function_not_implemented=False
interrupted_system_call=False
operation_not_permitted=False
segv=True                         # libctest-glibc dynamic daemon_failure only
explicit_group_or_wrapper_fail=True
busybox_subtest_fail=True
pipe_or_fd_resource_error=True
cp_not_found=True
```

Strict current count: 13 clean groups, 11 failing groups. This deliberately treats busybox subtest `fail` lines as real failures instead of relying on `GROUP END`.

## This Iteration

- `libctest-glibc` was investigated first. A host glibc comparison in the same docker image reproduced the first `clocale_mbfuncs` failure and many other libc-test/glibc semantic mismatches, so those are not SeaOS kernel fixes by themselves.
- A real kernel gap was fixed in the same group: `set_robust_list(99)` and `get_robust_list(100)` no longer act as empty success stubs. SeaOS now records each thread's robust futex list and, on thread/process exit, marks owned futex words with `FUTEX_OWNER_DIED` and wakes waiters. The `pthread_robust_detach` failure from the previous log is gone.
- The glibc test image still lacks locale data and `libgcc_s.so.1`, which explains some remaining `libctest-glibc` failures such as UTF-8 locale cases and `pthread_exit_cancel` abort noise. These are tracked as real current failures but not patched by changing the protected image.
- `libcbench-glibc` remains clean in the latest run. The worktree keeps the in-progress `src/kernel/fs/fs.c` memfs temp-directory compatibility fix that initializes `/tmp`, `/var`, and `/var/tmp`.
- `netperf-glibc` still fails all subtests, but the older repeated netperf `[SEGV]` lines did not recur in this log.
- The latest strict scan marks `busybox-musl` and `busybox-glibc` failing because they contain explicit busybox testcase `fail` lines. In the final rerun, `busybox-musl` exposes both `which ls fail` and `kill $! fail`.

## Remaining Real Gaps

Current failing groups in log order:

1. `busybox-musl`: `which ls` and shell `kill $!` subtests fail.
2. `libctest-glibc`: earliest marker is `FAIL clocale_mbfuncs [status 1]`; robust-detach is fixed, but many libc/image/kernel-adjacent failures remain.
3. `busybox-glibc`: `which ls` and shell `kill $!` subtests fail.
4. `cyclictest-glibc`: `Creating fdpair (error: Too many open files)` and `kill hackbench: fail`.
5. `netperf-glibc`: all subtests report `end: fail`, currently without the old netperf SEGV lines.
6. `unixbench-musl`: shell script cannot create a pipe, `Bad file descriptor`.
7. `lmbench-musl`: `cp: not found`, then initcode timeout after `latency measurements`.
8. `ltp-musl`: shell script cannot create a pipe, `No file descriptors available`.
9. `unixbench-glibc`: shell script cannot create a pipe, `Bad file descriptor`.
10. `lmbench-glibc`: `cp: not found`, partial file bandwidth output, then timeout.
11. `ltp-glibc`: shell script cannot create a pipe, `Too many open files`.

Do not treat wrapper `test end` or `GROUP END` as testsuite success.

## Public-Path Risk Notes

This iteration touched process lifecycle, syscall robust-list handling, and a shared FS initialization path. These are public kernel paths, so future work must regression-check with `make all` and the fixed RV docker command before committing.

The robust-list implementation is intentionally minimal Linux compatibility: it supports RISC-V LP64 `struct robust_list_head` size 24, walks a bounded list during exit, marks only futex words owned by the exiting TID, preserves the `FUTEX_WAITERS` bit, and wakes waiters. It does not implement PI futexes or a full Linux thread-group robust-futex model.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` must remain unmodified. Root-level temporary image files may be removed before fixed RV reruns and before commit staging.
