# CLAUDE.md — SeaOS LoongArch 工作记忆

> 本文件由 Claude Code / agent 每次会话自动加载。它记录 LoongArch（B 线）的
> 当前事实、命令、约束和下一个调试目标。全项目规则在 `AGENTS.md`；详细
> 交接文档在 `docs/LOONGARCH_MUSL_HANDOFF.md`；开发日志在 `la-current.md`。

## 当前优先级

- 目标：让 LoongArch 内核真正通过所有 `/musl` 测试。
- 不要把 `======== test sucess ========` 或 `#### OS COMP TEST GROUP END ... ####`
  这类包装层输出当作通过的证据。
- 只有当一个测试的真实子测试既没有 `FAIL`、没有 `[SEGV]`、没有
  `Function not implemented`、没有 `Interrupted system call`、没有 panic/trap
  失败、也没有 unknown syscall 时，才算真正通过。
- 当前聚焦：`/musl/libctest_testcode.sh`。
- glibc 工作有意推迟，等 musl 真正干净后再做。

## 仓库与环境

- 工作区：`/Users/daydream/Code/OS2026/oskernel2025-seaos`
- 迭代用 Docker 容器：`nostalgic_khayyam`
- 容器内工作区：`/workspace`
- 官方镜像 / 工具链容器：`zhouzhouyi/os-contest:20260510`
- 容器内官方 QEMU 路径：`/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64`
- 比赛用 QEMU 版本不可更改。
- 不可修改 `data/sdcard-rv.img.gz`、`data/sdcard-la.img.gz`、官方测试脚本
  或测试二进制。

构建并复制可运行内核：

```bash
docker exec nostalgic_khayyam bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'
```

用真实测试镜像跑当前 LoongArch 测试入口：

```bash
docker exec nostalgic_khayyam bash -lc 'cd /workspace && timeout 180 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_libctest.log 2>&1'; echo exit=$?
```

检查失败：

```bash
docker exec nostalgic_khayyam bash -lc 'grep -a -n "FAIL\|failed:\|unknown syscall\|trap:\|panic\|Function not implemented\|Interrupted system call\|SEGV" /tmp/seaos_la_libctest.log | tail -180'
docker exec nostalgic_khayyam bash -lc 'grep -a -n "pthread_cancel\|pthread_cond\|sem_init\|status 247\|status 1\|Pass!" /tmp/seaos_la_libctest.log | tail -260'
```

## 当前测试入口

`src/user/initcode_la.c` 临时收窄为只跑：

```c
char path[] = "/musl/libctest_testcode.sh";
char arg0[] = "libctest";
char *argv[2] = { arg0, 0 };
run_one(path, argv, "libctest_testcode.sh");
syscall1(SYS_shutdown, 0);
```

这是迭代期间有意为之。只有当 `libctest-musl` 真正干净后，才恢复完整的
`/musl` 扫描。

## 最新已验证状态

日期：2026-06-24。

最新一次运行：

```text
/tmp/seaos_la_libctest_timed_futex.log
command exit = 124 because host timeout killed QEMU after kernel printed shutdown
```

该日志中的重要结果：

```text
FAIL pthread_cancel [status 247]
FAIL pthread_cancel [status 247]
```

解读：

- 静态 `pthread_cancel` 仍然超时。
- 动态 `pthread_cancel` 仍然超时。
- `pthread_cond`、`pthread_cond_smasher`、`pthread_condattr_setclock` 和
  动态 `sem_init` 现在打印 `Pass!`。
- `open: lookup '/etc/passwd' FAILED` 会出现在日志里，但对应的测试仍然
  打印 `Pass!`；不要单独把这一行当作失败。
- 脚本到达了 `GROUP END libctest-musl` 并打印包装层 `test sucess`，但这
  不算真正通过，因为还有两个 `pthread_cancel` 失败。

## 应保留的 LoongArch 已完成工作

- `exec` 现在把新的 trap frame 拷到活动的 syscall trap frame 中，而不是在
  `sys_exec` 内部直接调用 `la_user_return()`。
