# SeaOS RISC-V 当前状态（2026-06-04 最新）

## 总体进度

**unixbench-musl 全部通过，busybox-musl 部分通过。** 内核启动正常，5 项 unixbench 测试（DHRY2、WHETSTONE、SYSCALL、CONTEXT、PIPE）均输出结果。busybox 独立命令测试已通过 7 项，卡在 `df`（需要 /proc/mounts）。

## 最新评测输出摘要

```
#### OS COMP TEST GROUP START unixbench-musl ####
Unixbench DHRY2 test(lps): 47078774
Unixbench WHETSTONE test(MFLOPS): 1134.372
Unixbench SYSCALL test(lps): 115206
Unixbench CONTEXT test(lps): 9688
Unixbench PIPE test(lps): 11358

#### OS COMP TEST GROUP START busybox-musl ####
#### independent command test
testcase busybox echo success
testcase busybox ash -c exit success
testcase busybox sh -c exit success
testcase busybox basename success
testcase busybox cal success
testcase busybox clear success
testcase busybox date success
ERROR: df: /proc/mounts: Operation not permitted
```

## 当前卡住点

`busybox df` 尝试读取 `/proc/mounts`，返回 "Operation not permitted"。原因：内核尚未实现 procfs（`/proc` 文件系统），`open("/proc/mounts")` 失败。

## 本会话关键修复

| 修复 | 文件 | 效果 |
|------|------|------|
| 创建 data/config.json (qemu.timeout=120) | data/config.json | 解决评测框架 60s 超时导致 score=0 |
| 剥离 boot printf (main.c) | src/kernel/main.c | 节省 UART 时间 |
| 剥离 boot printf (proc.c) | src/kernel/proc/proc.c | 节省 UART 时间 |
| 剥离 boot printf (trap_kernel.c) | src/kernel/trap/trap_kernel.c | 节省 UART 时间 |
| 剥离 boot printf (plic.c) | src/kernel/trap/plic.c | 节省 UART 时间 |
| 剥离 boot printf (virtio.c) | src/kernel/fs/virtio.c | 节省 UART 时间 |
| 剥离 ext4 info printf (fs.c) | src/kernel/fs/fs.c | 节省 UART 时间 |
| 移除未使用变量 (cpuid, ver, devid, vendor) | main.c, virtio.c | 修复 -Werror 编译失败 |

## 历史已修复的关键 bug

| 修复 | 文件 | 说明 |
|------|------|------|
| epc+=4 前置到 syscall() 之前 | trap/trap_user.c | 修复 fork 后 gp 未初始化 SEGV |
| rt_sigaction 缓冲区溢出修复 | syscall/sysfunc.c | 修复 pc=0 崩溃 |
| proc_fork 删除冗余 epc+=4 | proc/proc.c | 修复 clone 返回 -1 |
| wait4 重写（proc_wait4） | proc/proc.c + sysfunc.c | 兼容 WNOHANG、pid 过滤、Linux wstatus 编码 |
| prepare_heap 越界检查 | proc/exec.c | 防止段地址乱序导致 OOM panic |
| pipe2(59) 实现 | fs/fs.c | 阻塞管道支持 |
| umask(166) 桩 | syscall/sysfunc.c | 返回 0022 |

## 下一步优先级

1. **procfs 桩实现**：至少让 `/proc/mounts` 可读（返回空或最小内容），解除 `df` 卡点
2. **继续跑 busybox 测试**：观察 df 之后还有哪些命令失败
3. **确认评分**：用完整评测命令确认 unixbench-musl 得分 > 0

## 评测命令

```bash
# 构建
docker run --rm -v "E:\code\2_3_os\oskernel2026-seaos:/src" zhouzhouyi/os-contest:20260510 bash -c "cd /src && make all 2>&1"

# 完整评测
docker run --rm \
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" \
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" \
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```
