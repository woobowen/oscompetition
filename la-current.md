# LoongArch 当前状态

> 最后更新：2026-06-24
>
> 本文档面向开发者，记录 LoongArch 线的当前状态。当前的首要目标是让所有
> `/musl` 测试真正通过。glibc 不是当前目标。

## 当前目标

让 LoongArch 内核真正通过所有 musl 测试。

是否通过必须看真实测试输出，而不是包装层字符串：

- `======== test sucess ========` 只是本机包装层的尾部标记。
- `#### OS COMP TEST GROUP END ... ####` 只说明脚本跑到了末尾。
- 如果任何子测试仍报 `FAIL`、`[SEGV]`、`Function not implemented`、
  `Interrupted system call`、panic、trap 失败或 unknown syscall，
  那么这个测试组就不算干净。

## 环境

- 宿主机工作区：`/Users/daydream/Code/OS2026/oskernel2025-seaos`
- Docker 容器：`nostalgic_khayyam`
- 容器内工作区：`/workspace`
- 官方镜像 / 工具链：`zhouzhouyi/os-contest:20260510`
- 官方 LoongArch QEMU：`/opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64`
- 真实测试磁盘：`sdcard-la.img`

构建：

```bash
docker exec nostalgic_khayyam bash -lc 'cd /workspace && make build-la && cp target/loongarch/kernel-la.elf kernel-la'
```

跑当前只跑 libctest 的入口：

```bash
docker exec nostalgic_khayyam bash -lc 'cd /workspace && timeout 180 /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 > /tmp/seaos_la_libctest.log 2>&1'; echo exit=$?
```

失败扫描：

```bash
docker exec nostalgic_khayyam bash -lc 'grep -a -n "FAIL\|failed:\|unknown syscall\|trap:\|panic\|Function not implemented\|Interrupted system call\|SEGV" /tmp/seaos_la_libctest.log | tail -180'
```

## 当前测试状态

当前 `src/user/initcode_la.c` 临时收窄为：

- 跑 `/musl/libctest_testcode.sh`
- 然后调用 `shutdown`

最新证据：

- 日志：Docker 容器内 `/tmp/seaos_la_libctest_timed_futex.log`。
- QEMU 命令退出码：`124`，因为宿主机 `timeout` 在内核打印
  `shutdown: system halting` 之后杀掉了 QEMU。
- 脚本到达了 `GROUP END libctest-musl`。
- 仍然真实失败的项：

```text
FAIL pthread_cancel [status 247]
FAIL pthread_cancel [status 247]
```

含义：

- 静态 `entry-static.exe pthread_cancel` 仍然超时。
- 动态 `entry-dynamic.exe pthread_cancel` 仍然超时。
- 下列之前失败的 libctest 用例现在通过：`pthread_cond`、
  `pthread_cond_smasher`、`pthread_condattr_setclock`，以及动态 `sem_init`。
- 有些测试过程中会出现 `open: lookup '/etc/passwd' FAILED`，但对应测试
  仍然打印 `Pass!`；不要单独把这一行当作失败。

## 应保留的已完成工作

### 进程、exec 与信号

- `exec` 不再在 syscall 实现内部直接跳到用户态。它把替换上下文拷到活动
  trap frame 中，由常规 syscall 返回路径走 `ertn`。
- `exec` 重置信号相关状态，避免被替换地址空间里旧的 handler 残留复用。
- 信号投递现在构建一个更大的用户信号帧：保存的 GPR、保存的 ERA、之前的
  信号 mask、`siginfo`、`ucontext`，以及给 `rt_sigreturn` 用的栈 trampoline。
- `rt_sigaction` 读取 Linux 内核 ABI 布局：handler、flags、restorer、mask。
- `rt_sigreturn` 从信号帧 / ucontext 路径恢复状态。
- 实现了 `clone(CLONE_VM)` 与 `CLONE_SETTLS`，给 musl pthreads 用。
- 线程退出时清掉 `clear_child_tid` 并唤醒 futex 等待者。

### Futex 与调度器

- Futex 支持 `WAIT`、`WAKE`、`REQUEUE`、`CMP_REQUEUE`、`WAIT_BITSET`、
  `WAKE_BITSET`。
- 带超时的 futex 等待用调度器 deadline：
  `la_proc_sleep_chan_until(chan, deadline_ticks)`。
- 调度器在 deadline 到达时唤醒 futex 睡眠者并标记为超时。
- 这修复了之前 `pthread_cond_smasher` 和 `sem_init` 的失败。

### 文件描述符与设备

- `LA_NFD` 现在是 128。
- fd 条目记录 `cloexec` 与 `nonblock`。
- `fcntl` 实现 `F_DUPFD`、`F_DUPFD_CLOEXEC`、`F_GETFD`、`F_SETFD`、
  `F_GETFL`、`F_SETFL`。
- `dup`、`dup3`、`pipe2`、`socket`、`accept4` 与 open 路径更准确地
  保留/设置 fd 标志。
