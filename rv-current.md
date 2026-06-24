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

Before each rerun, remove only the root-level temporary files `sdcard-rv.img`, `sdcard-la.img`, `sdcard-rv.img.gz`, and `sdcard-la.img.gz`. Do not modify the protected grader images under `data/`.

Generated log: `os_serial_out_rv.txt`

The latest regenerated RISC-V run was started with the fixed docker command on 2026-06-24 Asia/Shanghai. The serial log reached `sys_shutdown: powering off via SBI SRST` at line 4533 after all 24 groups were enumerated. The docker container was manually stopped only after the serial log had reached `sys_shutdown` and stopped growing.

## Important Interpretation Rule

`======== test end    ========` is an initcode/test-wrapper marker. It only proves that the wrapper reached its own cleanup point. It is not proof that the testsuite really passed.

Real status must come from the actual `testsuits-for-oskernel/` program and script output. Internal markers such as `FAIL`, `[SEGV]`, `end: fail`, `Function not implemented`, `Interrupted system call`, `panic`, pipe/open errors, or group-level `test fail` remain real defects even if a wrapper later prints its end marker.

## Current RV Evidence

RV initcode now keeps reading `SYS_get_dentries` until it returns `<= 0`, so short reads no longer stop directory enumeration early. To keep long or hanging groups from blocking later coverage, the current RV initcode runs shorter primary groups first and defers `unixbench`, `lmbench`, and `ltp` until after the primary `/musl` and `/glibc` groups.

| Test group | Latest RV log marker | Real testsuite status notes |
|---|---|---|
| `libcbench-musl` | `GROUP END` line 154 | Clean in this log: no focused `FAIL`, `[SEGV]`, `end: fail`, timeout, panic, or unknown syscall marker in the group. |
| `libctest-musl` | `GROUP END` line 813 | Clean in this log: no focused failure marker in the group. |
| `busybox-musl` | `GROUP END` line 1060 | Clean in this log: no focused failure marker in the group. |
| `cyclictest-musl` | `GROUP END` line 1108 | Clean in this log: no focused failure marker in the group. |
| `netperf-musl` | `GROUP END` line 1170 | Clean in this log: no focused failure marker in the group. |
| `iperf-musl` | `GROUP END` line 1296 | Clean in this log: no focused failure marker in the group. |
| `iozone-musl` | `GROUP END` line 1701 | Clean in this log. The previous `iozone.DUMMY.*: Operation not permitted` blocker is not present. |
| `lua-musl` | `GROUP END` line 1718 | Clean in this log: no focused failure marker in the group. |
| `basic-musl` | `GROUP END` line 1907 | Clean in this log: no focused failure marker in the group. |
| `libcbench-glibc` | `GROUP END` line 1992 | Fails internally: `[SEGV]` at lines 1981 and 1982. Do not count the wrapper/group end as success. |
| `libctest-glibc` | `GROUP END` line 3410 | Fails internally: many `FAIL`, `[SEGV]`, `timed out`, and `signal Aborted` markers, including `daemon_failure`, `lseek_large`, pthread, regex, stdio, locale, and wide-char cases. |
| `busybox-glibc` | `GROUP END` line 3656 | Clean in this log: no focused failure marker in the group. |
| `cyclictest-glibc` | `GROUP END` line 3704 | Clean in this log: no focused failure marker in the group. |
| `netperf-glibc` | `GROUP END` line 3741 | Fails internally: UDP/TCP stream and RR/CRR subtests report `end: fail`; several netperf processes hit `[SEGV]`. |
| `iperf-glibc` | `GROUP END` line 3868 | Clean in this log: no focused failure marker in the group. |
| `iozone-glibc` | `GROUP END` line 4273 | Clean in this log: no focused failure marker in the group. |
| `lua-glibc` | `GROUP END` line 4290 | Clean in this log: no focused failure marker in the group. |
| `basic-glibc` | `GROUP END` line 4478 | Clean in this log: no focused failure marker in the group. |
| `unixbench-musl` | `GROUP START` line 4485, wrapper `test fail` line 4488 | Fails before tests run: `/musl/unixbench_testcode.sh: line 5: can't create pipe: Bad file descriptor`. |
| `lmbench-musl` | `GROUP START` line 4493, timeout line 4496, `test fail` line 4498 | Fails by initcode timeout after `latency measurements`; this is a real incomplete group, not success. |
| `ltp-musl` | `GROUP START` line 4503, wrapper `test fail` line 4506 | Fails before tests run: `/musl/ltp_testcode.sh: line 13: can't create pipe: No file descriptors available`. |
| `unixbench-glibc` | `GROUP START` line 4511, wrapper `test fail` line 4514 | Fails before tests run: `/glibc/unixbench_testcode.sh: line 5: can't create pipe: Bad file descriptor`. |
| `lmbench-glibc` | `GROUP START` line 4519, timeout line 4522, `test fail` line 4524 | Fails by initcode timeout after `latency measurements`; this is a real incomplete group, not success. |
| `ltp-glibc` | `GROUP START` line 4529, wrapper `test fail` line 4532 | Fails before tests run: `/glibc/ltp_testcode.sh: line 13: can't create pipe: Too many open files`. |

