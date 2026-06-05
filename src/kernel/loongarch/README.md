# LoongArch 启动记录

## 当前检查点

- `kernel-la` 已经是合法 LoongArch ELF。
- QEMU 可以通过 `-kernel kernel-la` 加载。
- QEMU virt 串口地址 `0x1fe001e0` 已可输出早期日志。
- 已补 `entry.S`、`boot.c`、`kernel.ld`，有 LoongArch 工具链时可走源码构建路径。
- 当前机器没有 `loongarch64-linux-gnu-*` 工具链时，`make build-la` 会自动回退到生成器 stub，保证评测入口仍可验证。

## 后续接入顺序

1. 在有 LoongArch 工具链的环境里验证 `LA_BUILD_MODE=source make build-la check-la`。
2. 补 LoongArch CSR 读写、异常向量、trap frame 布局和 syscall 入口。
3. 补用户态返回路径，让 `proc_return` 可以进入 LoongArch 用户程序。
4. 移植上下文切换和调度器架构钩子。
5. 支持 LoongArch QEMU 命令使用的 `virtio-blk-pci` 块设备。
6. 块设备可读写后复用 EXT4、initcode 和测试入口扫描。
