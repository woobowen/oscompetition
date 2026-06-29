# LoongArch Task Matrix

> Last updated: 2026-06-26.
>
> Scope: SeaOS LoongArch (`kernel-la`) contest test planning and handoff prompt.
> This is a task/status document, not a design doc or implementation plan.

## Ground Rules

- Use the official Docker image `zhouzhouyi/os-contest:20260510`.
- Use the container-local official QEMU:
  `/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64`.
- Do not use host QEMU as acceptance evidence.
- Do not modify `autotest-for-oskernel/`, `data/sdcard-rv.img.gz`,
  `data/sdcard-la.img.gz`, official test scripts, or official test binaries.
- Do not hide real failures by suppressing logs, faking output, hardcoding
  scores, skipping user programs, or treating wrapper markers as success.
- A group is clean only when its real output has no `FAIL`, `[SEGV]`, panic,
  trap failure, unknown syscall, `Function not implemented`,
  `Interrupted system call`, or group-specific failure marker.
- LoongArch changes should stay under `src/kernel/loongarch/` and
  `src/user/initcode_la.c` unless a public change is truly required.
- If a public/shared file changes, record the RISC-V regression risk.

## Total Contest Tasks

The LoongArch test image contains 24 big test groups:

- `/musl`: 12 groups
- `/glibc`: 12 groups

The local `autotest-for-oskernel/kernel/judge/` directory contains 22 judge
scripts. It has judge scripts for every group except `unixbench-musl` and
`unixbench-glibc`.

Therefore:

- Image/script-level total: 24 big groups.
- Local judge-entry total: 22 judged groups.
- If someone says "13 remaining", the likely meaning is: 24 image/script
  groups minus the 11 completed `/musl` image groups. The local judged
  remaining count is 12 because 22 judge scripts exist and 10 judged `/musl`
  groups are clean; `unixbench-musl` is clean at image/script level but has no
  local judge script.

## Full Group List

| Group | Libc | Judge script | Current LoongArch status |
|---|---|---:|---|
| `libcbench-musl` | musl | yes | Clean in latest full musl run. |
| `libctest-musl` | musl | yes | Clean in latest full musl run. |
| `busybox-musl` | musl | yes | Clean in latest full musl run. |
| `cyclictest-musl` | musl | yes | Clean in `/tmp/seaos_la_cyclictest_ticktime_clean1.log`; `NO_STRESS_P1/P8` and `STRESS_P1/P8` all end success, `kill hackbench` succeeds, GROUP END/shutdown reached, and broad failure scan is empty. |
| `netperf-musl` | musl | yes | Clean in focused and latest full musl run. |
| `iperf-musl` | musl | yes | Clean in focused official-QEMU run. |
| `iozone-musl` | musl | yes | Clean in `/tmp/seaos_la_iozone_mem_budget3.log`; reaches GROUP END/shutdown and hard failure scan is empty. |
| `lua-musl` | musl | yes | Clean in focused official-QEMU run. |
| `basic-musl` | musl | yes | Clean in focused official-QEMU run after LoongArch non-`CLONE_VM` clone stack fix. |
| `unixbench-musl` | musl | no | Clean in `/tmp/seaos_la_unixbench_final2.log`; no local judge script. |
| `lmbench-musl` | musl | yes | Clean in `/tmp/seaos_la_lmbench_execshare2.log`; full focused official script reaches GROUP END and broad failure scan is empty. |
| `ltp-musl` | musl | yes | Not clean. Latest focused LTP order `/tmp/seaos_la_ltp_restart1.log` shows the new wait4 restart fix removes the previous `waitpid(...)=EINTR` TBROK layer (`waitpid.*EINTR` count 0); `capget01`/`capget02` and `capset01`-`capset04` remain real TPASS/ret 0. Earlier bind/ar/arping/proc/helper evidence remains valid. The group still has wrapper `FAIL LTP CASE`, known `asapi_01` `hopopt` libc-table TFAIL, `bind06` namespace-config TCONF, and cgroup controller/helper blockers, including no-arg `cgroup_regression_3_1.sh` infinite helper loop. |
| `libcbench-glibc` | glibc | yes | Deferred until musl is clean. |
| `libctest-glibc` | glibc | yes | Deferred until musl is clean. |
| `busybox-glibc` | glibc | yes | Deferred until musl is clean. |
| `cyclictest-glibc` | glibc | yes | Deferred until musl is clean. |
| `netperf-glibc` | glibc | yes | Deferred until musl is clean. |
| `iperf-glibc` | glibc | yes | Deferred until musl is clean. |
| `iozone-glibc` | glibc | yes | Deferred until musl is clean. |
| `lua-glibc` | glibc | yes | Deferred until musl is clean. |
| `basic-glibc` | glibc | yes | Deferred until musl is clean. |
| `unixbench-glibc` | glibc | no | Deferred until musl is clean; no local judge script. |
| `lmbench-glibc` | glibc | yes | Deferred until musl is clean. |
| `ltp-glibc` | glibc | yes | Deferred until musl is clean. |

## Current Progress

Current priority is LoongArch `/musl`; glibc is intentionally deferred.

Verified as clean:

- `libcbench-musl`
- `libctest-musl`
- `unixbench-musl`
- `busybox-musl`
- `netperf-musl`
- `iperf-musl`
- `iozone-musl`
- `lua-musl`
- `basic-musl`
- `lmbench-musl`
- `cyclictest-musl`

Latest evidence:

- Build command:
  `docker exec seaos-la bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'`
- QEMU command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 5400 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_musl_after_netperf_fix.log 2>&1'; echo exit=$?`
- Netperf focused QEMU command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 420 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_netperf_sigabi_accept2.log 2>&1'; echo exit=$?`
- Iperf focused QEMU command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 900 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_iperf_focus.log 2>&1'; echo exit=$?`
- Lua focused QEMU command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 360 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_lua_focus.log 2>&1'`
- Basic focused QEMU command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 600 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_basic_clone_fix.log 2>&1'`
- Lmbench focused QEMU command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 2400 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_lmbench_execshare2.log 2>&1'; echo exit=$?`
- Iozone focused QEMU command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 900 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_iozone_mem_budget3.log 2>&1'; echo exit=$?`
- Cyclictest focused QEMU command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 420 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_cyclictest_ticktime_clean1.log 2>&1'; echo exit=$?`
  The log reaches `#### OS COMP TEST GROUP END cyclictest-musl ####` and
  `shutdown: system halting`; `NO_STRESS_P1/P8`, `STRESS_P1/P8`, and
  `kill hackbench` are all success, and the broad failure scan is empty.
- Socket regression recheck logs after `LA_NSOCK=256`:
  `/tmp/seaos_la_netperf_sock256_recheck.log` and
  `/tmp/seaos_la_iperf_sock256_recheck.log`; both reach GROUP END and have
  empty hard failure scans.
- Socket/iozone regression recheck logs after dynamic socket/pipe buffers:
  `/tmp/seaos_la_netperf_dynsock512_recheck.log`,
  `/tmp/seaos_la_iperf_dynsock512_recheck.log`, and
  `/tmp/seaos_la_iozone_dynsock512_recheck.log`; all reach GROUP END and have
  no hard failure markers.
- Restored full `/musl` smoke after iozone memory-budget fix:
  `/tmp/seaos_la_musl_restore_after_iozone.log`; it reaches
  `libcbench-musl` and `libctest-musl` GROUP END, enters `unixbench-musl`,
  exits by timeout 124 at 180 seconds, and has an empty hard failure scan up
  to that cutoff. It is not full `/musl` pass evidence.
- Restored full `/musl` smoke after the LTP raw-socket work:
  `/tmp/seaos_la_musl_restore_after_ltp_rawsock.log`; it reaches
  `libcbench-musl` and `libctest-musl` GROUP END, enters `unixbench-musl`,
  stops after CONTEXT by timeout 124 at 180 seconds, and has no `FAIL`,
  `[SEGV]`, `trap:`, `panic`, `unknown syscall`, or `Function not implemented`
  before timeout. It does contain existing nonzero child `exit: pid ... code=1`
  debug lines, so it is only a full-entry smoke, not full `/musl` pass evidence.
- Restored full `/musl` smoke after the LTP ancillary work:
  `/tmp/seaos_la_musl_after_ltp_ancillary_smoke.log`; it reaches
  `libcbench-musl` and `libctest-musl` GROUP END, enters `unixbench-musl`,
  exits by timeout 124 at 180 seconds, and has an empty broad failure scan.
  It is only a full-entry smoke, not full `/musl` pass evidence.
