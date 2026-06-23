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

The latest regenerated RISC-V log was written on 2026-06-24 05:12:17 Asia/Shanghai. It reached `sys_shutdown: powering off via SBI SRST` at line 5434 after all 24 groups were enumerated. This run was generated after the robust futex owner-death fix, the memfs `/tmp` compatibility change, and the normal-fork mmap metadata inheritance fix. The docker/QEMU container was stopped only after `sys_shutdown` had already appeared.

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
| `busybox-musl` | `GROUP END` line 1059 | Fails internally despite `GROUP END`: line 851 reports `testcase busybox which ls fail`. The shell `kill $!` subtest now reports success at line 867. |
| `cyclictest-musl` | `GROUP END` line 1107 | Clean in this log. `/dev/cpu_dma_latency` warnings are optional cyclictest PM-QoS noise and are not treated as failures by themselves. |
| `netperf-musl` | `GROUP END` line 1169 | Clean in this log: all netperf subtests report success. |
| `iperf-musl` | `GROUP END` line 1295 | Clean in this log: no focused failure marker in the group. |
| `iozone-musl` | `GROUP END` line 1700 | Clean in this log: no focused failure marker in the group. |
| `lua-musl` | `GROUP END` line 1717 | Clean in this log: no focused failure marker in the group. |
| `basic-musl` | `GROUP END` line 1906 | Clean in this log: no focused failure marker in the group. |
| `libcbench-glibc` | `GROUP END` line 1995 | Clean in this log. The previous stdio `[SEGV]` remains gone after memfs initializes `/tmp`, `/var`, and `/var/tmp`. |
| `libctest-glibc` | `GROUP END` line 3424 | Fails internally: first visible failing case is `FAIL clocale_mbfuncs [status 1]` at line 2320. Static and dynamic `daemon_failure` still fail their glibc-vs-musl semantic expectation, but the previous dynamic `[SEGV]` is gone. `pthread_robust_detach` no longer reports the old timeout/owner-death failure. |
| `busybox-glibc` | `GROUP END` line 3670 | Fails internally despite `GROUP END`: line 3462 reports `testcase busybox which ls fail`. The shell `kill $!` subtest now reports success at line 3478. |
| `cyclictest-glibc` | `GROUP END` line 3718 | Clean in this log: the previous `Creating fdpair (error: Too many open files)` marker is gone, and `kill hackbench` reports success at line 3717. |
| `netperf-glibc` | `GROUP END` line 3774 | Fails internally: `UDP_STREAM` reports `end: fail` at line 3734. `TCP_STREAM`, `UDP_RR`, `TCP_RR`, and `TCP_CRR` report success; the older netperf `[SEGV]` lines did not recur. |
| `iperf-glibc` | `GROUP END` line 3901 | Clean in this log: no focused failure marker in the group. |
| `iozone-glibc` | `GROUP END` line 4306 | Clean in this log: no focused failure marker in the group. |
| `lua-glibc` | `GROUP END` line 4323 | Clean in this log: no focused failure marker in the group. |
| `basic-glibc` | `GROUP END` line 4512 | Clean in this log: no focused failure marker in the group. |
| `unixbench-musl` | `GROUP START` line 4519, timeout line 4535, `test fail` line 4537 | Fails by initcode timeout after running benchmark output through `FS_WRITE_BIG`. The previous early `can't create pipe: Bad file descriptor` marker is gone. |
| `lmbench-musl` | `GROUP START` line 4542, timeout line 4556, `test fail` line 4558 | Fails by initcode timeout after `latency measurements`. The previous `cp: not found` marker is gone in this log. |
| `ltp-musl` | `GROUP START` line 4563, timeout line 5250, `test fail` line 5252 | Fails after running many LTP cases. Real failures include unknown syscalls 89/217/219/171, `waitpid(...,0) failed: EINTR`, `wait() failed: EPERM`, missing `/proc/self/maps`, missing shell helpers such as `basename`, and network/protocol gaps. |
| `unixbench-glibc` | `GROUP START` line 5257, timeout line 5273, `test fail` line 5275 | Fails by initcode timeout after running benchmark output through `FS_READ_BIG`. The previous early `can't create pipe: Bad file descriptor` marker is gone. |
| `lmbench-glibc` | `GROUP START` line 5280, timeout line 5289, `test fail` line 5291 | Fails by initcode timeout after partial latency output. The previous `cp: not found` marker is gone in this log. |
| `ltp-glibc` | `GROUP START` line 5296, timeout line 5431, `test fail` line 5433 | Fails after running several LTP cases. Real failures include `waitpid(...,0) failed: EINTR`, `wait() failed: EPERM`, `accept03` O_PATH errno mismatch, missing `/proc/self/maps`, and timeout cleanup failures. The previous early pipe/fd exhaustion marker is gone. |

