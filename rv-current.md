# SeaOS RISC-V 当前状态（2026-06-04 最新评测）

## 总体进度

内核能正常启动并进入评测流程，initcode 成功 exec 各测试脚本，但所有测试目前均以 `test fail` 结束。每个进程启动时都有三次 `[DBG] gp=0` 日志（来自 initcode 的三个 ecall），后续 exec 完成后仍然导致崩溃。

## 当前崩溃模式（按类型分类）

### 1. musl 脚本的 TLS 初始化崩溃（占多数失败）

**受影响**：`/musl/unixbench_testcode.sh`、`busybox_testcode.sh`、`lmbench_testcode.sh`、`ltp_testcode.sh`

```
proc_exec: pid=N exec done argc=4 heap_top=0x0000000000165380 tf=0x... entry=0x0000000000010148 script
[SEGV] pid=N t=15 pc=0x00000000001048a8 stval=0xfffffffffffff908 gp=0xfffffffffffff990 sp=0x0000003fffffdd40
```

**根因链条**：
1. musl 脚本成功 exec，entry=0x10148（正确）
2. `_start` 应该在 0x10148 初始化 gp，但评测日志显示 `gp=0xfffffffffffff990`（错误）
3. 程序运行到 0x1048a8（musl `__init_tp` 内部），执行 `sd s1, -0x6F8(gp)` 时：
   - gp=0 → 实际访问地址 = -0x6F8 = 0xfffffffffffff908
   - 触发 store page fault（trap_id=15）
   - 内核 uvm_ustack_grow 尝试扩栈失败（地址超出栈范围）→ proc_exit(-11)

**核心问题**：**gp 寄存器在 `_start` 初始化后被清零**。这不能通过 mmap 修复解决。

**gp 清零的唯一路径**：
- 内核中 `sys_rt_sigreturn` 执行 `tf->gp = frame[3]`
- 或者信号投递时捕获了 gp=0 的状态，导致 signal frame[3]=0，sigreturn 恢复后 gp=0

**调查结论**：gp=0 发生在进程初始化阶段（fork/exec/entry），不是后续信号干扰，需要检查 fork/exec 的 trapframe 初始化逻辑。

### 2. glibc 脚本的 TLS 初始化崩溃

**受影响**：`/glibc/unixbench_testcode.sh`、`busybox_testcode.sh`、`lmbench_testcode.sh`、`ltp_testcode.sh`

```
proc_exec: pid=N exec done argc=4 heap_top=0x00000000001c1988 tf=0x... entry=0x00000000000105a0 script
[DBG] pid=N sepc=0x00000000000d10b0 gp=0 scause=0x000000000000000d sp=0x0000003fffffde00
[SEGV] pid=N t=13 pc=0x00000000000d10b0 stval=0xfffffffffffffeb8 gp=0x0000000000000000 sp=0x0000003fffffde00
```

**特征**：
- glibc entry=0x105a0（不同于 musl）
- trap_id=13（Load page fault，不是 store）
- 同样是 gp=0，触发负地址访问
- 崩溃前有一次 trap_id=13 的 ecall（scause=0xd）触发了某个 load page fault

**根因**：与 musl 类似，gp 被清零，导致 glibc TLS 初始化时对 gp-relative 地址的访问失败。

### 3. ELF 头解析失败（无法到达 exec 阶段）

**受影响**：多个脚本（cyclictest、netperf、iperf、libcbench、libctest、iozone、lua、basic）

```
proc_exec: pid=N read elf header size=64 entry=0x455420504d4f4320 ...
proc_exec: pid=N invalid ELF header or read fail
```

**根因**：这些脚本是纯文本 shell 脚本（以 `#!` 或 `# OS COMP TEST GROUP` 开头），不是 ELF 可执行文件。内核 exec 路径不支持 shebang 解析。

**根本原因**：评测脚本设计为由 shell 解释器执行（如 `sh unixbench_testcode.sh`）。initcode 直接执行它们的文件路径时，内核应该：
1. 识别 `#!` 和 shebang 行
2. 加载指定的解释器
3. 用原脚本作为参数传给解释器

当前实现完全没有 shebang 支持。

## 近期已完成的修复（本会话及上个会话）

| 修复 | 文件 | 说明 |
|------|------|------|
| stale tf 指针 | trap/trap_user.c | syscall 后 exec 替换 trapframe，加 `tf = p->tf` |
| trap_id=12 未处理 | trap/trap_user.c | case 12 (instruction page fault) 加入 stack grow 路径 |
| proc_exit 替换 panic | trap/trap_user.c | 用户态非法访问不再 kernel panic，改为 proc_exit(-11) |
| mmap len 对齐 | syscall/sysfunc.c | 非页对齐 len 自动 round-up，len 类型从 uint32→uint64 |
| memcpy 缺失 | lib/utils.c + method.h | freestanding kernel 补充 memcpy 实现 |
| 信号系统 | 多文件 | rt_sigaction、setitimer、rt_sigreturn、itimer→SIGALRM 投递链 |
| proc_prepare_heap 日志 | proc/exec.c | 移除冗长的 printf（已完成）|

## 当前最优先修复项

### 优先级 1：修复 gp=0 问题

**关键发现**：每个进程在启动阶段就显示 `gp=0`（来自 initcode 的 ecall 前三次）。这表明问题可能出在：
1. initcode 本身的 gp 初始化
2. fork 时 trapframe 复制的问题
3. exec 时 trapframe 初始化的问题

**当前调试痕迹**：
- `[DBG] pid=N sepc=0x... gp=0 scause=...` 从 initcode 的前三个 ecall 就开始打印
- 之后 exec 完成后仍是 gp=0，导致崩溃

**下一步**：
1. 检查 initcode 的编译和 `_start` 是否正确初始化 gp
2. 查看 fork 和 exec 时 trapframe 的 gp 字段是否被正确复制/初始化
3. 考虑是否需要在 trap_user_handler 刚进入时就记录所有寄存器状态（包括实际硬件 gp vs tf->gp）

### 优先级 2：实现 shebang 解析

**影响范围**：多个测试脚本无法执行，需要识别 `#!` 并调用解释器。

### 优先级 3：glibc TLS 初始化问题

待 gp=0 问题修复后，glibc 的 trap_id=13 问题可能同步解决。

## 已知 syscall 状态

详见 [docs/SYSCALL_STATUS.md](docs/SYSCALL_STATUS.md)。

## 评测命令（复现）

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