- LTP focused QEMU logs:
  `/tmp/seaos_la_ltp_focus.log`,
  `/tmp/seaos_la_ltp_fchmodat.log`,
  `/tmp/seaos_la_ltp_fchown_setpgid.log`,
  `/tmp/seaos_la_ltp_env_stubs.log`,
  `/tmp/seaos_la_ltp_identity_access.log`,
  `/tmp/seaos_la_ltp_abort_coredump.log`,
  `/tmp/seaos_la_ltp_accept03_fix.log`,
  `/tmp/seaos_la_ltp_symlink_red.log`,
  `/tmp/seaos_la_ltp_symlink_green.log`,
  `/tmp/seaos_la_ltp_symlink_wrapper_green.log`,
  `/tmp/seaos_la_ltp_access02_clean.log`,
  `/tmp/seaos_la_ltp_rawsock1.log`,
  `/tmp/seaos_la_ltp_rawsock2.log`,
  `/tmp/seaos_la_ltp_sendmsg1.log`,
  `/tmp/seaos_la_ltp_rawdeliver1.log`,
  `/tmp/seaos_la_ltp_rawdeliver2.log`,
  `/tmp/seaos_la_ltp_ancillary1.log`,
  `/tmp/seaos_la_ltp_mktemp1.log`,
  `/tmp/seaos_la_ltp_kconfig1.log`
- Current-round iozone focused QEMU logs:
  `/tmp/seaos_la_iozone_mem_budget3.log` supersedes the old
  `/tmp/seaos_la_iozone_focus1.log` and `/tmp/seaos_la_iozone_focus2.log`.
  The new run reaches `#### OS COMP TEST GROUP END iozone-musl ####` and
  `shutdown: system halting`; hard failure scanning for `FAIL`, `[SEGV]`,
  `trap:`, `Fork failed`, `memfs: out of memory`, nonzero child `exit: pid`,
  `Broken pipe`, etc. is empty. `iozone-musl` is now clean.
- Current-round LTP focused QEMU log:
  `/tmp/seaos_la_ltp_access04_ro_mount.log`; `access04` errno assertions for
  `EINVAL`, `ENOENT`, `ENAMETOOLONG`, `ENOTDIR`, `ELOOP`, and `EROFS` are TPASS
  for root/nobody after LoongArch `faccessat` and readonly mount tracking, but
  `ltp-musl` remains not clean due later cases and wrapper/result accounting.
- Latest LTP raw/asapi focused QEMU logs:
  `/tmp/seaos_la_ltp_rawsock1.log` clears the early IPv6 RAW
  `socket(10, 3, 58/159)=EINVAL` layer but still exposes checksum, ICMPv6
  filter, and pktinfo/sendmsg failures. `/tmp/seaos_la_ltp_rawsock2.log`
  retains `IPV6_CHECKSUM` setsockopt/sendto error semantics, making
  `asapi_01` offset 19/20/66 cases TPASS. `/tmp/seaos_la_ltp_sendmsg1.log`
  then clears the old `sendmsg ENOSYS` layer and makes the
  `IPV6_RECVPKTINFO` set-get assertion TPASS. `/tmp/seaos_la_ltp_rawdeliver2.log`
  adds minimal raw loopback delivery and ICMP6_FILTER handling: `asapi_02`
  reports 12 passed / 0 failed and `asapi_03` `IPV6_RECVPKTINFO` set-get and
  receive are TPASS. `/tmp/seaos_la_ltp_ancillary1.log` further makes all 18
  `asapi_03` assertions TPASS, including `IPV6_RECVHOPLIMIT/RTHDR/HOPOPTS/
  DSTOPTS/TCLASS` and 2292 options. The latest run was manually stopped at
  later LTP cases, has no GROUP END, and still contains real LTP failures, so
  `ltp-musl` is not clean.
- Latest LTP env/kconfig focused QEMU logs:
  `/tmp/seaos_la_ltp_mktemp1.log` shows the runtime `/bin/mktemp` busybox
  wrapper works: `ar01.sh` no longer reports `sh: can't execute 'mktemp':
  No such file or directory`. It advances to a new real blocker,
  `sh: out of range` and `ar01 1 TBROK: timeout need to be >= 1 ()`.
  `/tmp/seaos_la_ltp_kconfig1.log` shows `KCONFIG_PATH=/etc/seaos-kconfig`
  is parsed. `acct02` changes from `Cannot parse kernel .config` to TCONF
  because `CONFIG_BSD_PROCESS_ACCT=n`; `aslr01` changes from the same parse
  TBROK to TCONF because `CONFIG_HAVE_ARCH_MMAP_RND_BITS=n`. The run still
  stops at `assign_password.sh` and kept real `ltp-musl` blockers at that
  point: wrapper `FAIL LTP CASE`, `ar01.sh` TBROK, `arping01.sh`, `hopopt`
  TFAIL, `ask_password.sh`/`assign_password.sh`, and later ABI/environment
  gaps. The `ar01.sh` and `arping01.sh` layers are now superseded by
  `/tmp/seaos_la_ar01_min_ar10.log` and `/tmp/seaos_la_ltp_after_ar10.log`.
- Latest LTP timeout focused QEMU logs:
  `/tmp/seaos_la_ltp_timeout_disabled1.log` and clean-image rerun
  `/tmp/seaos_la_ltp_no_ar_cleanimg1.log` show `TST_TIMEOUT=-1` moves
  `ar01.sh` past the busybox ash/LTP shell harness
  `timeout need to be >= 1 ()` layer. The next real blocker is missing
  busybox `ar` applet: `ar: applet not found`, followed by
  `ar01 1 TBROK: ar -cr ... failed`. Source no longer creates a `/bin/ar`
  busybox wrapper, but busybox shell still resolves `ar` as an applet name and
  reports the missing applet at execution time. `ltp-musl` is still not clean.
- Latest LTP arping/fd focused QEMU logs:
  `/tmp/seaos_la_arping01_clean1.log` shows focused `arping01.sh` now TPASS
  with Summary `passed 1 / failed 0 / broken 0` and an empty broad failure
  scan after fixing LoongArch socket fd duplication and minimal AF_PACKET
  `sockaddr_ll`/ARP reply behavior. `/tmp/seaos_la_ltp_after_arping_fix1.log`
  confirms the same `arping01.sh` TPASS in focused LTP order.
- Latest LTP ar focused QEMU logs:
  `/tmp/seaos_la_ar01_min_ar10.log` shows focused `ar01.sh` Summary
  `passed 20 / failed 0 / broken 0 / skipped 0 / warnings 0`.
  `/tmp/seaos_la_ltp_after_ar10.log` confirms `ar01.sh` 20/20 TPASS in
  focused LTP order and `arping01.sh` remains TPASS. `ltp-musl` remains not
  clean: `asapi_01` still has the known `hopopt` libc-table TFAIL,
  `ask_password.sh` returns 1, and `assign_password.sh` remains an
  interactive bash/password/keyctl blocker.
- Restored full `/musl` smoke after the env/kconfig helper changes:
  `/tmp/seaos_la_musl_after_ltp_kconfig_smoke.log`; it reaches
  `libcbench-musl` and `libctest-musl` GROUP END, enters `unixbench-musl`,
  exits by timeout 124 at 180 seconds, and has an empty broad failure scan.
  It is only a restored-entry smoke, not full `/musl` pass evidence.
- Restored full `/musl` smoke after the ar focused fix:
  `/tmp/seaos_la_musl_after_ar_smoke.log`; it reaches
  `libcbench-musl` and `libctest-musl` GROUP END, enters `unixbench-musl`,
  prints UnixBench DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/SPAWN/EXECL results,
  exits by timeout 124 at 360 seconds, and has an empty broad failure scan.
  It is only a restored-entry smoke, not full `/musl` pass evidence.
- Latest LTP bind focused logs:
  `/tmp/seaos_la_bind02_fix1.log` shows direct `bind02` TPASS for non-root
  privileged-port `EACCES`; `/tmp/seaos_la_bind03_fix1.log` shows direct
  `bind03` TPASS for AF_UNIX rebind `EINVAL` and occupied pathname
  `EADDRINUSE`; `/tmp/seaos_la_bind04_fix3.log` shows direct `bind04`
  Summary `passed 16 / failed 0 / broken 0`; `/tmp/seaos_la_bind05_fix1.log`
  shows direct `bind05` Summary `passed 14 / failed 0 / broken 0`. Each direct
  log reaches shutdown and has an empty hard failure scan.
- Latest focused LTP order after bind fixes:
  `/tmp/seaos_la_ltp_after_bind05_fix1.log` confirms `bind01` through
  `bind05` all have real TPASS assertions and `FAIL LTP CASE ... : 0` in
  LTP order. The same run keeps `ltp-musl` not clean: `asapi_01` still has the
  known `hopopt` libc-table TFAIL, `bind06` TCONF due missing
  `CONFIG_USER_NS`/`CONFIG_NET_NS`, and later proc/cgroup cases expose
  `/proc/sys/kernel/pid_max` and `/proc/self/mounts` TBROK. The run was
  manually stopped after collecting this evidence and is not GROUP END/pass
  evidence.
- Restored full `/musl` smoke after the bind focused fixes:
  `/tmp/seaos_la_musl_after_bind_smoke1.log`; it reaches
  `libcbench-musl` and `libctest-musl` GROUP END, enters `unixbench-musl`,
  prints DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/SPAWN/EXECL, exits by timeout
  124 at 360 seconds, and has an empty hard failure scan. It is only a
  restored-entry smoke, not full `/musl` pass evidence.