Focused checks in the latest RISC-V log:

```text
all_24_groups_seen=True
sys_shutdown=True
panic=False
unknown_syscall=True              # LTP-only: 89, 217, 219, 171
function_not_implemented=False
interrupted_system_call=False
operation_not_permitted=False
segv=False
explicit_group_or_wrapper_fail=True
busybox_subtest_fail=True
pipe_or_fd_resource_error=False    # old early pipe/fd blockers are gone
cp_not_found=False
```

Strict current count: 14 clean groups, 10 failing groups. This deliberately treats busybox subtest `fail` lines as real failures instead of relying on `GROUP END`.

## This Iteration

- `libctest-glibc` was investigated first. A host glibc comparison in the same docker image reproduced the `daemon_failure` semantic failure without a kernel crash: glibc forks and later returns `EBADF`, while the test expects musl-specific `EMFILE` before fork. That semantic mismatch is still a real testsuite failure, but not a SeaOS-only kernel crash.
- A real kernel stability gap was fixed in the same group: normal `fork()` now clones the parent's mmap-region metadata into the child. Previously, the child copied only already-faulted leaf pages and had `child->mmap = NULL`, so a dynamic glibc child could fault an unfaulted libc page after fork with no VMA metadata to service the lazy page fault. The latest log has no `[SEGV]`, and dynamic `daemon_failure` now fails only by the host-glibc semantic expectation.
- `cyclictest-glibc` improved from the previous `Creating fdpair (error: Too many open files)` and `kill hackbench: fail` to clean in the latest log.
- `netperf-glibc` improved from all subtests failing to only `UDP_STREAM` failing. The older netperf `[SEGV]` lines did not recur.
- The late `unixbench`, `lmbench`, and `ltp` groups now get past the old early pipe/fd and `cp: not found` startup blockers. They remain real failures because they hit initcode timeouts and LTP-internal errors.

## Remaining Real Gaps

Current failing groups in log order:

1. `busybox-musl`: `which ls` subtest fails.
2. `libctest-glibc`: earliest marker is `FAIL clocale_mbfuncs [status 1]`; `daemon_failure` remains a glibc semantic mismatch, and other locale/wide-char/stdio/pthread cases still fail or time out.
3. `busybox-glibc`: `which ls` subtest fails.
4. `netperf-glibc`: only `UDP_STREAM` still reports `end: fail`.
5. `unixbench-musl`: benchmark runs but hits initcode timeout/test-fail.
6. `lmbench-musl`: latency measurements run but hit initcode timeout/test-fail.
7. `ltp-musl`: LTP runs but has internal failures, unknown syscall markers, wait/signal errors, missing helper utilities, and timeout/test-fail.
8. `unixbench-glibc`: benchmark runs but hits initcode timeout/test-fail.
9. `lmbench-glibc`: latency measurements run but hits initcode timeout/test-fail.
10. `ltp-glibc`: LTP runs but has wait/signal errors, `/proc/self/maps` and socket errno gaps, cleanup failures, and timeout/test-fail.

Do not treat wrapper `test end` or `GROUP END` as testsuite success.

## Public-Path Risk Notes

This iteration touched process lifecycle and VM metadata inheritance. It is a public kernel path, so future work must regression-check with `make all` and the fixed RV docker command before committing.

The normal-fork mmap fix intentionally preserves the existing eager copy of already-faulted leaf pages. It only clones VMA metadata so lazy faults in the child can be resolved later. It is not copy-on-write, not a full Linux `mm_struct`, and not file-backed mmap semantics.

The robust-list implementation from the previous iteration remains intentionally minimal Linux compatibility: it supports RISC-V LP64 `struct robust_list_head` size 24, walks a bounded list during exit, marks only futex words owned by the exiting TID, preserves the `FUTEX_WAITERS` bit, and wakes waiters. It does not implement PI futexes or a full Linux thread-group robust-futex model.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` must remain unmodified. Root-level temporary image files may be removed before fixed RV reruns and before commit staging.
