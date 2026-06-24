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

The latest regenerated RISC-V log was written on 2026-06-24 19:05:58 Asia/Shanghai. It reached `sys_shutdown: powering off via SBI SRST` at line 5268 after all 24 groups were enumerated. The docker/QEMU container was stopped after `sys_shutdown` had already appeared.

## Run Monitoring Rule

During long fixed-docker reruns, inspect `os_serial_out_rv.txt` about every 15 minutes instead of waiting blindly on the wrapper command:

- If `sys_shutdown: powering off via SBI SRST` appears, treat the run as complete, stop any stale docker/QEMU container if it remains alive, then analyze the log.
- If the file size and mtime do not change for one 15-minute polling interval and no `sys_shutdown` appears, treat the run as stuck and stop waiting so the log can be analyzed as a hang.
- Never treat wrapper `======== test end ========`, `GROUP END`, or `FAIL LTP CASE ... : 0` as success by itself.

## Important Interpretation Rule

`======== test end    ========` is an initcode/test-wrapper marker. It only proves that the wrapper reached its own cleanup point. It is not proof that the testsuite really passed.

Real status must come from the actual `testsuits-for-oskernel/` program and script output. Internal markers such as `FAIL`, `[SEGV]`, `end: fail`, `Function not implemented`, `Interrupted system call`, `panic`, `test fail`, `test timeout`, `cp: not found`, pipe/open/fd errors, file-descriptor exhaustion, `Fork failed`, or busybox testcase `fail` lines remain real defects even if a wrapper later prints its end marker.

## Current RV Evidence

RV initcode enumerates the full `/musl` 12 groups plus `/glibc` 12 groups. The current log still defers `unixbench`, `lmbench`, and `ltp` until after the shorter primary `/musl` and `/glibc` groups.

| Test group | Latest RV log marker | Real testsuite status notes |
|---|---|---|
| `libcbench-musl` | `GROUP END` line 154 | Clean in this log: no focused failure marker in the group. |
| `libctest-musl` | `GROUP END` line 813 | Clean in this log: no focused failure marker in the group. Static and dynamic pthread cases reach END. |
| `busybox-musl` | `GROUP END` line 1060 | Clean in this log: focused busybox subtests report success, including `which ls` line 852 and shell `kill $!` line 868. |
| `cyclictest-musl` | `GROUP END` line 1110 | Clean in this log. `/dev/cpu_dma_latency` warnings remain optional cyclictest PM-QoS noise. |
| `netperf-musl` | `GROUP END` line 1168 | Clean in this log: all five netperf subtests report success at lines 1128-1167. |
| `iperf-musl` | `GROUP END` line 1294 | Clean in this log: no focused failure marker in the group. |
| `iozone-musl` | `GROUP END` line 1699 | Clean in this log: all visible iozone phases reach `iozone test complete`. |
| `lua-musl` | `GROUP END` line 1716 | Clean in this log: all Lua subtests report success. |
| `basic-musl` | `GROUP END` line 1905 | Clean in this log: no focused failure marker in the group. |
| `libcbench-glibc` | `GROUP END` line 1994 | Clean in this log. The previous stdio `[SEGV]` remains gone. |
| `libctest-glibc` | `GROUP END` line 3423 | Fails internally. First visible failing case is `FAIL clocale_mbfuncs [status 1]` at line 2319; multiple glibc libc semantic cases, `pthread_cancel`, and `sscanf_long` still fail or time out. No `[SEGV]` occurs. |
| `busybox-glibc` | `GROUP END` line 3670 | Clean in this log: focused busybox subtests report success, including `which ls` line 3462 and shell `kill $!` line 3478. |
| `cyclictest-glibc` | `GROUP END` line 3720 | Clean in this log: no focused failure marker. |
| `netperf-glibc` | `GROUP END` line 3772 | Fails internally: `UDP_STREAM` reports `end: fail` at line 3732; TCP_STREAM, UDP_RR, TCP_RR, and TCP_CRR all report success at lines 3741/3751/3761/3771. |
| `iperf-glibc` | `GROUP END` line 3899 | Clean in this log: no focused failure marker in the group. |
| `iozone-glibc` | `GROUP END` line 4304 | Clean in this log: no `Fork failed` marker; all visible iozone phases reach `iozone test complete`. |
| `lua-glibc` | `GROUP END` line 4321 | Clean in this log: all Lua subtests report success. |
| `basic-glibc` | `GROUP END` line 4510 | Clean in this log: no focused failure marker in the group. |
| `unixbench-musl` | `GROUP START` line 4517, timeout line 4534, `test fail` line 4536 | Fails by initcode timeout after running benchmark output. No early `can't create pipe` marker. |
| `lmbench-musl` | `GROUP START` line 4541, timeout line 4552, `test fail` line 4554 | Fails by initcode timeout after partial latency output. No `cp: not found` marker. |
| `ltp-musl` | `GROUP START` line 4560, timeout line 5201, `test fail` line 5203 | Fails after running named LTP cases. New targeted progress: `alarm02`, `alarm03`, `alarm05`, and `alarm06` now report only TPASS lines for their real assertions; alarm-related TFAIL count dropped from 6 to 0 versus the previous baseline. Existing progress remains: no unknown syscall 36/89/171/217/219; `access02` passes symlink setup and reports TPASS for file/symlink access checks before executable script behavior TFAIL; `access04` reports TPASS for all six errno checks as root and nobody; `adjtimex02` internal checks are TPASS. Real failures remain: `abort01`, `accept02` checkpoint timeout, `access01` child-result reporting, `access02` executable script behavior, kernel config/proc gaps, AF_ALG/AIO unsupported configs, protocol gaps, and shell helper failures. |
| `unixbench-glibc` | `GROUP START` line 5208, timeout line 5224, `test fail` line 5226 | Fails by initcode timeout after running benchmark output. No early `can't create pipe` marker. |
| `lmbench-glibc` | `GROUP START` line 5232, timeout line 5240, `test fail` line 5242 | Fails by initcode timeout after partial latency output. No `cp: not found` marker. |
| `ltp-glibc` | `GROUP START` line 5246, timeout line 5264, `test fail` line 5266 | Fails after starting `abort01`: coredump expectation fails, then the group hits wrapper timeout before later glibc LTP cases are reached. |