- Latest LTP capability/proc/cgroup focused logs:
  `/tmp/seaos_la_capability_fix4.log` shows direct focused `capget01`,
  `capget02`, and `capset01`-`capset04` all have Summary `failed 0` and
  `broken 0`, with an empty broad failure scan. `/tmp/seaos_la_ltp_after_capability_fix1.log`
  confirms the same cases TPASS/ret 0 in focused LTP order and shows the old
  `/proc/sys/kernel/pid_max` and `/proc/self/mounts` TBROK layers are gone.
  `/tmp/seaos_la_ltp_after_rmdir_fix1.log` shows `rmdir not found` is gone;
  `/tmp/seaos_la_ltp_after_killall_fix1.log` shows `killall not found` is
  gone. The next cgroup blockers are controller/helper semantics:
  memory/base controller TCONF, `cgroup_require: controller not defined`,
  `Number of subgroups must be possitive integer`, and helper scripts directly
  executed with `must call tst_run`. `ltp-musl` remains not clean.
- Restored full `/musl` smoke after capability/proc/cgroup fixes:
  `/tmp/seaos_la_musl_after_capability_smoke1.log`; it reaches
  `libcbench-musl` and `libctest-musl` GROUP END, enters `unixbench-musl`,
  prints DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/SPAWN/EXECL, exits by timeout
  124 at 360 seconds, and has an empty hard failure scan. It is only a
  restored-entry smoke, not full `/musl` pass evidence.
- Latest LTP optional ENOSYS/cgroup-helper focused logs:
  `/tmp/seaos_la_ltp_enosys_only1.log` shows the old `accept03`-adjacent
  optional fd-creation `UNKNOWN #...` lines are gone after registering
  explicit `-ENOSYS` stubs for eventfd/epoll/signalfd/timerfd/pidfd/fanotify/
  inotify/userfaultfd/perf/io_uring/bpf/fsopen/fspick/open_tree/memfd probes.
  The same log confirms `ar01.sh` remains 20/20 TPASS, `arping01.sh` remains
  TPASS, and `broken_ip-*` advances to TPASS/TCONF. It still exposes real
  `asapi_01` `hopopt` TFAIL and cgroup helper TBROK:
  `cgroup_fj_common.sh`/`cgroup_lib.sh` `must call tst_run`,
  `cgroup_fj_function.sh` `controller not defined`,
  `cgroup_fj_stress.sh` `Number of subgroups must be possitive integer`, then
  `cgroup_regression_3_1.sh` helper. `/tmp/seaos_la_ltp_enosys_timeout30_1.log`
  is a rejected trial: `TST_TIMEOUT=30` regresses `ar01.sh`, `arping01.sh`,
  and `broken_ip-*`, so current initcode keeps `TST_TIMEOUT=-1`.
- Restored full `/musl` smoke after optional ENOSYS stubs:
  `/tmp/seaos_la_musl_after_enosys_stub_smoke1.log`; it reaches
  `libcbench-musl` and `libctest-musl` GROUP END, enters `unixbench-musl`,
  prints DHRY2/WHETSTONE/SYSCALL/CONTEXT/PIPE/SPAWN/EXECL, exits by timeout
  124 at 360 seconds, and has an empty hard failure scan. It is only a
  restored-entry smoke, not full `/musl` pass evidence.
- Current-round lmbench highmem/dedup logs:
  `/tmp/seaos_la_lmbench_highmem_memfs.log` and
  `/tmp/seaos_la_lmbench_highmem_dedup.log`; these experiments were reverted.
  They advanced default `lat_fs` past earlier points but still produced real
  `memfs: out of memory`, one highmem-boundary trap in the first experiment,
  and the old `Hello` shell corruption. That `Hello` blocker was later
  superseded by the memfs fd writable fix; the later `lat_fs /var/tmp` and
  `lat_ctx 96` blockers are now superseded by sparse-zero memfs holes and
  read-only ELF segment fork-share. See the newer lmbench evidence below.