- 信号投递现在构建一个更大的信号帧，包含 `siginfo`、`ucontext`、保存的
  信号 mask，以及给 `rt_sigreturn` 用的栈 trampoline。
- `rt_sigaction` 使用 Linux 内核 ABI 布局：
  `{ handler, flags, restorer, mask }`。
- FD 表从 32 扩大到 128 条。
- FD 状态现在记录 `FD_CLOEXEC` 和 `O_NONBLOCK`。
- `fcntl` 支持 `F_DUPFD`、`F_DUPFD_CLOEXEC`、`F_GETFD`、`F_SETFD`、
  `F_GETFL`、`F_SETFL`。
- `dup`、`dup3`、`pipe2`、`socket`、`accept4` 和 open 路径能更准确地
  保留或设置 fd 标志。
- 控制台写入现在要求 `LA_FD_CONSOLE`；关掉 stdout 再 dup 一个文件不再
  只按 fd 号写到 UART。
- `/dev/null` 与 `/dev/zero` 以简单字符设备形式支持。
- `fstat`、`newfstatat`、`statx` 在 libctest 使用的字段上具有
  LoongArch/musl 兼容的布局。
- `utimensat` 持久化 memfs 时间戳，支持 `UTIME_NOW`、`UTIME_OMIT`
  以及基于 fd 的 `futimens`。
- `RLIMIT_NOFILE` 是进程局部的，fork/clone 时继承。
- Futex 现在支持 `WAIT`、`WAKE`、`REQUEUE`、`CMP_REQUEUE`、
  `WAIT_BITSET`、`WAKE_BITSET`。
- 带超时的 futex 等待通过调度器 deadline 实现；这修复了
  `pthread_cond_smasher` 和 `sem_init` 的回归。

## 当前失败假设

剩下的 `pthread_cancel` 超时大概率出在以下某条路径：

- SIGCANCEL 投递的信号帧 / `ucontext` 布局与 musl 期望的 `SA_SIGINFO`
  格式不完全一致。
- musl 的 cancel handler 修改 `ucontext` 之后，`rt_sigreturn` 恢复的 mask
  或 PC 偏移有误。
- 某条取消路径上，睡眠中的线程被 `tgkill`/futex 唤醒后按普通唤醒返回，
  没有按 `-EINTR` 返回。
- 目标线程的 `clear_child_tid` 或线程退出唤醒不完整。

推荐的下一步：

1. 临时把 initcode 进一步收窄为只跑静态 `pthread_cancel`。
2. 在 `clone`、`tgkill`、信号投递、`rt_sigreturn`、futex 等待/唤醒/EINTR
   与 `la_proc_exit` 处加少量带守护条件的日志。
3. 确认目标线程是否真的收到 SIGCANCEL、进入 handler、从 `rt_sigreturn`
   返回、退出、唤醒 join 线程。
4. 同样处理动态 `pthread_cancel`。
5. 修好后立刻删掉临时诊断。

## 硬性约束

- 所有编辑过的文件保持 UTF-8。保留已有的中文注释。
- 不通过改包装层、抑制日志、伪造输出或跳过用户程序来隐藏真实失败。
- 不修改官方压缩镜像或官方测试。
- LoongArch 改动不能让 `kernel-rv` 回退。
- 如果改了 `src/kernel/loongarch/` 之外的公共/共享文件，必须显式注明
  RISC-V 的风险。

## 文档指针

- `AGENTS.md`：SeaOS 全项目规则与 RISC-V 主导的约定。
- `la-current.md`：LoongArch 当前开发日志与状态。
- `docs/LOONGARCH_MUSL_HANDOFF.md`：交给下一位 agent 或队友的交接文档。
- `docs/SYSCALL_STATUS.md`：syscall 状态表，可能滞后，musl 全绿后
  应该更新。
- `docs/DECISIONS.md`：设计决策；任何会改变 syscall/进程/内存语义的
  新兼容策略都要更新。
