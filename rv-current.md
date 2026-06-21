# SeaOS RISC-V Current State (2026-06-22)

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

The latest regenerated RISC-V run used the fixed docker command from 2026-06-21 23:31:00 to 2026-06-22 01:33:26 Asia/Shanghai. The docker container exited with status 0, completed under the 2.5 hour cap, and the serial log reaches `sys_shutdown` at line 2865.

## Important Interpretation Rule

`======== test end    ========` is an initcode/test-wrapper marker. It only proves that the wrapper reached its own cleanup point. It is not proof that the testsuite really passed.

Real status must come from the actual `testsuits-for-oskernel/` program and script output. Internal markers such as `FAIL`, `[SEGV]`, `end: fail`, `Function not implemented`, `Interrupted system call`, `panic`, `fork fail`, `no more mmap`, `unknown syscall`, or group-level `test fail` remain real defects even if the wrapper later prints its end marker.

## Current RV Evidence

| Test group | Wrapper marker in latest log | Real testsuite status notes |
|---|---|---|
| `unixbench-musl` | `GROUP END` line 100, wrapper end line 102 | No focused failure marker in the latest run. |
| `busybox-musl` | `GROUP END` line 347, wrapper end line 349 | No focused failure marker in the latest run. |
| `cyclictest-musl` | `GROUP END` line 397, wrapper end line 399 | Includes `kill hackbench: success`. |
| `netperf-musl` | `GROUP END` line 455, wrapper end line 457 | `UDP_STREAM`, `TCP_STREAM`, `UDP_RR`, `TCP_RR`, and `TCP_CRR` print `end: success`. |
| `lmbench-musl` | `GROUP END` line 503, wrapper end line 505 | No focused failure marker in the latest run. |
| `iperf-musl` | `GROUP END` line 629, wrapper end line 631 | No focused failure marker in the latest run. |
| `unixbench-glibc` | `GROUP END` line 664, wrapper end line 666 | No focused failure marker in the latest run. |
| `libcbench-glibc` | `GROUP END` line 751, wrapper end line 753 | Improved but not clean. The earlier `free(): invalid pointer` lines are gone. The first remaining internal failure is `[SEGV]` line 723 (`pc=0x236a6`, `stval=0xf0`, `ra=0x112d8`) after `b_pthread_createjoin_serial1`, before `b_pthread_create_serial1`. |
| `libctest-glibc` | `GROUP END` line 2494, wrapper end line 2496 | Not clean. Many `FAIL` lines remain. Current first true failure is `FAIL clocale_mbfuncs [status 1]` at line 1079. `Interrupted system call` remains at lines 1173, 1232, and 1581. Many status-127 failures remain later in static tests. |
| `busybox-glibc` | `GROUP END` line 2741, wrapper end line 2743 | No focused failure marker in the latest run. |
| `cyclictest-glibc` | `GROUP END` line 2791, wrapper end line 2793 | Includes `kill hackbench: success`; group wrapper completes under hackbench pressure. |
| `netperf-glibc` | `GROUP END` line 2824, wrapper end line 2826 | Not clean. `UDP_STREAM`, `TCP_STREAM`, `UDP_RR`, `TCP_RR`, and `TCP_CRR` still print `end: fail` at lines 2803, 2808, 2813, 2818, and 2823. Four subtests also report `[SEGV] pc=0x3ffb1392e0` at lines 2805, 2810, 2815, and 2820. |
| `lmbench-glibc` | `GROUP END` line 2862, wrapper end line 2864 | No focused failure marker in the latest run. |

Focused checks in the final log:

```text
sys_shutdown=True
panic=False
pmem_alloc=False
fork_fail=False
no_more_mmap=False
unknown_syscall=False
group_test_fail=False
free_invalid_pointer=False
segv=True
```

## This Iteration

- Retained D34: `brk(214)` grow synchronizes newly mapped heap leaves and `heap_top` to live same-`vm_owner` siblings.
- Added D35: mmap lazy faults now reuse/synchronize the same PA for the same VA across live `CLONE_VM` siblings; `munmap` clears live sibling PTEs and frees the PA once; `proc_free()` avoids freeing shared leaves while same-owner siblings are still live.
- This removes the earliest `libcbench-glibc` malloc-thread symptom (`free(): invalid pointer`) that previously appeared immediately after `b_malloc_big2`.
- It does not hide or fix the later `libcbench-glibc` pthread-area `[SEGV] pc=0x236a6 stval=0xf0`; that remains the next libcbench target.

## Remaining Real Gaps

1. `libcbench-glibc`: first remaining real failure is `[SEGV]` at line 723, `pc=0x236a6`, `stval=0xf0`, after `b_pthread_createjoin_serial1`.
2. `libctest-glibc`: many true `FAIL` lines remain, starting with `clocale_mbfuncs`; `Interrupted system call` remains in wrapper wait paths.
3. `netperf-glibc`: all five real subtests still end in `fail`, with four repeated user `[SEGV]` reports in the response path.

Do not treat wrapper `test end` as testsuite success.

## Public-Path Risk Notes

This iteration investigated `libcbench-glibc` line 723 and tried a stricter `PROT_NONE`/`mprotect` propagation patch. The strict `PROT_NONE` trial hung in `libcbench-glibc` under the 2.5 hour cap; the narrower `mprotect` propagation trial did not improve the libcbench `[SEGV]` and introduced a `cyclictest-glibc` `kill hackbench: fail` regression, so both code changes were rolled back. The retained source state is the prior D35 baseline; only docs/status/log files were updated in this round.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` were not modified.