- Current focused remainder logs:
  - `/tmp/seaos_la_musl_restore_after_iozone.log`: latest restored full
    `/musl` entry smoke after the iozone memory-budget fix. It reaches
    `libcbench-musl` and `libctest-musl` GROUP END, enters `unixbench-musl`,
    exits by timeout 124 at 180 seconds, and has an empty broad failure scan
    up to that cutoff. It is not full `/musl` pass evidence.
  - `/tmp/seaos_la_musl_restore_smoke.log`: older restored-entry smoke after
    reverting failed COW/highmem experiments; superseded by the 180-second
    smoke above.
  - `/tmp/seaos_la_iozone_cow2.log`,
    `/tmp/seaos_la_iozone_cow_mprotect1.log`, and
    `/tmp/seaos_la_iozone_cow_tlbinval1.log`: three COW/fork-pressure attempts
    for `iozone-musl`; all introduce or retain real `pc=0`/ADEF user faults
    and were reverted. Do not resume from these half-fixes.
  - `/tmp/seaos_la_cyclictest_ticktime_clean1.log`: current clean evidence for
    `cyclictest-musl`; `NO_STRESS_P1/P8` and `STRESS_P1/P8` all end success,
    `kill hackbench` succeeds, GROUP END/shutdown is reached, and broad
    failure scan is empty.
  - `/tmp/seaos_la_cyclictest_red1.log`: older superseded blocker evidence;
    `cyclictest-musl` passed `NO_STRESS_P1/P8`, then hackbench hit
    `fork() (error: Out of memory)` and cascaded into
    `No measurements available`, `Broken pipe`, and worker OOM.
  - `/tmp/seaos_la_cyclictest_highmem1.log`,
    `/tmp/seaos_la_cyclictest_highmem2.log`, and
    `/tmp/seaos_la_cyclictest_usermem1.log`: high/extended user-page pool
    experiments for cyclictest; all ADEF during startup or early exec and
    were reverted. The remaining safe changes are PA/KVA hygiene fixes only.
  - `/tmp/seaos_la_ltp_red1.log`: focused LTP progresses through many
    `access*`/`adjtimex*` TPASS/TCONF cases, but the wrapper still emits
    `FAIL LTP CASE ... : 0/32`; under the current pass rules this is not clean
    and official scripts must not be modified.
  - Read-only image inspection
    `debugfs -R "cat /musl/ltp_testcode.sh" sdcard-la.img` proves the LTP
    wrapper unconditionally prints `FAIL LTP CASE $(basename "$file") : $ret`
    after every case, even when `$ret` is 0. Under the current "no FAIL in
    real evidence" rule, `ltp-musl` cannot become a clean big group without
    changing/suppressing official-script output, which is forbidden. Continue
    LTP only for reducing true nonzero/TFAIL/TBROK blockers, not as an
    immediately countable clean group.
  - `/tmp/seaos_la_lmbench_latfs_sparse1.log`: direct
    `lmbench_all lat_fs /var/tmp` now prints 0k/1k/4k/10k results and reaches
    wrapper success without `memfs: out of memory`, after sparse-zero memfs
    hole handling.
  - `/tmp/seaos_la_lmbench_latctx96_forkoom_diag.log`: old final
    `lat_ctx 96` blocker was `fork: copy_pgtbl OOM pid=4`.
  - `/tmp/seaos_la_lmbench_latctx96_execshare1.log`: direct
    `lmbench_all lat_ctx -P 1 -s 32 96` prints `96 59.84` and reaches wrapper
    success after exec read-only segment fork-share.
  - `/tmp/seaos_la_lmbench_execshare2.log`: official focused
    `lmbench_testcode.sh` reaches `#### OS COMP TEST GROUP END lmbench-musl ####`;
    `lat_ctx` prints 2/4/8/16/24/32/64/96 and the broad failure scan is empty.
  - `/tmp/seaos_la_cyclictest_memfs_inode_cap.log`: older superseded
    `cyclictest-musl` evidence. It had
    `NO_STRESS_P1/P8` and `STRESS_P1` success after BSS memory-budget work,
    but still has many `No measurements available`, `Connection reset by peer`,
    and hangs at `STRESS_P8 begin`; superseded by
    `/tmp/seaos_la_cyclictest_ticktime_clean1.log`.
  - `/tmp/seaos_la_cyclictest_mem_budget2.log`: reducing `LA_NFD` to 256
    causes earlier hackbench `Broken pipe`; that fd-cap attempt was reverted.
  - `/tmp/seaos_la_cyclictest_focused_after_iozone.log`: fresh RED after the
    iozone fix fails at hackbench `Creating fdpair (error: Too many open files
    in system)`, then cascades into `CLIENT: ready write (error: Broken pipe)`.
  - `/tmp/seaos_la_cyclictest_npipe1024.log` and
    `/tmp/seaos_la_cyclictest_dynpipe8192.log`: pipe-capacity attempts do not
    remove the fdpair ENFILE because default hackbench uses
    `socketpair(AF_UNIX, SOCK_STREAM)`.
  - `/tmp/seaos_la_cyclictest_dynsock512.log`: `LA_NSOCK=512` with dynamic
    stream socket buffers removes the fdpair ENFILE layer, but the group is
    still not clean: hackbench repeatedly prints `No measurements available`,
    then `Reading for readyfds (error: Connection reset by peer)`, and the run
    was stopped after `STRESS_P1` success while stuck in `STRESS_P8 begin`.
  - `/tmp/seaos_la_cyclictest_sockrecvdbg.log`: temporary official-QEMU trace
    shows the readyfd parent read failed because socket idx 0 had
    `refs=0x191`, `state=ESTABLISHED`, but `type=0`; root cause was
    `la_sock_connect_pair()` not setting AF_UNIX `socketpair` endpoints to
    `LA_SOCK_STREAM`.
  - `/tmp/seaos_la_cyclictest_socketpair_type.log`: after setting both
    `socketpair` endpoints to stream type, the old fdpair/readyfds failures
    disappear and `STRESS_P1 end: success` is reached. The run still times out
    at 360s after `STRESS_P8 begin`, without GROUP END; this is now historical
    evidence superseded by the tick-timebase fix.
  - `/tmp/seaos_la_cyclictest_slice1.log`: rejected `LA_TIME_SLICE=1`
    scheduler trial; it faults during `NO_STRESS_P1` and was reverted.
  - `/tmp/seaos_la_cyclictest_sched_diag1.log`: temporary scheduler counters
    show RT cyclictest workers sleeping while about 200 normal-priority
    hackbench tasks remain runnable; current blocker is runqueue pressure/
    main-thread progress in `STRESS_P8`, not the old readyfd/socketpair layer.
  - `/tmp/seaos_la_cyclictest_pressure_slice2.log`: high-pressure normal
    2-tick slice trial still times out in `STRESS_P8 begin`; reverted.
  - `/tmp/seaos_la_cyclictest_pressure_slice1.log`: high-pressure 1-tick
    trial triggers repeated `trap: ecode=0xd` and initcode fault; reverted.
  - `/tmp/seaos_la_cyclictest_sockbuf512.log`: 512-byte stream receive window
    trial still times out in `STRESS_P8 begin`; reverted.
  - `/tmp/seaos_la_iozone_mem_budget3.log`: `iozone-musl` reaches GROUP END,
    shutdown, and has an empty hard failure scan; clean.
  - `/tmp/seaos_la_iozone_highuser1.log`,
    `/tmp/seaos_la_iozone_highuser2.log`, and
    `/tmp/seaos_la_iozone_highuser3.log`: highmem user-page experiments for
    iozone fork pressure. The first two trap in `pmem_init` while touching
    high RAM/KVA; the third boots and passes iozone automatic plus initial
    writer throughput, but traps in `la_pmem_alloc_user_page()` while clearing
    a high KVA and ends with `test fail`. These patches were reverted.
  - `/tmp/seaos_la_ltp_rawsock1.log`: minimal IPv6 RAW socket creation
    support removes asapi's early `socket(10, 3, 58/159)=EINVAL` failure, but
    this log is now superseded by rawsock2/sendmsg1/rawdeliver2.
  - `/tmp/seaos_la_ltp_rawsock2.log`: `IPV6_CHECKSUM` error semantics are now
    TPASS for asapi_01 offset 19/20/66, superseding the checksum layer.
  - `/tmp/seaos_la_ltp_rawdeliver2.log`: minimal raw loopback delivery and
    ICMP6_FILTER handling make `asapi_02` report 12 passed / 0 failed, and
    `asapi_03` `IPV6_RECVPKTINFO` set-get and receive are TPASS. The remaining
    `hopopt` TFAIL is a libc boundary: LTP checks
    `getprotobyname("hopopt")->p_proto == 0`, while this musl uses an internal
    protocol table with protocol 0 named `ip` and no `hopopt` alias, and does
    not read initcode's `/etc/protocols`. This log is superseded for
    `asapi_03` by `/tmp/seaos_la_ltp_ancillary1.log`.
  - `/tmp/seaos_la_ltp_ancillary1.log`: all 18 `asapi_03` assertions are
    TPASS, including hoplimit/tclass receive and 2292 option set-get/receive.
    Current remaining LTP work is kernel config/shell-helper coverage,
    `hopopt`, `ask_password.sh`/`assign_password.sh`, and later ABI/environment
    gaps, not the already-cleared early RAW socket, checksum, sendmsg ENOSYS,
    ICMP6_FILTER/raw receive, `IPV6_RECVPKTINFO`, or IPv6 ancillary receive
    layers.
  - `/tmp/seaos_la_iozone_cow1.log`: a minimal COW fork experiment was
    rejected and reverted because it introduced early TLB refill/user faults.
  - `/tmp/seaos_la_cyclic_focus1.log`: `NO_STRESS_P1/P8` run, but
    hackbench/stress hits `fork() (error: Out of memory)`,
    `No measurements available`, `Broken pipe`, and worker OOM.
  - `/tmp/seaos_la_cyclic_cow1.log` and
    `/tmp/seaos_la_cyclic_cow2.log`: minimal COW attempts were rejected and
    reverted because they introduced early page/TLB faults.
  - `/tmp/seaos_la_iozone_focus1.log` and `/tmp/seaos_la_iozone_focus2.log`:
    reach GROUP END but have repeated real `Fork failed`; not clean.
  - `/tmp/seaos_la_ltp_focused1.log`: still shows broad LTP wrapper failures,
    unknown syscalls, shell-helper gaps, `hopopt` and IPv6 RAW/asapi blockers.
  - `/tmp/seaos_la_lmbench_fix2.log`: official focused lmbench no longer hits
    the old `Hello` shell corruption, but is superseded by the newer
    `/tmp/seaos_la_lmbench_focus3.log`.
  - `/tmp/seaos_la_lmbench_latsig_catch_300.log`,
    `/tmp/seaos_la_lmbench_latsig_prot2.log`,
    `/tmp/seaos_la_lmbench_lat_pipe1.log`, and
    `/tmp/seaos_la_lmbench_shell_prot_pipe1.log`: direct/shell focused
    checks show `lat_sig catch/prot` and `lat_pipe` are not the current
    blocker; outer timeout follows `shutdown: system halting`.
  - `/tmp/seaos_la_lmbench_focus3.log`: reaches `Pipe latency`,
    `Process fork+exit`, `Process fork+execve`, `Process fork+/bin/sh -c`,
    `File /var/tmp/XXX write bandwidth`, then default `lat_fs /var/tmp`
    emits repeated real `memfs: out of memory`; not clean.
- Latest restored full `/musl` smoke after vfork exec/mm fixes:
  `/tmp/seaos_la_musl_full_after_vfork_mm.log`; it uses full
  `run_test_entries("/musl")`, reaches `libcbench-musl` and
  `libctest-musl` GROUP END, enters `unixbench-musl`, prints through
  `Unixbench EXECL`, exits by timeout 124, and has an empty broad failure
  scan up to that cutoff. It is not full `/musl` pass evidence.
- Latest restored full `/musl` smoke:
  `/tmp/seaos_la_musl_full_after_ltp_accept.log`; it uses full
  `run_test_entries("/musl")`, times out at 360 s in `unixbench-musl`
  after `FS_WRITE_SMALL`, and has an empty broad failure scan up to that
  cutoff. It does not reach `unixbench-musl` GROUP END.
- Latest restored full `/musl` smoke after symlink work:
  `/tmp/seaos_la_musl_full_after_symlink.log`; it also uses full
  `run_test_entries("/musl")`, reaches `libcbench-musl` and
  `libctest-musl` GROUP END, enters `unixbench-musl`, prints through
  `FS_WRITE_SMALL`, exits by timeout 124, and has an empty broad failure scan
  up to that cutoff.
- Current-round restored full `/musl` smoke:
  `/tmp/seaos_la_musl_after_current_revert.log`; it uses full
  `run_test_entries("/musl")`, reaches `libcbench-musl` and
  `libctest-musl` GROUP END, enters `unixbench-musl`, prints through
  `FS_WRITE_SMALL`, exits by timeout 124, and has an empty broad failure scan
  up to that cutoff. It is not full `/musl` pass evidence.
- Full `/musl` restored-entry smoke command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 360 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_musl_after_basic_fix.log 2>&1'`
- UnixBench single-group QEMU command:
  `docker exec seaos-la bash -lc 'cd /workspace && timeout 3600 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_unixbench_final2.log 2>&1'; echo exit=$?`
- Libctest regression-recheck QEMU log:
  `/tmp/seaos_la_libctest_utime_fix.log`
- Cyclictest focused blocker log:
  `/tmp/seaos_la_cyclictest_sleep_wrapper.log`
