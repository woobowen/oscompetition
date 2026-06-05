# LoongArch Bring-up Notes

Current checkpoint:

- `kernel-la` is a real LoongArch ELF.
- QEMU can load it with `-kernel kernel-la`.
- Early UART output works on the QEMU virt serial device at `0x1fe001e0`.
- The current image is still a generated early boot stub, not the shared kernel.

Next integration order:

1. Replace the generated stub with a LoongArch linker script and real entry code.
2. Add LoongArch CSR helpers, trap frame layout, exception vector, and syscall entry.
3. Add user-mode return support so `proc_return` can branch into LoongArch user code.
4. Port context switch and scheduler arch hooks.
5. Add virtio PCI block-device support for the LoongArch QEMU command line.
6. Reuse EXT4 and init/test discovery once block I/O works.
