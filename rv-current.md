# SeaOS RISC-V Current State (2026-06-21)

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

The latest regenerated RISC-V run used the fixed docker command from 2026-06-21 08:55:31 to 2026-06-21 10:49:32 Asia/Shanghai. The docker container exited with status 0, and the serial log reaches `sys_shutdown` at line 2880.

## Important Interpretation Rule

`======== test end    ========` is an initcode/test-wrapper marker written by this project. It only proves that the wrapper reached its own cleanup point. It is not proof that the testsuite really passed. Older logs may contain a misleading spelling; treat it as the same wrapper end marker, not as success evidence.

Real status must come from the actual `testsuits-for-oskernel/` program and script output. Internal markers such as `FAIL`, `[SEGV]`, `end: fail`, `Function not implemented`, `Interrupted system call`, `panic`, `fork fail`, `no more mmap`, `unknown syscall`, or group-level `test fail` remain real defects even if the wrapper later prints its end marker.

## Current RV Evidence

| Test group | Wrapper marker in latest log | Real testsuite status notes |
|---|---|---|
| `unixbench-musl` | `GROUP END` line 100, wrapper end line 102 | No focused failure marker in the latest run. |
| `busybox-musl` | `GROUP END` line 347, wrapper end line 349 | No focused failure marker in the latest run. |
| `cyclictest-musl` | `GROUP END` line 402, wrapper end line 404 | Includes `kill hackbench: success`. `SENDER: write (error: No error information)` appears while terminating hackbench workers at lines 395-399. |
| `netperf-musl` | `GROUP END` line 461, wrapper end line 463 | `UDP_STREAM`, `TCP_STREAM`, `UDP_RR`, `TCP_RR`, and `TCP_CRR` print `end: success`. |
| `lmbench-musl` | `GROUP END` line 509, wrapper end line 511 | No focused failure marker in the latest run. |
| `iperf-musl` | `GROUP END` line 635, wrapper end line 637 | No focused failure marker in the latest run. |
| `unixbench-glibc` | `GROUP END` line 670, wrapper end line 672 | No focused failure marker in the latest run. |
| `libcbench-glibc` | `GROUP END` line 758, wrapper end line 760 | Not clean. `free(): invalid pointer` remains at line 696, before `b_malloc_thread_stress`; the first pthread-area `[SEGV]` remains at line 730 (`pc=0x236a6`, `stval=0xf0`, `ra=0x112d8`). |
| `libctest-glibc` | `GROUP END` line 2499, wrapper end line 2501 | Not clean. Still has many real `FAIL` lines, `Interrupted system call` at line 2439, `[SEGV]` at lines 2037 and 2235, and the PC=0 cascade at lines 2466-2484. |
| `busybox-glibc` | `GROUP END` line 2746, wrapper end line 2748 | No focused failure marker in the latest run. |
| `cyclictest-glibc` | `GROUP END` line 2796, wrapper end line 2798 | Includes `kill hackbench: success`; group wrapper completes under hackbench pressure. |
| `netperf-glibc` | `GROUP END` line 2829, wrapper end line 2831 | Not clean. All five subtests still print `end: fail` at lines 2808, 2813, 2818, 2823, and 2828. Four subtests also report `[SEGV] pc=0x3ffb1392e0` at lines 2810, 2815, 2820, and 2825. |
| `lmbench-glibc` | `GROUP END` line 2877, wrapper end line 2879 | No focused failure marker in the latest run. |

Focused checks in the final log:

```text
panic=False
fork_fail=False
no_more_mmap=False
unknown_syscall=False
group_test_fail=False
pmem_alloc=False
segv=True
```

## This Iteration

- Retained one narrow RISC-V kernel change: `brk(214)` grow now synchronizes newly mapped heap leaves and `heap_top` to live same-`vm_owner` siblings. This is documented in `docs/DECISIONS.md` D34 and `docs/SYSCALL_STATUS.md`.
- Tested and reverted two narrower hypotheses because they did not improve `libcbench-glibc`: `munmap` sibling stale-PTE cleanup and `clone` tid-page prefault before sharing.
- Final evidence still shows the same first `libcbench-glibc` corruption path: `free(): invalid pointer` in the malloc-thread section, followed later by `[SEGV] pc=0x236a6 stval=0xf0` in the pthread create/join area. This suggests the first useful next step is to instrument or reason from the two-thread malloc stress path and shared address-space/page-table semantics, not to treat the wrapper end marker as success.

## Current Boundary

The RISC-V kernel currently boots, runs the visible script sequence through `lmbench-glibc`, and reaches `sys_shutdown` under the fixed docker command after regenerating `kernel-rv` with `make all`. That is a regression baseline, not a claim that every testsuite is clean.

Remaining real gaps to prioritize:

1. `libcbench-glibc` still has pthread/malloc corruption (`free(): invalid pointer`) and the first pthread-area `[SEGV]` at `pc=0x236a6`.
2. `libctest-glibc` still has many real `FAIL` lines, `Interrupted system call`, and multiple user `[SEGV]` reports, including the later PC=0 cascade.
3. `netperf-glibc` still has real UDP/TCP subtest failures (`end: fail`) plus current SEGVs in four subtests.

Do not hide these by relying on wrapper `test end` or the historical misleading spelling. The next useful work is to reduce one real internal failure at a time while keeping the current regression baseline from going backwards.

## Public-File Risk Notes

This iteration touches `src/kernel/syscall/sysfunc.c`, `src/kernel/proc/method.h`, `src/kernel/proc/proc.c`, `docs/SYSCALL_STATUS.md`, and `docs/DECISIONS.md`. Risk: `brk`, process, and page-table behavior are shared paths; mistakes can affect fork/clone, pthread shared address spaces, heap growth, or later mmap/munmap behavior. The final fixed docker run above is the regression evidence for the retained state.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` were not modified.