- Log markers:
  - `#### OS COMP TEST GROUP END libcbench-musl ####`
  - `#### OS COMP TEST GROUP END libctest-musl ####`
  - `#### OS COMP TEST GROUP END unixbench-musl ####`
  - `#### OS COMP TEST GROUP END busybox-musl ####`
  - `#### OS COMP TEST GROUP END netperf-musl ####`
  - `Unixbench SHELL1 test(lpm): 33`
  - `Unixbench SHELL8 test(lpm): 14`
  - `Unixbench SHELL16 test(lpm): 8`
  - `Unixbench EXEC test(lps): 879`
  - `====== netperf UDP_STREAM end: success ======`
  - `====== netperf TCP_STREAM end: success ======`
  - `====== netperf UDP_RR end: success ======`
  - `====== netperf TCP_RR end: success ======`
  - `====== netperf TCP_CRR end: success ======`
  - `====== iperf BASIC_UDP end: success ======`
  - `====== iperf BASIC_TCP end: success ======`
  - `====== iperf PARALLEL_UDP end: success ======`
  - `====== iperf PARALLEL_TCP end: success ======`
  - `====== iperf REVERSE_UDP end: success ======`
  - `====== iperf REVERSE_TCP end: success ======`
  - `testcase lua date.lua success`
  - `testcase lua file_io.lua success`
  - `testcase lua max_min.lua success`
  - `testcase lua random.lua success`
  - `testcase lua remove.lua success`
  - `testcase lua round_num.lua success`
  - `testcase lua sin30.lua success`
  - `testcase lua sort.lua success`
  - `testcase lua strings.lua success`
  - `#### OS COMP TEST GROUP END lua-musl ####`
  - `#### OS COMP TEST GROUP END basic-musl ####`
- Broad failure scan on `/tmp/seaos_la_unixbench_final2.log` is empty with the
  extended pattern that includes `UNKNOWN`, `No such file`, and
  `argument expected`.
- Group-sliced broad failure scans over `libcbench-musl`, `busybox-musl`,
  `unixbench-musl`, and `netperf-musl` in
  `/tmp/seaos_la_musl_after_netperf_fix.log` are empty.
- Focused broad failure scan over `/tmp/seaos_la_iperf_focus.log` is empty.
- Focused broad failure scan over `/tmp/seaos_la_libctest_utime_fix.log` is
  empty; that log rechecks the earlier `utime` regression and reaches
  `#### OS COMP TEST GROUP END libctest-musl ####`.
- Focused broad failure scan over `/tmp/seaos_la_lua_focus.log` is empty.
- Focused broad failure scan over `/tmp/seaos_la_basic_clone_fix.log` is empty.
- `/tmp/seaos_la_musl_after_basic_fix.log` uses the restored full
  `run_test_entries("/musl")` entry. Its 360-second window times out during
  `unixbench-musl` `FS_WRITE_SMALL`; the broad failure scan up to that point
  is empty. It is a restored-entry smoke log, not full `/musl` pass evidence.
- `iozone-musl` is clean in `/tmp/seaos_la_iozone_mem_budget3.log`.
  Older `/tmp/seaos_la_iozone_focus1.log` and
  `/tmp/seaos_la_iozone_focus2.log` fork-failure evidence is superseded.
- `ltp-musl` is not clean. Focused logs show concrete progress through three
  setup layers:
  - `/tmp/seaos_la_ltp_focus.log`: common setup blocked on
    `fchmodat(53)` / `chmod(...)=ENOSYS`.
  - `/tmp/seaos_la_ltp_fchmodat.log`: `fchmodat` ENOSYS gone; next blockers
    were `fchownat(54)` and `setpgid(154)`.
  - `/tmp/seaos_la_ltp_fchown_setpgid.log`: those ENOSYS gaps gone; LTP
    reached real case failures and environment gaps.
  - `/tmp/seaos_la_ltp_env_stubs.log`: `/etc/passwd`, `/etc/group`, and
    `/proc/self/maps` stubs removed `getpwnam(nobody)` and proc maps ENOENT.
  - `/tmp/seaos_la_ltp_identity_access.log`: `setuid/setresuid` ENOSYS gone,
    `access01` mostly reports TPASS, but older real failures still included
    `abort01`, `accept01/02/03`, `symlinkat(36)`, `acct(89)`,
    `adjtimex(171)`, AF_ALG socket paths, and harness result reporting.
  - `/tmp/seaos_la_ltp_accept03_fix.log`: `abort01`, `accept01`,
    `accept02`, `accept03`, and `accept4_01` real assertions now pass.
  - `/tmp/seaos_la_ltp_symlink_red.log`: `access02` and `access04` still
    broke at `symlink()` setup with `UNKNOWN #0x24` / `ENOSYS`.
  - `/tmp/seaos_la_ltp_symlink_green.log`: after minimal memfs symlink,
    `symlinkat(36)`, `readlinkat(78)`, and final-component symlink-follow,
    `UNKNOWN #0x24` disappeared and `access02` reached 6 TPASS lines for
    `file_f/file_r/file_w`.
  - `/tmp/seaos_la_ltp_access02_clean.log`: after memfs exec-source support
    plus vfork exec/mm/ASID fixes, `access02` reaches TPASS for
    `file_x` and `symlink_x` X_OK execution as both root and nobody; no
    `trap:` or temporary clone/exec trace remains in that window.
    `ltp-musl` remains not clean: the same focused run still shows
    `access01` `TBROK: Test 12 haven't reported results!`, `access04`
    errno mismatches (`ENAMETOOLONG`, `ENOTDIR`, read-only behavior),
    `adjtimex(171)` ENOSYS, AF_ALG socket gaps, IPv6 socket case failures,
    and broader LTP environment/ABI gaps.
- `cyclictest-musl` is clean in
  `/tmp/seaos_la_cyclictest_ticktime_clean1.log`. The focused official-QEMU
  run reaches GROUP END/shutdown; `NO_STRESS_P1/P8`, `STRESS_P1/P8`, and
  `kill hackbench` all report success; broad failure scan is empty. The old
  fork/OOM, highmem, socketpair, runqueue-pressure, and STRESS_P8 timeout
  logs are historical superseded evidence.
- `lmbench-musl` is clean in `/tmp/seaos_la_lmbench_execshare2.log`.
  Older `lat_fs`/`lat_ctx 96` blocker logs are retained as history only and
  are superseded by the current GROUP END evidence.
- Focused official-QEMU logs used during UnixBench repair:
  `/tmp/seaos_la_multi_sh_focus_sigsuspend.log` and
  `/tmp/seaos_la_multi_sh_focus_sortsrc.log`.

Current source state:

- `src/user/initcode_la.c` has been restored to full `/musl` scanning:
  `run_test_entries("/musl")`.
- `os_serial_out_la.txt` is an old log and is not current evidence for the
  latest LoongArch state.
- Restored full-entry smoke `/tmp/seaos_la_musl_after_bind_smoke1.log`
  reaches `libcbench-musl` and `libctest-musl` GROUP END, enters
  `unixbench-musl`, and has an empty hard failure scan before its 360-second
  timeout. It is not full `/musl` pass evidence.
- Branch note: current local `HEAD` is
  `47c4bd880d75734aa0f663630420bf406bc59c03`, while the local
  `origin/os2026-1` ref is `db829a7238c8c3eb5b8a7bae00c43a03057c2046`
  (`os2026-1` is behind by 8 commits). Do not claim branch
  alignment until the user decides how to reconcile the dirty LoongArch
  worktree with the newer origin.

Remaining current-priority work:

- Continue from `ltp-musl` only. Current evidence is
  `/tmp/seaos_la_ltp_enosys_only1.log`: optional fd-creation `UNKNOWN #...`
  probes are now explicit `-ENOSYS`, while `bind01` through `bind05`,
  password helpers, direct `brk01`, ar01/arping, capget/capset, pid_max,
  self-mounts, rmdir, and killall remain cleared. The remaining known early
  blocker is `asapi_01` `hopopt` libc-table boundary; later visible gaps are
  `bind06` namespace config TCONF plus cgroup controller/helper issues.
  Do not
  repeat the already-fixed `ar01.sh` missing-`ar` layer,
  `arping01.sh` AF_PACKET/getsockname/EBADF layer,
  early RAW socket `EINVAL`, `IPV6_CHECKSUM` offset
  semantics, ICMP6_FILTER/raw receive timeout, `IPV6_RECVPKTINFO` set-get or
  receive, `sendmsg ENOSYS`, IPv6 ancillary receive options,
  kernel config lookup, `mktemp` helper lookup, optional fd-creation
  `UNKNOWN`, password/keyctl helper
  interaction, direct `brk01`, `bind01`-`bind05`,
  `access01/access02/access03/access04`, `adjtimex`, AF_ALG,
  `fchmodat/fchownat/setpgid/setuid`, `abort01`, or
  `accept01/02/03` blockers. The `hopopt` protocol-0 lookup is a current musl
  internal protocol-table boundary, not a kernel-stub fix target.
