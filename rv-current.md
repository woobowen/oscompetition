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

The latest regenerated RISC-V log was written on 2026-06-24 06:24:32 Asia/Shanghai. It reached `sys_shutdown: powering off via SBI SRST` at line 5204 after all 24 groups were enumerated. This run was generated after the robust futex owner-death fix, the memfs `/tmp` compatibility change, the normal-fork mmap metadata inheritance fix, and the BusyBox applet `newfstatat()` compatibility fix. The docker/QEMU container was stopped only after `sys_shutdown` had already appeared.

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
| `busybox-musl` | `GROUP END` line 1060 | Clean in this log: all focused busybox subtests report success, including `which ls` at line 852 and shell `kill $!` at line 868. |
| `cyclictest-musl` | `GROUP END` line 1111 | Clean in this log. `/dev/cpu_dma_latency` warnings are optional cyclictest PM-QoS noise and are not treated as failures by themselves. |
| `netperf-musl` | `GROUP END` line 1169 | Clean in this log: all netperf subtests report success. |
| `iperf-musl` | `GROUP END` line 1295 | Clean in this log: no focused failure marker in the group. |
| `iozone-musl` | `GROUP END` line 1700 | Clean in this log: no focused failure marker in the group. |
| `lua-musl` | `GROUP END` line 1717 | Clean in this log: no focused failure marker in the group. |
| `basic-musl` | `GROUP END` line 1906 | Clean in this log: no focused failure marker in the group. |
| `libcbench-glibc` | `GROUP END` line 1995 | Clean in this log. The previous stdio `[SEGV]` remains gone after memfs initializes `/tmp`, `/var`, and `/var/tmp`. |
| `libctest-glibc` | `GROUP END` line 3423 | Fails internally: first visible failing case is `FAIL clocale_mbfuncs [status 1]` at line 2320. Static and dynamic `daemon_failure` still fail their glibc-vs-musl semantic expectation, but no `[SEGV]` occurs. `pthread_robust_detach` no longer reports the old timeout/owner-death failure. |
| `busybox-glibc` | `GROUP END` line 3670 | Clean in this log: all focused busybox subtests report success, including `which ls` at line 3462 and shell `kill $!` at line 3478. |
| `cyclictest-glibc` | `GROUP END` line 3720 | Clean in this log: no `Creating fdpair` marker, and `kill hackbench` reports success at line 3719. |
| `netperf-glibc` | `GROUP END` line 3772 | Fails internally: `UDP_STREAM` reports `end: fail` at line 3732. Other focused netperf subtests complete without `end: fail`; the older netperf `[SEGV]` lines did not recur. |
| `iperf-glibc` | `GROUP END` line 3899 | Clean in this log: no focused failure marker in the group. |
| `iozone-glibc` | `GROUP END` line 4150 | Clean in this log: no focused failure marker in the group. |
| `lua-glibc` | `GROUP END` line 4167 | Clean in this log: no focused failure marker in the group. |
| `basic-glibc` | `GROUP END` line 4356 | Clean in this log: no focused failure marker in the group. |
| `unixbench-musl` | `GROUP START` line 4363, timeout line 4380, `test fail` line 4382 | Fails by initcode timeout after running benchmark output. The old early `can't create pipe: Bad file descriptor` marker remains gone. |
| `lmbench-musl` | `GROUP START` line 4387, timeout line 4399, `test fail` line 4401 | Fails by initcode timeout after `latency measurements`. The previous `cp: not found` marker remains gone. |
| `ltp-musl` | `GROUP START` line 4406, timeout line 5029, `test fail` line 5031 | Fails after running named LTP cases. `basename: not found` is gone; real failures now include unknown syscalls 89/217/219/171, `waitpid(...,0) failed: EINTR`, `wait() failed: EPERM`, missing `/proc/self/maps`, and network/protocol gaps. |
| `unixbench-glibc` | `GROUP START` line 5036, timeout line 5052, `test fail` line 5054 | Fails by initcode timeout after running benchmark output. The old early `can't create pipe: Bad file descriptor` marker remains gone. |
| `lmbench-glibc` | `GROUP START` line 5059, timeout line 5068, `test fail` line 5070 | Fails by initcode timeout after partial latency output. The previous `cp: not found` marker remains gone. |
| `ltp-glibc` | `GROUP START` line 5075, timeout line 5201, `test fail` line 5203 | Fails after running named LTP cases. `basename: not found` is gone; real failures include `waitpid(...,0) failed: EINTR`, `wait() failed: EPERM`, `accept03` O_PATH errno mismatch, missing `/proc/self/maps`, and timeout cleanup failures. |

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
busybox_subtest_fail=False
pipe_or_fd_resource_error=False
cp_not_found=False
basename_not_found=False
```

Strict current count: 16 clean groups, 8 failing groups. This deliberately treats busybox subtest `fail` lines and LTP `FAIL LTP CASE` lines as real failures instead of relying on `GROUP END` or wrapper markers.

## This Iteration

- `newfstatat(79)` now shares the existing BusyBox applet compatibility boundary used by `faccessat(48)` and `execve(221)`: stat probes for a bounded set of known BusyBox applet pathnames can stat the real BusyBox binary when no separate applet inode exists.
- Root cause: BusyBox `which` and shell PATH lookup call `stat()`/`newfstatat()` as well as `access()` before `execve()`. SeaOS already accepted applet `access()` and could execute applets through BusyBox, but stat-based discovery still returned `ENOENT`, so `which ls` failed and LTP shell wrappers printed `basename: not found` before the kernel ever reached the exec fallback.
- Result: `busybox-musl` and `busybox-glibc` are clean in the latest strict scan; LTP case names are now visible for both `/musl` and `/glibc`, replacing the previous repeated `basename: not found` noise.
- `libctest-glibc` still fails by known libc-test/glibc semantic and environment gaps, but no `[SEGV]` is present.
- `cyclictest-glibc` remains clean. `netperf-glibc` still has only `UDP_STREAM` failing.

## Remaining Real Gaps

Current failing groups in log order:

1. `libctest-glibc`: earliest marker is `FAIL clocale_mbfuncs [status 1]`; `daemon_failure` remains a glibc semantic mismatch, and other locale/wide-char/stdio/pthread cases still fail or time out.
2. `netperf-glibc`: only `UDP_STREAM` still reports `end: fail`.
3. `unixbench-musl`: benchmark runs but hits initcode timeout/test-fail.
4. `lmbench-musl`: latency measurements run but hit initcode timeout/test-fail.
5. `ltp-musl`: LTP runs with named cases but has internal failures, unknown syscall markers, wait/signal errors, `/proc/self/maps` gap, and timeout/test-fail.
6. `unixbench-glibc`: benchmark runs but hits initcode timeout/test-fail.
7. `lmbench-glibc`: latency measurements run but hits initcode timeout/test-fail.
8. `ltp-glibc`: LTP runs with named cases but has wait/signal errors, `/proc/self/maps` and socket errno gaps, cleanup failures, and timeout/test-fail.

Do not treat wrapper `test end` or `GROUP END` as testsuite success.

## Public-Path Risk Notes

This iteration touched `newfstatat(79)`, a shared filesystem/syscall path. Future work must regression-check with `make all` and the fixed RV docker command before committing.

The BusyBox applet stat fix is intentionally bounded. It does not create files, symlinks, or modify the protected image. It only reports executable-file stat metadata for known BusyBox applet names when the real applet path is absent but the actual BusyBox binary exists.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` must remain unmodified. Root-level temporary image files may be removed before fixed RV reruns and before commit staging.
