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

The latest regenerated RISC-V log was written on 2026-06-24 10:09:13 Asia/Shanghai. It reached `sys_shutdown: powering off via SBI SRST` at line 5232 after all 24 groups were enumerated. The docker/QEMU container was stopped only after `sys_shutdown` had already appeared.

## Run Monitoring Rule

During long fixed-docker reruns, inspect `os_serial_out_rv.txt` about every 15 minutes instead of waiting blindly on the wrapper command:

- If `sys_shutdown: powering off via SBI SRST` appears, treat the run as complete, stop any stale docker/QEMU container if it remains alive, then analyze the log.
- If the file size and mtime do not change for one 15-minute polling interval and no `sys_shutdown` appears, treat the run as stuck and stop waiting so the log can be analyzed as a hang.
- Never treat wrapper `======== test end ========` or `GROUP END` as success by itself.

## Important Interpretation Rule

`======== test end    ========` is an initcode/test-wrapper marker. It only proves that the wrapper reached its own cleanup point. It is not proof that the testsuite really passed.

Real status must come from the actual `testsuits-for-oskernel/` program and script output. Internal markers such as `FAIL`, `[SEGV]`, `end: fail`, `Function not implemented`, `Interrupted system call`, `panic`, `test fail`, `test timeout`, `cp: not found`, pipe/open/fd errors, file-descriptor exhaustion, `Fork failed`, or busybox testcase `fail` lines remain real defects even if a wrapper later prints its end marker.

## Current RV Evidence

RV initcode enumerates the full `/musl` 12 groups plus `/glibc` 12 groups. The current log still defers `unixbench`, `lmbench`, and `ltp` until after the shorter primary `/musl` and `/glibc` groups.

