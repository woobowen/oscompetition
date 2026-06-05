# LoongArch 启动记录

## 当前检查点

- `kernel-la` 已经是合法 LoongArch ELF。
- QEMU 可以通过 `-kernel kernel-la` 加载。
- QEMU virt 串口地址 `0x1fe001e0` 已可输出早期日志。
- 已补 `entry.S`、`boot.c`、`trap_entry.S`、`trap.c`、`syscall.c`、`userret.S`、`userret.c`、`kernel.ld`，有 LoongArch 工具链时可走源码构建路径。
- 已固定早期 trap frame 布局、CSR 异常号解析和 syscall 分发入口；trap 入口会保存/恢复通用寄存器，syscall 返回前会推进 `ERA += 4`。
- 已补 `la_user_return(tf)` 用户态返回骨架：设置 `ERA`，设置 `PRMD.PPLV=3/PIE=1`，恢复 GPR 后执行 `ertn`。
- 已补 `la_trap_frame_init_user()` 和 `la_proc_return()`，用于把 LoongArch 进程 trap frame 初始化对接到用户态返回路径。
- 当前 syscall 统一返回 `-ENOSYS`，还没有和共享 syscall 表对接。
- 当前机器没有 `loongarch64-linux-gnu-*` 工具链时，`make build-la` 会自动回退到生成器 stub，保证评测入口仍可验证。

## 后续接入顺序

1. 准备 LoongArch 交叉工具链，至少需要 `loongarch64-linux-gnu-gcc` 和 `loongarch64-linux-gnu-ld`。
   检查命令：
   ```bash
   command -v loongarch64-linux-gnu-gcc
   command -v loongarch64-linux-gnu-ld
   ```
2. 在有 LoongArch 工具链的环境里验证源码构建路径：
   ```bash
   LA_BUILD_MODE=source make build-la
   LA_BUILD_MODE=source make check-la
   ```
3. 移植上下文切换和调度器架构钩子。
4. 支持 LoongArch QEMU 命令使用的 `virtio-blk-pci` 块设备。
5. 块设备可读写后复用 EXT4、initcode 和测试入口扫描。