- Do not restart `lmbench-musl` from old `lat_fs /var/tmp`, `Hello` shell
  corruption, `lat_sig catch/prot`, `lat_pipe`, or `lat_ctx 96` blockers unless
  a fresh full scan proves regression; `/tmp/seaos_la_lmbench_execshare2.log`
  is the current clean evidence.
- Do not restart `cyclictest-musl` from old hackbench/STRESS_P8 evidence unless
  a fresh full scan proves regression; `/tmp/seaos_la_cyclictest_ticktime_clean1.log`
  is the current clean evidence.
- Mark each musl group clean only after broad failure scanning and
  group-specific output checks.
- After all 12 musl groups are clean, decide whether to begin glibc or update
  contest strategy.

## Suggested Next Step

P1: `ltp-musl` is the only remaining `/musl` group. Current focused evidence:
`/tmp/seaos_la_ltp_enosys_only1.log` confirms optional fd-creation
`UNKNOWN #...` probes are gone, `ar01.sh` remains 20/20 TPASS,
`arping01.sh` remains TPASS, `broken_ip-*` advances, and earlier
bind/password/brk/capability evidence remains valid. Start the next LTP pass
from the still-open `asapi_01` `hopopt` libc-table boundary, `bind06`
namespace-config TCONF, and cgroup controller/helper blockers:
`cgroup_core01/02` memory controller TCONF, `cgroup_core03` V2 base controller
TCONF, `cgroup_fj_common.sh`/`cgroup_lib.sh` helper `must call tst_run` TBROK,
`cgroup_fj_function.sh` `cgroup_require: controller not defined`, and
`cgroup_fj_stress.sh` `Number of subgroups must be possitive integer`;
the latest run then reaches `cgroup_regression_3_1.sh` helper.
`ar01.sh` is now 20/20 TPASS in `/tmp/seaos_la_ar01_min_ar10.log` and
focused LTP order; `arping01.sh` is TPASS in
`/tmp/seaos_la_arping01_clean1.log` and focused LTP order.
`cyclictest-musl` is already clean in
`/tmp/seaos_la_cyclictest_ticktime_clean1.log`; do not restart from old
hackbench/STRESS_P8 timeout logs unless a fresh scan proves regression.

Known facts:

- `unixbench-musl` is clean in `/tmp/seaos_la_unixbench_final2.log`.
- `busybox-musl` and `netperf-musl` are clean in
  `/tmp/seaos_la_musl_after_netperf_fix.log`.
- `lua-musl` is clean in `/tmp/seaos_la_lua_focus.log`.
- `basic-musl` is clean in `/tmp/seaos_la_basic_clone_fix.log`.
- `lmbench-musl` is clean in `/tmp/seaos_la_lmbench_execshare2.log`.
- `cyclictest-musl` is clean in `/tmp/seaos_la_cyclictest_ticktime_clean1.log`.
- Do not restart from pthread_cancel, EXECL, `fstime`, `[` applet lookup,
  `multi.sh` argument handling, busybox, netperf, iperf, lua, basic, lmbench,
  or cyclictest unless a fresh full scan shows a regression.
- The next round should continue LTP from `/tmp/seaos_la_ltp_after_bind05_fix1.log`,
  not from the old memfs/tmpdir shebang exec fault, earlier setup ENOSYS
  layers, raw socket creation, checksum semantics, ICMP6_FILTER delivery, or
  IPv6 ancillary receive layers, `ar01.sh`, AF_PACKET `arping01.sh`,
  kernel config lookup, `mktemp` helper lookup, password/keyctl helper
  interaction, direct `brk01`, or `bind01`-`bind05`.
- `lmbench-musl` current facts:
  - The old `lat_fs` and final `lat_ctx 96` blockers are superseded by
    `/tmp/seaos_la_lmbench_execshare2.log`.
  - Retained fix: exec maps non-writable PT_LOAD segments as RX plus
    fork-share, so fork shares text/rodata instead of deep-copying them.

Recommended commands:

```bash
docker exec seaos-la bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'
docker exec seaos-la bash -lc 'cd /workspace && timeout 5400 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_musl_next.log 2>&1'; echo exit=$?
docker exec seaos-la bash -lc 'grep -a -n "^FAIL \|\[SEGV\]\|failed:\|UNKNOWN\|unknown syscall\|trap:\|panic\|Function not implemented\|Interrupted system call\|No such file\|argument expected\|end: fail\|test fail\|test timeout\|exec fail\|fork fail\|Fork failed\|Operation not permitted\|Broken pipe" /tmp/seaos_la_musl_next.log | tail -260'
docker exec seaos-la bash -lc 'grep -a -n "#### OS COMP TEST GROUP START\|#### OS COMP TEST GROUP END\|run /musl/\|cyclictest\|netperf\|iperf\|iozone\|lua\|basic\|lmbench\|ltp" /tmp/seaos_la_musl_next.log'
```

## Updated Prompt For The Next Agent

```text
先执行 ls -la 并说明目录扫描结果，然后阅读 AGENTS.md、CLAUDE.md、
la-current.md、la-task.md、docs/LOONGARCH_MUSL_HANDOFF.md，完全遵循项目约束。

项目路径：/home/addaswsw/project/OS/oskernel2025-seaos
任务类型：一次性任务，不新增 design doc / implementation plan。
目标分支：os2026-1。先确认本地 HEAD 与 origin/os2026-1 是否对齐。
当前记录：本地 HEAD 为 47c4bd880d75734aa0f663630420bf406bc59c03，
本地 origin/os2026-1 为 db829a7238c8c3eb5b8a7bae00c43a03057c2046，
当前 dirty worktree 尚未与 newer origin 自动合并/变基；不要擅自 reset/rebase。

当前目标：只推进 LoongArch，使 /musl 的 12 个大测试组真实通过。
RISC-V 暂不处理，但公共文件改动不能导致 kernel-rv 回退。glibc 暂缓。

LoongArch 大测试点口径：
- 测试镜像共有 24 个大测试组：/musl 12 个 + /glibc 12 个。
- 本地 judge 脚本共有 22 个，缺 unixbench-musl 和 unixbench-glibc。
- 当前优先级只看 /musl 12 个；glibc 等 musl 干净后再做。
- 已完成并有最新证据的 LoongArch 大测试组：libcbench-musl、libctest-musl、unixbench-musl、busybox-musl、cyclictest-musl、netperf-musl、iperf-musl、iozone-musl、lua-musl、basic-musl、lmbench-musl。
- 当前 /musl 只剩 ltp-musl 未 clean；本地 judged groups 还剩 12 个，
  image/script groups 还剩 13 个。

硬约束：
- 禁止修改 autotest-for-oskernel/、data/sdcard-rv.img.gz、
  data/sdcard-la.img.gz、官方测试脚本和测试二进制。
- 禁止吞错、伪造输出、硬编码分数、跳过用户程序执行、抑制真实失败日志。
- 不把 “test sucess” 或 “GROUP END” 当通过证据；真实通过必须无 FAIL、
  [SEGV]、panic、trap 失败、unknown syscall、Function not implemented、
  Interrupted system call，也不能有组内 end: fail/test fail/test timeout。
- 文件保持 UTF-8。LoongArch 改动优先限制在 src/kernel/loongarch/ 和
  src/user/initcode_la.c。
- 若改公共文件，必须说明 RISC-V 风险。
- syscall 出错返回 (uint64)(-EXXX)，未知/未实现返回 -ENOSYS，不能 panic。

本地环境：
- 使用 Docker 镜像 zhouzhouyi/os-contest:20260510。
- 固定容器名 seaos-la。若不存在则创建：
docker run -dit --name seaos-la -v "/home/addaswsw/project/OS/oskernel2025-seaos:/workspace" zhouzhouyi/os-contest:20260510 bash
- 最终测试必须使用容器内官方 QEMU：
/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64
不要用宿主机 QEMU 作为验收依据。
- 若 /workspace/sdcard-la.img 不存在，用运行副本准备：
docker exec seaos-la bash -lc 'cd /workspace && gzip -dkc data/sdcard-la.img.gz > sdcard-la.img'
禁止改动原始 .img.gz。

当前状态：
- src/user/initcode_la.c 已恢复为完整 /musl 扫描：run_test_entries("/musl")。
- 最新有效证据：
  - /tmp/seaos_la_musl_after_netperf_fix.log：libcbench-musl、libctest-musl、
    unixbench-musl、busybox-musl、netperf-musl 均到达 GROUP END；这些已完成
    组的组内广义失败扫描为空。
  - /tmp/seaos_la_unixbench_final2.log：unixbench-musl 到达 GROUP END，
    输出全部 UnixBench 子项分数，扩展广义失败扫描为空。
  - /tmp/seaos_la_netperf_sigabi_accept2.log：netperf-musl focused 验证中
    UDP_STREAM/TCP_STREAM/UDP_RR/TCP_RR/TCP_CRR 全部 end: success。
  - /tmp/seaos_la_iperf_focus.log：iperf-musl focused 验证中
    BASIC_UDP/BASIC_TCP/PARALLEL_UDP/PARALLEL_TCP/REVERSE_UDP/REVERSE_TCP
    全部 end: success，扩展广义失败扫描为空。
  - /tmp/seaos_la_lua_focus.log：lua-musl focused 验证中 9 个 Lua 子脚本
    全部输出 `testcase lua ... success`，到达 GROUP END，扩展广义失败扫描为空。
  - /tmp/seaos_la_basic_clone_fix.log：basic-musl focused 验证中 clone/fork/
    waitpid 等子项到达 END，到达 GROUP END，扩展广义失败扫描为空。
  - /tmp/seaos_la_lmbench_execshare2.log：lmbench-musl focused 验证中
    `lat_ctx` 输出 2/4/8/16/24/32/64/96 全部结果，到达 GROUP END，
    扩展广义失败扫描为空。
  - /tmp/seaos_la_iozone_mem_budget3.log：iozone-musl focused 验证到达
    GROUP END 与 shutdown，硬失败扫描为空，计入 clean。旧
    /tmp/seaos_la_iozone_focus.log 的 `Fork failed` 证据已 supersede。
  - /tmp/seaos_la_cyclictest_ticktime_clean1.log：cyclictest-musl focused
    验证中 `NO_STRESS_P1/P8` 与 `STRESS_P1/P8` 全部 `end: success`，
    `kill hackbench: success`，到达 GROUP END 与 shutdown，广义失败扫描为空。
    根因是 LoongArch `clock_gettime/gettimeofday` 与
    `clock_nanosleep(TIMER_ABSTIME)` 曾使用不同 timebase；现保留修复为三者
    使用同一个 100 Hz tick clock。
  - /tmp/seaos_la_musl_after_basic_fix.log：完整 /musl 入口恢复后 360 秒
    冒烟扫描，覆盖 libcbench/libctest 并进入 unixbench 的 FS_WRITE_SMALL；
    因 timeout 退出 124，到该截点广义失败扫描为空，但不是全量 /musl 通过证据。
  - /tmp/seaos_la_libctest_utime_fix.log：libctest-musl 重新验证中
    静态/动态 utime 均 END，到达 GROUP END，扩展广义失败扫描为空。
- /tmp/seaos_la_ltp_identity_access.log：ltp-musl focused 定位中，
    `fchmodat/fchownat/setpgid/setuid/setresuid` setup ENOSYS 已消失，
    `/etc/passwd`/`/etc/group`/`/proc/self/maps` 环境缺口已补；
    `access01` 多数子项 TPASS，但当时仍有 `abort01`、`accept*`、
    `symlinkat(36)`、`acct(89)`、`adjtimex(171)`、AF_ALG socket 等真实
    缺口。
- /tmp/seaos_la_ltp_accept03_fix.log：本轮 focused 定位中，`abort01`、
    `accept01`、`accept02`、`accept03`、`accept4_01` 的真实断言均已
    TPASS；整组仍因后续 `access01` harness、`symlinkat/readlinkat`、
    `acct/adjtimex`、AF_ALG 和 fd-creation coverage 不计入 clean。
- /tmp/seaos_la_ltp_symlink_red.log：`access02`/`access04` 在
    `symlink()` setup 阶段触发 `UNKNOWN #0x24`，用户态报 `ENOSYS`。
