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

The latest regenerated RISC-V log was written on 2026-06-24 11:21:10 Asia/Shanghai. It reached `sys_shutdown: powering off via SBI SRST` at line 5026 after all 24 groups were enumerated. The docker/QEMU container was stopped after `sys_shutdown` had already appeared.

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
| `libctest-glibc` | `GROUP END` line 3424 | Fails internally. First visible failing case is still `FAIL clocale_mbfuncs [status 1]` at line 2320. Static `pthread_cancel` times out, dynamic pthread cancellation aborts because `libgcc_s.so.1` is not installed, and dynamic `daemon_failure` still reports the glibc-vs-musl daemon semantic mismatch. No `[SEGV]` occurs. |
| `busybox-glibc` | `GROUP END` line 3671 | Clean in this log: focused busybox subtests report success, including `which ls` and shell `kill $!`. |
| `cyclictest-glibc` | `GROUP END` line 3721 | Clean in this log: no `Creating fdpair` marker, and `kill hackbench` reports success. |
| `netperf-glibc` | `GROUP END` line 3773 | Fails internally, but improved: only `UDP_STREAM` reports `end: fail` at line 3732. The previous `TCP_STREAM`, `UDP_RR`, `TCP_RR`, and `TCP_CRR` `end: fail` markers are absent. No `[SEGV]` occurs. |
| `iperf-glibc` | `GROUP END` line 3900 | Clean in this log: no focused failure marker in the group. |
| `iozone-glibc` | `GROUP END` line 4151 | Fails internally: `Fork failed` appears seven times at lines 3969/3999/4029/4059/4089/4119/4150. |
| `lua-glibc` | `GROUP END` line 4168 | Clean in this log: no focused failure marker in the group. |
| `basic-glibc` | `GROUP END` line 4357 | Clean in this log: no focused failure marker in the group. |
| `unixbench-musl` | `GROUP START` line 4364, timeout line 4381, `test fail` line 4383 | Fails by initcode timeout after running benchmark output. No early `can't create pipe` marker. |
| `lmbench-musl` | `GROUP START` line 4388, timeout line 4402, `test fail` line 4404 | Fails by initcode timeout after partial latency output. No `cp: not found` marker. |
| `ltp-musl` | `GROUP START` line 4409, timeout line 4957, `test fail` line 4959 | Fails after running named LTP cases. The previous 33 `waitpid(...,0) failed: EINTR` lines are gone. Real failures remain: unknown syscalls 89/217/219/171, missing LTP shell helper paths, missing kernel config/proc data, socket/protocol gaps, and named LTP case failures. |
| `unixbench-glibc` | `GROUP START` line 4964, timeout line 4981, `test fail` line 4983 | Fails by initcode timeout after running benchmark output. No early `can't create pipe` marker. |
| `lmbench-glibc` | `GROUP START` line 4988, timeout line 4998, `test fail` line 5000 | Fails by initcode timeout after partial latency output. No `cp: not found` marker. |
| `ltp-glibc` | `GROUP START` line 5005, timeout line 5023, `test fail` line 5025 | Fails after starting `abort01`. The previous 4 `waitpid(...,0) failed: EINTR` lines are gone. Real failures remain: `abort01` coredump expectation fails, the group still hits wrapper timeout, and later glibc LTP cases are not reached in this run. |

Focused checks in the latest RISC-V log:

```text
all_24_groups_seen=True
sys_shutdown=True
panic=False
unknown_syscall=True              # LTP-only: 89, 217, 219, 171
function_not_implemented=False
interrupted_system_call=False
segv=False
wait_failed_eperm=False
waitpid_eintr=False               # was 37 total in the previous log
explicit_group_or_wrapper_fail=True
busybox_subtest_fail=False
pipe_or_fd_startup_error=False
cp_not_found=False
basename_not_found=False
fork_failed=True                  # iozone-glibc internal marker
netperf_end_fail=True             # netperf-glibc UDP_STREAM only
```

Strict current count: 15 clean groups, 9 failing groups. This deliberately treats internal `FAIL`, netperf `end: fail`, iozone `Fork failed`, benchmark timeouts, and LTP `FAIL LTP CASE` lines as real failures instead of relying on `GROUP END` or wrapper markers.

## This Iteration

- RISC-V signal delivery now supports minimal `SA_RESTART` for `wait4(260)`. The syscall dispatcher snapshots the current syscall number and original `a0`-`a5` arguments before the return value overwrites `a0`; signal delivery uses that snapshot only when a `wait4` returns `-EINTR` and the delivered handler has `SA_RESTART`.
- This fixes the LTP harness-level `waitpid(...,0) failed: EINTR` breakage without making futex, accept, nanosleep, or other interruptible syscalls restart implicitly. Those paths still return `-EINTR` where cancellation or timeout behavior depends on it.
- `SA_RESTART` is now defined in the RISC-V signal constants, and the restart snapshot is reset in `proc_alloc` with the rest of per-process signal state.
- `libctest-glibc` was investigated first. The current visible failures are not a small kernel-semantic patch: locale/stdio/fnmatch cases are glibc-vs-musl libc-test semantics, dynamic pthread cancellation aborts before kernel behavior because `libgcc_s.so.1` is absent, `daemon_failure` is explicitly documented by the test as musl-specific behavior, and static glibc `pthread_cancel` still needs future RISC-V signal-unwind/VDSO-compatible work.

## Remaining Real Gaps

Current failing groups in log order:

1. `libctest-glibc`: earliest marker is `FAIL clocale_mbfuncs [status 1]`; static `pthread_cancel` still times out, dynamic pthread cancellation aborts due missing `libgcc_s.so.1`, and several locale/stdio/regex cases fail under glibc libc semantics.
2. `netperf-glibc`: improved but still fails internally; only `UDP_STREAM end: fail` remains in the latest log.
3. `iozone-glibc`: internal `Fork failed` markers remain.
4. `unixbench-musl`: benchmark runs but hits initcode timeout/test-fail.
5. `lmbench-musl`: latency measurements run but hits initcode timeout/test-fail.
6. `ltp-musl`: LTP runs with named cases and no longer has harness `waitpid(...)=EINTR`, but still has unknown syscalls, helper/proc/config gaps, socket/protocol gaps, and many named case failures.
7. `unixbench-glibc`: benchmark runs but hits initcode timeout/test-fail.
8. `lmbench-glibc`: latency measurements run but hits initcode timeout/test-fail.
9. `ltp-glibc`: no longer has harness `waitpid(...)=EINTR`, but starts with `abort01` coredump failure and still hits wrapper timeout before later cases.

Do not treat wrapper `test end` or `GROUP END` as testsuite success.

## Public-Path Risk Notes

This iteration touched shared process, syscall, and signal paths: `src/kernel/proc/type.h`, `src/kernel/proc/proc.c`, `src/kernel/syscall/syscall.c`, and `src/kernel/trap/trap_user.c`. Future work must regression-check with `make all` and the fixed RV docker command before committing.

The `SA_RESTART` fix is intentionally narrow. It does not implement Linux restart blocks, queued signals, alternate signal stacks, VDSO `__vdso_rt_sigreturn`, signal-frame CFI, job control, or complete process-group signaling. It only restores the original syscall PC and arguments for `wait4` when the interrupted handler explicitly has restart semantics.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` must remain unmodified. Root-level temporary image files may be removed before fixed RV reruns and before commit staging.
