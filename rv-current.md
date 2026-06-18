# SeaOS RISC-V Current State (2026-06-18)

## Baseline Command

Current evidence comes from the required docker command:

```powershell
docker run --rm `
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" `
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" `
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```

Before each rerun, stale root-level generated files from interrupted grader runs were removed: `sdcard-rv.img`, `sdcard-la.img`, `sdcard-rv.img.gz`, and `sdcard-la.img.gz`. The protected images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` were not modified.

The final RISC-V serial log reached `sys_shutdown`. After that point only the LoongArch QEMU thread was still running, so the lingering container was stopped manually. RISC-V validation is based on the generated `os_serial_out_rv.txt`.

## Current RV Evidence

Generated log: `os_serial_out_rv.txt`

| Test group | Current result | Evidence |
|---|---|---|
| `unixbench-musl` | Pass | Reached `#### OS COMP TEST GROUP END unixbench-musl ####` and `test sucess`. |
| `busybox-musl` | Pass | Reached `#### OS COMP TEST GROUP END busybox-musl ####` and `test sucess`. |
| `cyclictest-musl` | Pass | Reached `#### OS COMP TEST GROUP END cyclictest-musl ####`, including `kill hackbench: success`, and `test sucess`. |
| `netperf-musl` | Pass | Reached `#### OS COMP TEST GROUP END netperf-musl ####` and `test sucess`. |
| `lmbench-musl` | Pass | Reached `#### OS COMP TEST GROUP END lmbench-musl ####` and `test sucess`. |
| `iperf-musl` | Pass | Reached `#### OS COMP TEST GROUP END iperf-musl ####` and `test sucess`. |
| `unixbench-glibc` | Pass | Reached `#### OS COMP TEST GROUP END unixbench-glibc ####` and `test sucess`. |
| `libcbench-glibc` | Pass | Reached `#### OS COMP TEST GROUP END libcbench-glibc ####` and `test sucess`. |
| `libctest-glibc` | Pass | Reached `#### OS COMP TEST GROUP END libctest-glibc ####` and `test sucess`; `unknown syscall 137` remains but does not fail this group. |
| `busybox-glibc` | Pass | Reached `#### OS COMP TEST GROUP END busybox-glibc ####` and `test sucess`. |
| `cyclictest-glibc` | Pass | Reached `#### OS COMP TEST GROUP END cyclictest-glibc ####`, including `kill hackbench: success`, and `test sucess`. |
| `netperf-glibc` | Pass | Reached `#### OS COMP TEST GROUP END netperf-glibc ####` and `test sucess`. |
| `lmbench-glibc` | Pass | Reached `#### OS COMP TEST GROUP END lmbench-glibc ####` and `test sucess`. |

No `panic`, `no more mmap`, `fork fail`, or `test fail` marker is present in the final RISC-V log. Some libc/libctest/netperf subprocesses still print internal `[SEGV]`, `unknown syscall 137`, or netperf `end: fail` lines, but the current grader-level group markers all reach `test sucess`.

## Current Boundary

The previously failing `cyclictest-glibc` group now passes under hackbench pressure. The final visible RISC-V run reaches `sys_shutdown` after `lmbench-glibc`.

The next useful work is not to preserve a newly failing group, but to reduce remaining compatibility noise:

- implement or minimally stub `rt_sigtimedwait(137)` so libc-test wrappers stop reporting `Function not implemented`;
- investigate glibc netperf's internal `getprotobyname` / netserver-control failures even though the group currently grades as success;
- eventually replace the fixed kernel/user page split with a safer unified or reclaiming allocator.

## Public-File Risk Notes

This iteration touches the AGENTS-listed public `Makefile`; risk is build-system regression from dependency include behavior. It also changes `src/kernel/mem/type.h` and `src/kernel/proc/type.h`, memory/process layout headers that can affect allocator pressure and process lifecycle behavior. The mitigation is a clean rebuild plus the required docker RISC-V run above.

The forbidden grader images `data/sdcard-rv.img.gz` and `data/sdcard-la.img.gz` were not modified.
