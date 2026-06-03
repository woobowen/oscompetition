# SeaOS RISC-V 当前状态（2026-06-04）

## 总体进度

内核能正常启动并进入评测流程，initcode 成功 exec 各测试脚本，但所有测试目前均以 `test fail` 结束。

## 当前主要崩溃点

### musl 脚本（/musl/*_testcode.sh）

所有能通过 ELF 头检查的 musl 脚本（unixbench、busybox、lmbench、ltp）均在同一处崩溃：

```
[SEGV] pid=N trap_id=15 sepc=0x00000000001048a8 stval=0xfffffffffffff908 sp=0x0000003fffffdd40
```

**根因分析（尚未修复）**：

- `stval=0xfffffffffffff908` = `-0x6F8`，是 musl `__init_tp` 中访问 TLS 时用到的偏移地址
- musl 调用 `mmap` 分配 TLS 空间，若 mmap 返回失败，`tp` 被置为 `(void*)-1`
- 随后访问 `*(tp - 0x6F8)` = `0xfffffffffffff908`，触发 store page fault（trap_id=15）
- **问题根源在 `sys_mmap`**：原实现对非页对齐的 `len` 直接返回 `-1`（拒绝），而 Linux 语义是自动向上对齐

**已实施的修复（本会话）**：
- `sys_mmap`：将 `len` 类型从 `uint32` 改为 `uint64`；对非对齐 `len` 自动 round-up：
  ```c
  uint64 aligned_len = (len + PGSIZE - 1) & ~(PGSIZE - 1);
  uint32 npages = aligned_len / PGSIZE;
  ```
- 同时添加 `start != 0` 的守卫，避免 start=0（内核选地址）时被误拒

**但构建/评测尚未重新运行确认修复效果。**

### glibc 脚本（/glibc/*_testcode.sh）

能通过 ELF 检查的 glibc 脚本（unixbench、busybox、lmbench、ltp）崩溃于：

```
[SEGV] pid=N trap_id=13 sepc=0x00000000000d10b0 stval=0xfffffffffffffeb8 sp=0x0000003fffffde00
```

`stval=0xfffffffffffffeb8` 同样是负地址，TLS 初始化类似问题。glibc 的 TLS 初始化路径与 musl 不同，但 mmap 修复后可能同步解决。

### ELF 头解析失败（多个脚本）

多个脚本（cyclictest、netperf、iperf、libcbench、libctest、iozone、lua、basic）：

```
proc_exec: pid=N read elf header size=64 entry=0x455420504d4f4320 ...
proc_exec: pid=N invalid ELF header or read fail
```

`entry=0x455420504d4f4320` = ASCII `"ET COMP"` — 说明这些 `.sh` 文件被当作 ELF 执行，但文件内容实际是 shell 脚本文本（`# OS COMP TEST GROUP...`）。这是 exec 路径没有识别 `#!` shebang 导致的，内核需要实现 shebang 解析，或由 shell 解释器执行这些脚本。

**更深层的问题**：这些测试脚本本身是 shell 脚本，需要先启动 sh/busybox 解释器再执行。而解释器的启动依赖 mmap 先修好。

## 近期已完成的修复（本会话及上个会话）

| 修复 | 文件 | 说明 |
|------|------|------|
| stale tf 指针 | trap/trap_user.c | syscall 后 exec 替换 trapframe，加 `tf = p->tf` |
| trap_id=12 未处理 | trap/trap_user.c | case 12 (instruction page fault) 加入 stack grow 路径 |
| proc_exit 替换 panic | trap/trap_user.c | 用户态非法访问不再 kernel panic，改为 proc_exit(-11) |
| mmap len 对齐 | syscall/sysfunc.c | 非页对齐 len 自动 round-up，len 类型从 uint32→uint64 |
| memcpy 缺失 | lib/utils.c + method.h | freestanding kernel 补充 memcpy 实现 |
| 信号系统 | 多文件 | rt_sigaction、setitimer、rt_sigreturn、itimer→SIGALRM 投递链 |

## 下一步计划

1. **重新构建并运行评测**，确认 mmap 修复生效
2. 若 musl 脚本仍在 sepc=0x1048a8 崩溃，需要调试 mmap 调用时的实际参数
3. musl 脚本通过后，下一个暴露的可能是 shebang 解析或更多 syscall 缺口
4. glibc 脚本的 TLS 崩溃需要单独分析

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