Focused checks in the latest RISC-V log:

```text
all_24_groups_seen=True
sys_shutdown=True
panic=False
unknown_syscall=False
function_not_implemented=False
interrupted_system_call=False
segv=False
wait_failed_eperm=False
waitpid_eintr=False
explicit_group_or_wrapper_fail=True
busybox_subtest_fail=False
pipe_or_fd_startup_error=False
cp_not_found=False
basename_not_found=False
fork_failed=False
netperf_end_fail=True             # netperf-glibc UDP_STREAM only
```

Strict current count: 16 clean groups, 8 failing groups. This deliberately treats internal `FAIL`, netperf `end: fail`, benchmark timeouts, and LTP `FAIL LTP CASE` lines as real failures instead of relying on `GROUP END` or wrapper markers.

## This Iteration

- `getitimer(102)` now reports the current `ITIMER_REAL` interval and remaining value instead of zero-filling the user buffer.
- `setitimer(103)` now fills `old_value`, validates user pointers and microsecond fields, and keeps `ITIMER_REAL` delivery through `proc_t.itimer_expire/interval`.
- `setitimer(..., old_value)` reports the active remaining value in coarse seconds for the current 0.1s SeaOS timer granularity. This preserves immediate `alarm(sec); alarm(0)` results and avoids musl `alarm()` returning one extra second after `sleep(1)`.
- The fixed RV docker rerun reached `sys_shutdown` at line 5267. The 24-group status remains unchanged from the baseline: 24 groups seen, 18 groups with `GROUP END`, and 6 timeout/test-fail wrapper markers.
- LTP alarm evidence improved: alarm-related TFAIL lines dropped from 6 to 0, and alarm-related TPASS lines rose from 7 to 13. `alarm02`, `alarm03`, `alarm05`, and `alarm06` now all pass their visible real assertions in `ltp-musl`.

## Remaining Real Gaps

Current failing groups in log order:

1. `libctest-glibc`: earliest marker is `FAIL clocale_mbfuncs [status 1]`; static/dynamic cancellation, stdio, locale, DNS, and regex cases still fail or time out under glibc libc semantics.
2. `netperf-glibc`: `UDP_STREAM end: fail` remains; TCP_STREAM, UDP_RR, TCP_RR, and TCP_CRR are success in the latest log.
3. `unixbench-musl`: benchmark runs but hits initcode timeout/test-fail.
4. `lmbench-musl`: latency measurements run but hits initcode timeout/test-fail.
5. `ltp-musl`: early syscall/socket/procfs/passwd/symlink/access/adjtimex/alarm blockers improved, but real failures remain in coredump, checkpointing, child result reporting, executable script behavior, config/proc exposure, AF/protocol support, and helper scripts.
6. `unixbench-glibc`: benchmark runs but hits initcode timeout/test-fail.
7. `lmbench-glibc`: latency measurements run but hits initcode timeout/test-fail.
8. `ltp-glibc`: starts with `abort01` coredump failure and hits wrapper timeout before later cases.

Do not treat wrapper `test end`, `GROUP END`, or `FAIL LTP CASE ... : 0` as testsuite success.

## Public-Path Risk Notes

This iteration touched the shared syscall implementation path: `src/kernel/syscall/sysfunc.c`. Future work must regression-check with `make all` and the fixed RV docker command before committing semantic changes.

The compatibility work is intentionally narrow. It does not implement BSD process accounting, Linux key retention, real adjtimex clock discipline, full `O_PATH`/`open_tree` semantics, full VFS symlink/component resolution, mount namespaces, saved uid/gid/capabilities, full VIRTUAL/PROF interval timers, or a full procfs maps implementation for arbitrary target processes.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` must remain unmodified. Root-level temporary image files may be removed before fixed RV reruns and before commit staging.