Focused checks in the latest RISC-V log:

```text
all_24_groups_seen=True
sys_shutdown=True
panic=False
unknown_syscall=False
function_not_implemented=False
interrupted_system_call=False
operation_not_permitted=False
segv=True                      # glibc libcbench/libctest/netperf
explicit_group_or_wrapper_fail=True
```

## This Iteration

- `src/user/initcode.c` no longer treats a short `SYS_get_dentries` read as EOF. It keeps reading until `get_dentries` returns `<= 0`.
- RV now enumerates the full `/musl` 12 groups plus `/glibc` 12 groups, for 24 total groups.
- Newly exposed groups from the old RV short-read boundary include `libcbench-musl`, `libctest-musl`, `iozone-musl`, `lua-musl`, `basic-musl`, and the later `/glibc` groups `iperf-glibc`, `iozone-glibc`, `lua-glibc`, `basic-glibc`, `unixbench-glibc`, `lmbench-glibc`, and `ltp-glibc`.
- The previous `iozone.DUMMY.*: Operation not permitted` symptom is gone in this run.
- The user-visible naked `-1` errno leak has been reduced: pipe/open failures now report concrete errno such as `EBADF`, `EMFILE`, or `ENFILE`-style messages instead of misleading `Operation not permitted`. Remaining pipe failures are still real bugs and should be fixed next.
- Initcode per-test timeout lets RV continue after long `lmbench` paths. The timeout marker is deliberately recorded as failure, not success.

## Remaining Real Gaps

1. `libcbench-glibc`: first failing group in the current execution order; contains `[SEGV]` in pthread/memory-stress paths.
2. `libctest-glibc`: many real libc-test failures remain; focus after the earlier glibc SEGVs are understood.
3. `netperf-glibc`: all netperf subtests report `end: fail`, with repeated `[SEGV]` in the glibc netperf process.
4. `unixbench-musl` and `unixbench-glibc`: shell script fails creating a pipe with `Bad file descriptor`.
5. `lmbench-musl` and `lmbench-glibc`: still exceed the current per-test timeout at `latency measurements`.
6. `ltp-musl` and `ltp-glibc`: shell script fails creating a pipe because file descriptors are exhausted or unavailable.

Do not treat wrapper `test end` as testsuite success.

## Public-Path Risk Notes

This iteration modifies RV initcode plus shared kernel syscall, FS, process, memory, and trap paths. Public-path files touched include `src/kernel/syscall/type.h`, `src/kernel/syscall/syscall.c`, and `src/kernel/mem/type.h`; these are shared-risk paths under `AGENTS.md` and must be regression-checked with `make all` and the fixed RV docker command.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` were not modified. Root-level temporary image files created by the docker run were removed before the fixed RV rerun.