- 控制台写入会检查 `LA_FD_CONSOLE`，不再无条件地把 fd 0-2 当作控制台。
  这修复了 `fflush_exit` 类 close/dup 行为。
- `/dev/null` 与 `/dev/zero` 以简单字符设备形式实现。

### 文件系统、stat、时间与资源限制

- memfs 支持更大的文件、truncate、路径查找与持久化时间戳。
- `fstat`、`newfstatat`、`statx` 使用与 LoongArch musl 测试字段兼容的布局。
- 支持 `newfstatat(fd, "", ..., AT_EMPTY_PATH)`。
- `statx` 支持基于 fd 的空路径查找，并在属性/时间/设备字段上给出
  测试期望的正确偏移。
- `utimensat` 支持基于路径名和基于 fd 的时间戳更新、`UTIME_NOW`、
  `UTIME_OMIT`。
- `RLIMIT_NOFILE` 是进程局部的，fork/clone 时继承。
- fd 分配路径遵守进程的 nofile 限制。

### 网络与 mmap 基线

- `socket_la.c/h` 中存在 loopback TCP/UDP socket 层。
- `pselect6` 与 `ppoll` 已经为 pipe/socket/console 路径做 readiness 检查。
- `mmap` 支持匿名映射和当前测试用到的 MAP_PRIVATE 文件映射。

## 当前失败：pthread_cancel

当前 libctest 证据中只剩 `pthread_cancel` 在失败。

观察到的症状：

- `entry-static.exe pthread_cancel`：`FAIL pthread_cancel [status 247]`
- `entry-dynamic.exe pthread_cancel`：`FAIL pthread_cancel [status 247]`

可能的原因：

- SIGCANCEL 投递的信号帧还不是 musl 期望的精确格式。
- `ucontext` 中信号 mask 或保存的 PC 偏移不完整。
- `rt_sigreturn` 没能正确恢复 musl 修改过的 mask/PC 路径。
- 睡眠中的目标线程可能按普通 futex 唤醒返回，而不是 EINTR。
- 线程退出在这条取消路径上可能没有唤醒 join 的父线程。

推荐的调试顺序：

1. 临时把 initcode 从完整 libctest 收窄为单一静态用例：
   `entry-static.exe pthread_cancel`。
2. 在这些点附近加带守护条件的日志：`clone`、`tgkill`、
   `la_signal_deliver`、`rt_sigreturn`、futex 等待唤醒/EINTR、
   `la_proc_exit` 和 `clear_child_tid`。
3. 证明这条链路：pthread 创建 → cancel 信号发出 → 目标收到 SIGCANCEL →
   handler 执行 → 取消路径让线程退出 → `clear_child_tid` 唤醒 → join/等待
   线程恢复。
4. 修复第一个断裂的环节。
5. 同样处理动态 `pthread_cancel`。
6. 删掉临时日志，重跑完整 `/musl/libctest_testcode.sh`。

## 移除了过时的指引

旧的状态小节被压缩了，因为它们把包装层完成、临时跳过策略、glibc 探索与
早期的 ADEF 调查混在一起，已经偏离了当前 musl 的目标。active 指引很简单：
修内核直到 musl 真正通过，再回到 glibc 和更长的 benchmark 组。

## 开发日志

### 2026-06-24 — libctest-musl 收窄到 pthread_cancel

带超时 futex 修复之后的状态：

- 完整只跑 libctest 的运行能到达 group end。
- 静态与动态 `pthread_cancel` 仍以 `status 247` 失败。
- `pthread_cond`、`pthread_cond_smasher`、`pthread_condattr_setclock` 和
  动态 `sem_init` 通过。
- QEMU 仍在宿主机 timeout 下运行，所以即便内核打印了 shutdown，命令
  退出码仍可能是 `124`。判断时一定要看日志本身。

含有相关未提交改动的文件：

- `src/kernel/loongarch/proc.h`
- `src/kernel/loongarch/proc.c`
- `src/kernel/loongarch/syscall.c`
- `src/kernel/loongarch/exec_la.c`
- `src/kernel/loongarch/memfs_la.c`
- `src/kernel/loongarch/memfs_la.h`
- `src/kernel/loongarch/boot.c`
- `src/kernel/loongarch/trap.c`
- `src/user/initcode_la.c`
- 自动生成的 `src/kernel/loongarch/initcode_la.h`

### 更早完成的阶段（摘要）

- LoongArch 启动、trap、timer、进程、页表与 virtio-block 引导。
- EXT4 读路径与目录枚举。
- ELF 加载器、PT_INTERP 加载器、auxv 构造、shebang 处理。
- 管道、memfs 写路径、fd 表、cwd 相对路径处理。
- 给 musl pthreads 用的 clone / futex / 信号 / 线程基线。
- 给 AF_INET TCP/UDP 用的 loopback socket 层。
- 优先级感知的调度器与 select/poll 基线。
- 文件映射 mmap 基线。
- libc 与 LTP 风格探测的长尾 syscall 补齐。

详细历史被有意压缩了，因为很多早期状态声明是基于包装层完成、而不是基于
真实子测试成功。