- /tmp/seaos_la_ltp_symlink_green.log：LoongArch memfs 最小 symlink、
    `symlinkat(36)`、`readlinkat(78)` 和最终路径 symlink-follow 后，
    `UNKNOWN #0x24` 消失，`access02` 推进到 `file_f/file_r/file_w`
    6 个 TPASS。
- /tmp/seaos_la_ltp_access02_clean.log：vfork exec/mm/ASID 修复后，
    `access02` 的 `file_x`/`symlink_x` X_OK 执行路径在 root/nobody 下
    均 TPASS，且清理后的窗口内无 `trap:` 或临时 trace；LTP 仍因
    `access01` result accounting、`access04` errno/symlink-loop、
    `adjtimex`、AF_ALG、IPv6 socket 等后续缺口不 clean。
- /tmp/seaos_la_ltp_mktemp1.log：`mktemp` helper 缺失已推进；
    `ar01.sh` 不再出现 `mktemp` `No such file`，当时进入
    `sh: out of range` / `timeout need to be >= 1 ()` TBROK；该层已被后续
    `/tmp/seaos_la_ar01_min_ar10.log` supersede。
- /tmp/seaos_la_ltp_kconfig1.log：运行期 `KCONFIG_PATH=/etc/seaos-kconfig`
    生效；`acct02` 与 `aslr01` 不再 `Cannot parse kernel .config`，
    而是按 SeaOS 不支持的 Linux 配置项正常 TCONF。该日志中的
    `ar01.sh`、`arping01.sh` 缺口已被后续 ar/arping 证据 supersede；
    LTP 当前仍因 `hopopt`、password/keyctl 类用例和后续缺口不 clean。
- /tmp/seaos_la_musl_after_ltp_kconfig_smoke.log：恢复完整 `/musl` 后
    180 秒 smoke 到达 libcbench/libctest GROUP END 并进入 unixbench，
    广义失败扫描为空；不是完整 `/musl` 通过证据。
- 本轮修复点：LoongArch non-CLONE_VM `clone(220)` 现在在 parent-side
  child trap frame 上应用 `new_stack`/TLS/clear_child_tid，修复 basic clone
  子进程 `era=-8` trap；LoongArch rt_sigaction 按 kernel ABI {handler, flags, mask}
  解析，避免把 SIGALRM mask 0x2000 当 restorer；socket accept/accept4 在
  pending signal 唤醒时返回 -EINTR；此前已修复 statx busybox applet probe、
  script/shebang exec 保留 old_argv[1..]、rt_sigsuspend(133) 最小 -EINTR
  兼容、initcode 创建缺失的 UnixBench sort.src 运行期 stub。
- 旧 os_serial_out_la.txt 不是当前 LoongArch 状态证据。
- cyclictest-musl 已计入 clean。旧 `/dev/cpu_dma_latency`、hackbench fork/OOM、
  `No measurements available`、`Broken pipe`、socketpair/readyfd 和
  STRESS_P8 timeout 证据均已被
  `/tmp/seaos_la_cyclictest_ticktime_clean1.log` supersede；不要从这些旧
  blocker 重新开始，除非新鲜完整扫描证明回归。
- lmbench-musl 已在 `/tmp/seaos_la_lmbench_execshare2.log` clean；不要再从
  旧 `lat_fs`/`lat_ctx 96` blocker 开始，除非新完整扫描显示回归。
- ltp-musl 已完成 focused 定位并推进多层 setup blocker，但不计入 clean。
  若继续 LTP，从 `/tmp/seaos_la_ltp_enosys_only1.log` 后的
  `asapi_01` `hopopt` libc-table boundary、`bind06` namespace-config TCONF、
  cgroup controller/helper 缺口开始；不要再从 `ar01.sh`、`bind01`-`bind05`、
  `arping01.sh`、
  `capget/capset`、`/proc/sys/kernel/pid_max`、`/proc/self/mounts`、`rmdir`、
  `killall`、optional fd-creation `UNKNOWN`、
  asapi 早期 RAW socket
  `EINVAL`、`IPV6_CHECKSUM` offset 语义、ICMP6_FILTER/raw receive timeout、
  `sendmsg ENOSYS`、`IPV6_RECVPKTINFO` receive 或 IPv6 ancillary receive
  options 开始。`hopopt`
  protocol-0 lookup 已定位为当前 musl 内置协议表边界，不能作为内核小修目标。
- 不要再从 pthread_cancel、EXECL、fstime、`[` applet、multi.sh 参数传递、
  busybox、netperf、iperf、lua、basic 或 LTP 的
  `fchmodat/fchownat/setpgid/setuid` setup ENOSYS、`abort01`、
  `accept01/02/03`、`access01/access04`、`adjtimex`、AF_ALG 开始，
  除非新完整扫描显示这些点回归。

下一个测试点：
2026-06-29 continuation audit 结论：本轮没有新增可计数 clean 大测试组，
但清除了完整 `/musl` 顺序中新暴露的 LTP `waitpid/EINTR` regression。
当前 `/musl` 仍为 11/12 clean，剩余 `ltp-musl`。新增复核证据：

- `src/user/initcode_la.c` 已恢复完整 `run_test_entries("/musl")`，并保持
  `TST_TIMEOUT=-1`。
- `docker exec -w /workspace seaos-la bash -lc 'make build-la && cp target/loongarch/kernel-la.elf kernel-la'`
  通过。
- `/tmp/seaos_la_musl_full_clean1.log`：完整 `/musl` 官方 QEMU 顺序通过
  `libcbench-musl`、`libctest-musl`、`unixbench-musl`、`busybox-musl`、
  `cyclictest-musl`、`netperf-musl`，并越过旧 `lmbench-musl` `lat_fs 0k`
  卡点进入 `ltp-musl`；该 LTP 段出现 43 条 `waitpid.*EINTR`，不是 clean。