| Test group | Latest RV log marker | Real testsuite status notes |
|---|---|---|
| `libcbench-musl` | `GROUP END` line 154 | Clean in this log: no focused failure marker in the group. |
| `libctest-musl` | `GROUP END` line 813 | Clean in this log: no focused failure marker in the group. Static and dynamic `pthread_robust_detach` both reach END. |
| `busybox-musl` | `GROUP END` line 1060 | Clean in this log: focused busybox subtests report success, including `which ls` and shell `kill $!`. |
| `cyclictest-musl` | `GROUP END` line 1111 | Clean in this log. `/dev/cpu_dma_latency` warnings are optional cyclictest PM-QoS noise and are not treated as failures by themselves. |
| `netperf-musl` | `GROUP END` line 1169 | Clean in this log: all netperf subtests report success. |
| `iperf-musl` | `GROUP END` line 1295 | Clean in this log: no focused failure marker in the group. |
| `iozone-musl` | `GROUP END` line 1700 | Clean in this log: no focused failure marker in the group. |
| `lua-musl` | `GROUP END` line 1717 | Clean in this log: no focused failure marker in the group. |
| `basic-musl` | `GROUP END` line 1906 | Clean in this log: no focused failure marker in the group. |
| `libcbench-glibc` | `GROUP END` line 1995 | Clean in this log. The previous stdio `[SEGV]` remains gone after memfs initializes `/tmp`, `/var`, and `/var/tmp`. |
| `libctest-glibc` | `GROUP END` line 3424 | Fails internally: first visible failing case is `FAIL clocale_mbfuncs [status 1]` at line 2320. Static `pthread_cancel` times out, dynamic pthread cancellation aborts because `libgcc_s.so.1` is not installed, and dynamic `daemon_failure` still reports the glibc-vs-musl daemon semantic mismatch. No `[SEGV]` occurs. |
| `busybox-glibc` | `GROUP END` line 3671 | Clean in this log: focused busybox subtests report success, including `which ls` and shell `kill $!`. |
| `cyclictest-glibc` | `GROUP END` line 3721 | Clean in this log: no `Creating fdpair` marker, and `kill hackbench` reports success. |
| `netperf-glibc` | `GROUP END` line 3755 | Fails internally: `UDP_STREAM`, `TCP_STREAM`, `UDP_RR`, `TCP_RR`, and `TCP_CRR` all report `end: fail` at lines 3733/3738/3743/3748/3753. No `[SEGV]` occurs. |
| `iperf-glibc` | `GROUP END` line 3882 | Clean in this log: no focused failure marker in the group. |
| `iozone-glibc` | `GROUP END` line 4177 | Fails internally: `Fork failed` appears six times at lines 4018/4048/4078/4108/4145/4176. |
| `lua-glibc` | `GROUP END` line 4194 | Clean in this log: no focused failure marker in the group. |
| `basic-glibc` | `GROUP END` line 4383 | Clean in this log: no focused failure marker in the group. |
| `unixbench-musl` | `GROUP START` line 4390, timeout line 4407, `test fail` line 4409 | Fails by initcode timeout after running benchmark output. The old early `can't create pipe: Bad file descriptor` marker remains gone. |
| `lmbench-musl` | `GROUP START` line 4414, timeout line 4424, `test fail` line 4426 | Fails by initcode timeout after `latency measurements`. The previous `cp: not found` marker remains gone. |
| `ltp-musl` | `GROUP START` line 4431, timeout line 5051, `test fail` line 5053 | Fails after running named LTP cases. Real failures include unknown syscalls 89/217/219/171, 33 `waitpid(...,0) failed: EINTR` lines, missing `/proc/self/maps`, missing `nobody` passwd data, missing LTP shell helper paths, and network/protocol gaps. |
| `unixbench-glibc` | `GROUP START` line 5058, timeout line 5075, `test fail` line 5077 | Fails by initcode timeout after running benchmark output. The old early `can't create pipe: Bad file descriptor` marker remains gone. |
| `lmbench-glibc` | `GROUP START` line 5082, timeout line 5094, `test fail` line 5096 | Fails by initcode timeout after partial latency output. The previous `cp: not found` marker remains gone. |
| `ltp-glibc` | `GROUP START` line 5101, timeout line 5229, `test fail` line 5231 | Fails after running named LTP cases. Real failures include 4 `waitpid(...,0) failed: EINTR` lines, `accept01` UDP errno mismatch, `accept03` O_PATH errno mismatch, missing `/proc/self/maps`, and timeout cleanup failures. |

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
wait_failed_eperm=False
waitpid_eintr=True                # 37 occurrences: 33 musl LTP, 4 glibc LTP
explicit_group_or_wrapper_fail=True
busybox_subtest_fail=False
pipe_or_fd_startup_error=False
cp_not_found=False
basename_not_found=False
fork_failed=True                  # iozone-glibc internal marker
```

Strict current count: 15 clean groups, 9 failing groups. This deliberately treats internal `FAIL`, netperf `end: fail`, iozone `Fork failed`, benchmark timeouts, and LTP `FAIL LTP CASE` lines as real failures instead of relying on `GROUP END` or wrapper markers.

## This Iteration

- RISC-V signal metadata now tracks `si_code` and sender pid for pending signals. `kill` records `SI_USER`, `tkill`/`tgkill` record `SI_TKILL`, `SIGCHLD` records `CLD_EXITED`, `SIGALRM` records `SI_KERNEL`, and user page-fault SIGSEGV records `SEGV_MAPERR`.
- `getpid(172)` now returns the thread-group leader pid for `CLONE_THREAD` members, while `gettid(178)` remains the per-thread id. This matches the Linux distinction used by glibc pthread cancellation paths.
- `rt_sigtimedwait(137)` now copies the stored `si_code` and `si_pid` into the returned `siginfo_t` instead of always zeroing them.
- A Linux-shaped RISC-V signal-frame experiment was tried while debugging static glibc `pthread_cancel`, but it regressed `libctest-musl` with `panic! uvm_copyin: invalid user address`; that experiment was reverted before the final build and docker run. The retained signal frame remains the existing SeaOS frame layout.
- Focused evidence from the reverted experiment showed that static glibc `pthread_cancel` reaches SIGCANCEL delivery, but full cancellation still needs a future RISC-V signal-unwind/VDSO-compatible trampoline path. This iteration does not make `libctest-glibc` pass.

## Remaining Real Gaps

Current failing groups in log order:

1. `libctest-glibc`: earliest marker is `FAIL clocale_mbfuncs [status 1]`; static `pthread_cancel` still times out, dynamic pthread cancellation aborts due missing `libgcc_s.so.1`, and several locale/stdio/regex cases fail under glibc libc semantics.
2. `netperf-glibc`: all five focused netperf subtests report `end: fail`; no current `[SEGV]`.
3. `iozone-glibc`: internal `Fork failed` markers remain.
4. `unixbench-musl`: benchmark runs but hits initcode timeout/test-fail.
5. `lmbench-musl`: latency measurements run but hits initcode timeout/test-fail.
6. `ltp-musl`: LTP runs with named cases but has internal failures, unknown syscall markers, repeated `waitpid(...)=EINTR`, `/proc/self/maps` gap, passwd/helper-file gaps, network/protocol gaps, and timeout/test-fail.
7. `unixbench-glibc`: benchmark runs but hits initcode timeout/test-fail.
8. `lmbench-glibc`: latency measurements run but hits initcode timeout/test-fail.
9. `ltp-glibc`: LTP runs with named cases but has repeated `waitpid(...)=EINTR`, `/proc/self/maps` and socket errno gaps, cleanup failures, and timeout/test-fail.

Do not treat wrapper `test end` or `GROUP END` as testsuite success.

## Public-Path Risk Notes

This iteration touched shared process, syscall, and signal paths: `src/kernel/proc/type.h`, `src/kernel/proc/proc.c`, `src/kernel/proc/exec.c`, `src/kernel/syscall/sysfunc.c`, and `src/kernel/trap/trap_user.c`. Future work must regression-check with `make all` and the fixed RV docker command before committing.

The signal metadata fix is intentionally narrow. It does not implement full Linux signal queues, SA_RESTART, alternate signal stacks, VDSO `__vdso_rt_sigreturn`, signal-frame CFI, or complete thread-group signal disposition. It only preserves enough `siginfo_t` source metadata for current Linux/RISC-V compatibility work without changing the existing SeaOS signal-frame layout.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` must remain unmodified. Root-level temporary image files may be removed before fixed RV reruns and before commit staging.
