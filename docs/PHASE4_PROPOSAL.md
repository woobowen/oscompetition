# Phase 4.2 Proposal

## Goal: Iozone + Lmbench Complete Through

Continue from Phase 4.1 (mmap file mapping done but not verified through iozone/lmbench tests due to QEMU ADEF bug).

## Blockers

| Blocker | Status | Plan |
|---------|--------|------|
| ADEF (QEMU erratum) | Workaround coded, awaiting long-run verification | Run overnight test; iteratively tighten workaround if needed |
| QEMU speed (Docker on macOS) | ~2 min per sub-test, 10+ min for libcbench alone | Use `nohup` overnight run or Linux host |
| Test chain stuck at libcbench-musl | Cannot verify later groups | ADEF workaround should unblock |

## Next Steps (Priority Order)

### 1. Overnight long-run test
```bash
# Run in container, no timeout, with serial log
docker exec nostalgic_khayyam bash -lc \
  'cd /workspace && /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 \
   -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot \
   -drive file=sdcard-la.img,if=none,format=raw,id=x0 \
   -device virtio-blk-pci,drive=x0' \
  > /tmp/os_serial_overnight.txt 2>&1
```
Let it run 4-8 hours (or until GROUP END appears for multiple test groups).

### 2. Analyze serial log
```bash
# After overnight run:
grep -E 'GROUP END|GROUP START|spurious|UNKNOWN|panic|SEGV' /tmp/os_serial_overnight.txt
```

### 3. If ADEF workaround works (GROUP END appears)
- Verify busybox-musl group starts
- Check for new UNKNOWN syscalls
- Fix any test failures
- Verify netperf/iperf groups (socket layer testing)

### 4. If ADEF workaround doesn't work
- Options:
  a. Try QEMU 10.1.5 (newer LoongArch fixes)
  b. Report QEMU bug to competition organizers
  c. Try disabling HPTW, use software-only TLB refill

## Open Questions
- Does the competition environment use the same QEMU 10.0.2?
- Will they accept workaround patches for known QEMU bugs?
- Is there a Linux host available for faster testing?