- `/tmp/seaos_la_ltp_restart1.log`：focused LTP-only 官方 QEMU 复验显示
  `waitpid.*EINTR` 计数为 0；旧日志中报 EINTR 的 `add_key*`、`adjtimex*`、
  `alarm*`、`bind01`-`bind05`、`brk01/02`、`capget01/02`、
  `capset01`-`capset04` 均不再出现 waitpid TBROK。
- `/tmp/seaos_la_ltp_protocol1.log`：复验 `/etc/protocols` 顺序试探无效，
  `asapi_01` `hopopt` 仍 TFAIL；该改动已撤回，继续按 musl 内置
  protocol table 边界记录。
- 只读 `debugfs -R "cat /musl/ltp/testcases/bin/cgroup_regression_3_1.sh"`
  证明该 helper 是无参数无限 `mkdir/rmdir` 循环：
  `while true; do mkdir $path/0; rmdir $path/0; done`。官方
  `/musl/ltp_testcode.sh` 又会把 `ltp/testcases/bin/*` 全部当独立 case
  直接执行；在 `TST_TIMEOUT=-1` 和禁止改脚本/跳过/伪造输出的硬约束下，
  该 helper 直接枚举问题不能被计为 clean。

新增语义改动：

- LoongArch `wait4(260)` pending signal 路径现在区分用户可见 `-EINTR` 与
  内部 `-ERESTARTSYS`。`SA_RESTART`、默认动作和忽略动作走内部 restart；
  trap 层遇到 `-ERESTARTSYS` 不写回 `a0`、不前进 `era`，先投递信号。
  handler 正常返回后重启原 wait syscall；handler `longjmp` 自然离开等待。

下一轮若继续推进，只应从仍未解决的真实边界开始：
`asapi_01` `hopopt` libc-table boundary、`bind06` namespace-config TCONF、
cgroup controller/helper blocker。不要再从 `waitpid/EINTR`、`lmbench lat_fs`、
`capget/capset`、`bind01`-`bind05`、`brk01/02`、`ar01.sh`、`arping01.sh`、
proc pid_max/self mounts、`rmdir`、`killall` 或 optional fd-creation
`UNKNOWN` 开始，除非新鲜完整扫描证明回归。

2026-06-27 continuation audit 结论：本轮没有新增可计数 clean 大测试组。
当前 `/musl` 仍为 11/12 clean，剩余 `ltp-musl`。复核证据：

- 当前 `HEAD=47c4bd880d75734aa0f663630420bf406bc59c03`，
  `origin/os2026-1=db829a7238c8c3eb5b8a7bae00c43a03057c2046`，分支 behind 8；
  dirty worktree 未做 pull/rebase/reset。
- `src/user/initcode_la.c` 保持完整 `run_test_entries("/musl")` 和
  `TST_TIMEOUT=-1`。
- 保护路径 `autotest-for-oskernel/`、`data/sdcard-rv.img.gz`、
  `data/sdcard-la.img.gz` 无 diff。
- `make build-la && cp target/loongarch/kernel-la.elf kernel-la` 在容器
  `seaos-la` 内通过。
- 只读 `debugfs -R "cat /musl/ltp_testcode.sh" sdcard-la.img` 证明官方
  LTP wrapper 无条件打印 `FAIL LTP CASE ... : $ret`，即使 `$ret=0`。
- 当前 `ltp-musl` 早期真失败/阻塞为：`asapi_01` `hopopt` musl 内置
  protocol table 边界；`bind06` namespace config TCONF；
  cgroup helper 被 wrapper 无参数直接执行导致 `must call tst_run`、
  `controller not defined`、`Number of subgroups must be possitive integer`；
  `cgroup_regression_3_1.sh` 无参数进入无限 `mkdir/rmdir` helper 循环。

在当前硬约束下，不能通过修改官方脚本、测试二进制、运行时跳过 helper、
抑制 `FAIL LTP CASE` 或伪造输出把 `ltp-musl` 计为 clean。后续若继续 LTP，
只能继续减少真实 `TFAIL/TBROK/nonzero`，并在汇报中明确它不是新增大组通过。

P1 只剩 ltp-musl。当前最新 focused 证据是：
`/tmp/seaos_la_ltp_enosys_only1.log` 确认 optional fd-creation `UNKNOWN #...`
探测已变成显式 `-ENOSYS`，`ar01.sh` 仍 20/20 TPASS，`arping01.sh` 仍 TPASS，
`broken_ip-*` 能推进到 TPASS/TCONF，且旧 `capget/capset`、
`/proc/sys/kernel/pid_max`、`/proc/self/mounts`、`rmdir`、`killall` 缺口已推进。
下一轮从仍未解决的 `asapi_01` `hopopt` libc-table boundary、`bind06`
namespace-config TCONF，以及 cgroup controller/helper blocker 开始；
`ar01.sh` 已在 `/tmp/seaos_la_ar01_min_ar10.log` 与 focused LTP 顺序中
20/20 TPASS，`arping01.sh` 已在 `/tmp/seaos_la_arping01_clean1.log` 与
focused LTP 顺序中 TPASS。`hopopt`
protocol-0 lookup 是 musl 内置协议表边界，早期 RAW socket `EINVAL` 与
`IPV6_CHECKSUM` offset 语义、ICMP6_FILTER/raw receive timeout、
`sendmsg ENOSYS`、`IPV6_RECVPKTINFO` receive 与 IPv6 ancillary receive
options 已推进；`symlinkat(36)` ENOSYS、
`access02` memfs/tmpdir shebang exec fault、`access01/access04`、
`adjtimex`、AF_ALG、kernel config lookup、`mktemp` helper lookup、
password/keyctl helper interaction、direct `brk01`、`bind01`-`bind05`、
`capget/capset`、proc pid_max/self mounts、`rmdir`、`killall`、
optional fd-creation `UNKNOWN`、cyclictest 已不是当前起点。
先不要处理 glibc。

执行策略：
1. 复核 git status、git diff、HEAD、当前 initcode、la-current.md、la-task.md。
2. 保持 src/user/initcode_la.c 为 run_test_entries("/musl")，不要恢复旧的 libctest-only。
3. 构建：
docker exec seaos-la bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'
4. 运行完整 musl：
docker exec seaos-la bash -lc 'cd /workspace && timeout 5400 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_musl_next.log 2>&1'; echo exit=$?
5. 扫描失败：
docker exec seaos-la bash -lc 'grep -a -n "^FAIL \|\[SEGV\]\|failed:\|UNKNOWN\|unknown syscall\|trap:\|panic\|Function not implemented\|Interrupted system call\|No such file\|argument expected\|end: fail\|test fail\|test timeout\|exec fail\|fork fail\|Fork failed\|Operation not permitted\|Broken pipe" /tmp/seaos_la_musl_next.log | tail -260'
6. 扫描大组覆盖：
docker exec seaos-la bash -lc 'grep -a -n "#### OS COMP TEST GROUP START\|#### OS COMP TEST GROUP END\|run /musl/\|cyclictest\|netperf\|iperf\|iozone\|lua\|basic\|lmbench\|ltp" /tmp/seaos_la_musl_next.log'
7. 当前 `/musl` 只剩 `ltp-musl`，已不存在可连续完成 2-3 个 `/musl`
   大测试组的空间；若继续，只能按日志顺序减少 `ltp-musl` 的真实
   `TFAIL/TBROK/nonzero`。每个定位点：定位真实失败 -> 最小修复 ->
   make build-la -> 官方 QEMU 验证。若阻塞来自官方 wrapper/helper 或 libc
   表边界，记录证据，不得用跳过/改脚本/抑制输出来计 clean。
8. 一个大测试组只有在真实失败扫描和组内语义检查都干净后，才更新 la-task.md 和 la-current.md。

最终汇报：
修改文件、测试命令、日志结论、完成/剩余的大测试组、是否新增依赖、
是否改公共文件及 RISC-V 风险、确认未修改 autotest-for-oskernel/ 和 data/*.img.gz。
```

## How To Update This Prompt After Each Completed Task

After each newly completed big test group or major task, update these parts:

1. `Current Progress`
   - Move the group from "Needs fresh verification" to "Clean".
   - Record the exact log path and the broad failure scan result.

2. `Full Group List`
   - Update the status cell for the completed group.
   - If a group has no local judge script, explicitly say how it was checked.

3. `Updated Prompt For The Next Agent`
   - Update `已完成并有最新证据的 LoongArch 大测试组`.
   - Update `当前状态`.
   - Replace `下一个测试点` with the next real failing or unverified group.
   - Replace log filenames and grep targets with the latest relevant ones.
   - Remove stale instructions for bugs that are already fixed.

4. `la-current.md`
   - Update the latest verified status and next action.
   - Keep wrapper-marker caveats.
   - Record public-file/RISC-V risk if applicable.

5. `docs/DECISIONS.md` and `docs/SYSCALL_STATUS.md`
   - Update only when syscall, ABI, process, FS, memory, signal, or scheduler
     semantics changed in a way future developers must understand.
