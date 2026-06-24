# LoongArch musl 交接文档

> 更新日期：2026-06-24
>
> 目的：为下一位开发者/Agent 在 LoongArch 线上真正跑通 `/musl` 测试提供一个干净的起点。

## 一句话目标

让 SeaOS LoongArch 真正通过所有 `/musl` 测试。如果任何真实子测试仍然失败，不能把包装层成功字符串当作通过。

## 硬性约束

- 使用官方 Docker/QEMU 环境。
- 不修改 QEMU 版本。
- 不修改 `data/sdcard-rv.img.gz`、`data/sdcard-la.img.gz`、官方测试脚本或测试二进制。
- 不通过抑制日志、伪造输出、硬编码测试输出或跳过用户程序执行来隐藏失败。
- 源码/文档保持 UTF-8。
- LoongArch 改动应限制在 `src/kernel/loongarch/` 与 `src/user/initcode_la.c` 之下，除非确实需要改公共文件。
- 若修改了公共文件，必须显式评估 RISC-V 回退风险。

## 环境

- 宿主机仓库：`/Users/daydream/Code/OS2026/oskernel2025-seaos`
- Docker 容器：`nostalgic_khayyam`
- 容器内仓库：`/workspace`
- QEMU：`/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64`
- 磁盘镜像：`/workspace/sdcard-la.img`

构建：

```bash
docker exec nostalgic_khayyam bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'
```

运行：

```bash
docker exec nostalgic_khayyam bash -lc 'cd /workspace && timeout 180 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_libctest.log 2>&1'; echo exit=$?
```

扫描：

```bash
docker exec nostalgic_khayyam bash -lc 'grep -a -n "FAIL\|failed:\|unknown syscall\|trap:\|panic\|Function not implemented\|Interrupted system call\|SEGV" /tmp/seaos_la_libctest.log | tail -180'
docker exec nostalgic_khayyam bash -lc 'grep -a -n "pthread_cancel\|pthread_cond\|sem_init\|status 247\|status 1\|Pass!" /tmp/seaos_la_libctest.log | tail -260'
```

## 当前状态

当前 `src/user/initcode_la.c` 已临时收窄为只跑：

```text
/musl/libctest_testcode.sh
```

最新已知日志：

```text
/tmp/seaos_la_libctest_timed_futex.log
```

已知仍未修复的真实失败：

```text
FAIL pthread_cancel [status 247]
FAIL pthread_cancel [status 247]
```

分别对应静态与动态的 `pthread_cancel`。

已知已修复的 libctest 用例：

- `pthread_cond`
- `pthread_cond_smasher`
- `pthread_condattr_setclock`
- 动态 `sem_init`
- `syscall_sign_extend`
- 与 `/dev/null`、stat 布局、`utimensat`、`RLIMIT_NOFILE`、close/dup stdout 行为相关的 fd/stat/time 测试

重要细节：

- 最近一次运行到达了 `GROUP END libctest-musl` 并打印了包装层成功标记。
- 这并不意味着 libctest 已经完全干净。两个 `pthread_cancel` 失败才是权威结论。
- QEMU 命令即便内核已打印 shutdown，也可能因宿主机超时而以 `124` 退出。要检查日志本身，而不只是命令退出码。

## 已完成工作

### ABI 与 exec

- LoongArch syscall ABI 在 `src/kernel/loongarch/syscall.c` 中实现。
- `exec` 现在替换活动 trap frame 并通过常规 syscall 出口返回，而不是在 syscall 内部直接跳到用户态。
- PT_INTERP 加载与 musl 动态链接器路径回退已存在。
- `exec` 重置信号状态。

### 信号与线程

- `rt_sigaction` 解析 Linux 内核 ABI 布局。
- 信号投递构建用户信号帧，包含保存的寄存器、siginfo、ucontext、保存的 mask 与 trampoline。
- `rt_sigreturn` 从信号帧/ucontext 路径恢复用户态。
- `clone(CLONE_VM)` 与 `CLONE_SETTLS` 已存在，用于 musl pthreads。
- 线程退出时 `clear_child_tid` 被清零并通过 futex 唤醒。

### Futex

- 支持 `WAIT`、`WAKE`、`REQUEUE`、`CMP_REQUEUE`、`WAIT_BITSET`、`WAKE_BITSET`。
- 带超时的等待使用调度器 deadline。
- 带超时的 futex 修复了 `pthread_cond_smasher` 与 `sem_init`。

### FD、stat、time、resource limit 修复

- FD 表大小为 128。
- fd 条目记录 close-on-exec 与 nonblocking 状态。
- 改进了 `fcntl`、`dup`、`dup3`、`pipe2`、`socket`、`accept4`。
- `/dev/null` 与 `/dev/zero` 以简单设备形式存在。
- 为 musl 测试调整了 `fstat`、`newfstatat`、`statx`、`utimensat`。
- `RLIMIT_NOFILE` 是进程局部的，fork/clone 时继承。

### 其他基础工作

- memfs 支持更大的可写文件、truncate、路径查找、时间戳。
- 存在 loopback TCP/UDP socket 层。
- 存在 `pselect6`/`ppoll` 的 readiness 基础实现。
- `mmap` 支持匿名与 MAP_PRIVATE 文件映射。

## 后续工作

### P0：修复静态 pthread_cancel

将 initcode 进一步收窄为只跑：

```text
/musl/entry-static.exe pthread_cancel
```

建议加诊断打印的位置：

- `sys_clone`：记录子线程 tid、flags、用户栈、tls、clear_child_tid。
- `sys_tgkill` / `sys_kill`：记录目标 pid/tid 与信号。
- `la_signal_deliver`：记录选中的信号、handler、restorer、旧 mask、新用户 SP、ucontext 指针、保存的 PC。
- `sys_rt_sigreturn`：记录恢复的 PC 与 mask。
- `sys_futex`：记录 WAIT/WAKE channel、期望值、返回原因（normal/EINTR/ETIMEDOUT）。
- `la_proc_exit`：记录 pid、类 tid 的 pid、exit code、clear_child_tid 唤醒情况。

要证明的链路：

```text
主线程创建目标线程
主线程发送 SIGCANCEL
目标线程若阻塞则被唤醒
目标线程收到 SIGCANCEL
目标线程执行 musl 的 cancel handler
handler 修改 ucontext 或取消状态
rt_sigreturn 返回到取消路径
目标线程退出
clear_child_tid futex 唤醒发生
主线程 join/wait 恢复
测试以 0 退出
```

修复第一个缺失的迁移。

### P1：修复动态 pthread_cancel

静态通过后，重跑动态用例：

```text
/musl/entry-dynamic.exe pthread_cancel
```

若静态通过但动态失败，检查动态链接器/TLS/auxv 与线程局部取消状态的交互。

### P2：重跑完整 libctest-musl

将 initcode 恢复为 `/musl/libctest_testcode.sh`。

通过标准：

```bash
grep -a "^FAIL " /tmp/seaos_la_libctest.log
```

必须为空，且广义失败扫描也不应出现真实失败。

### P3：恢复完整 musl 扫描

libctest 干净后，恢复 initcode 中完整的 `/musl` 扫描并逐组运行。除非用户明确选择把 unixbench/lmbench/ltp 标记为超出竞赛策略范围，否则不要跳过它们。

### P4：更新文档/状态表

在拿到真实的通过证据后，更新：

- `CLAUDE.md`
- `la-current.md`
- 若 syscall 状态变化则更新 `docs/SYSCALL_STATUS.md`
- 若语义变化则更新 `docs/DECISIONS.md`

## 风险

- 信号帧 ABI 的 bug 可能让取消看起来工作正常，却破坏后续的信号测试。保持诊断打印最小化，修好后立刻删掉。
- futex 上的捷径可能让一个 pthread 测试通过、另一个破坏。倾向真实 Linux 兼容的返回行为：被信号中断返回 `-EINTR`，只有真正超时才返回 `-ETIMEDOUT`，正常唤醒才返回 `0`。
- 修改 `exec`、trap 返回或页表/TLB 代码可能重新引入 ADEF 类的错误。保持这些改动尽量小。
- 串口日志在 QEMU 下很慢。使用带守护条件的日志，修好后立刻删掉。

## 交给队友 Agent 的提示词

在新的 Agent 会话中使用以下提示词：

```text
你在 /Users/daydream/Code/OS2026/oskernel2025-seaos 中开发 SeaOS LoongArch。
目标是让所有 /musl 测试真正通过。如果任何真实子测试仍然报告 FAIL、SEGV、panic、
unknown syscall、Function not implemented、Interrupted system call，
不能把 "test sucess" 或 "GROUP END" 这类包装层字符串当作通过。

先读 AGENTS.md、CLAUDE.md、la-current.md、docs/LOONGARCH_MUSL_HANDOFF.md。
使用官方 Docker 容器 nostalgic_khayyam 和官方 QEMU
/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64。不修改 QEMU 版本、
测试镜像、官方测试或测试二进制。

当前聚焦的测试是通过临时 initcode 入口跑 /musl/libctest_testcode.sh。
最新证据显示 libctest 仅剩两个真实失败：静态与动态 pthread_cancel，
均为 "FAIL pthread_cancel [status 247]"。
pthread_cond、pthread_cond_smasher、pthread_condattr_setclock、sem_init
已在带超时 futex 修复后通过。

先把 initcode 进一步收窄为只跑静态 pthread_cancel。
在 clone、tgkill/SIGCANCEL、la_signal_deliver、rt_sigreturn、futex
wait/wake/EINTR、la_proc_exit、clear_child_tid 唤醒 处加最少的带守护条件
的诊断打印。证明从创建线程、SIGCANCEL 投递、handler 执行、
rt_sigreturn、线程退出、clear_child_tid futex 唤醒、到 join 恢复
这条完整链路。修复第一个断裂的环节，然后同样处理动态 pthread_cancel。
两者都通过后，跑完整 libctest-musl，要求 grep '^FAIL ' 为空才能宣告成功。
之后再恢复完整 musl 扫描，逐组推进。

使用 apply_patch 做源码编辑。文件保持 UTF-8。不隐藏失败、不伪造测试输出。
LoongArch 改动尽量隔离，除非确实需要改公共文件，若改了需注明 RISC-V 风险。
```
