# SeaOS 设计决策日志（DECISIONS）

> 一行一条不可轻易反悔的决策：日期 / 决策 / 理由。改决策须新增一条，不删旧条（在旧条标注"被 Dx 取代"）。

## D1（2026-05-29）errno 策略：新建 src/kernel/lib/errno.h
- 决策：在 `src/kernel/lib/errno.h` 定义标准 Linux（asm-generic）errno 数值，经 `lib/mod.h` 全局可用。
- 理由：后续数十个 syscall 需要命名错误码；musl/busybox 判错依赖真实 Linux 数值；裸 `-38` 散落不可维护。
- 注：RISC-V 使用 asm-generic errno 表，数值与多数 Linux 架构一致（ENOSYS=38, EINVAL=22, EBADF=9 ...）。

## D2（2026-05-29）未知 syscall 策略：panic → 返回 -ENOSYS 继续执行
- 决策：`src/kernel/syscall/syscall.c` 对未知/未实现号不再 `panic`，改为 printf 号码 + 返回 `(uint64)(-ENOSYS)`。
- 理由：让内核继续往后跑，一次性暴露整条缺失 syscall 链，再按实测逐批补（不盲目预埋）。
- 代价：缺失从"硬停"变"软失败"，可能在更靠后处以更隐晦方式崩；靠保留的 printf 仍可在日志定位每个缺口。

## D3（2026-05-29）syscall 96 set_tid_address 最小实现：返回 pid
- 决策：仅返回 `myproc()->pid`；暂不存储 tidptr，暂不实现退出时 clear_child_tid 清零 + futex 唤醒。
- 理由：单线程/进程模型下 TID==PID；futex/线程退出语义按后续实测需求再补，符合"最小可运行兼容"。

## D4（2026-06-03）exec 支持动态链接 ELF（PT_INTERP + auxv）
- 决策：`src/kernel/proc/exec.c` 新增动态链接器加载支持：
  - 扫描 PT_INTERP 段读取 interpreter 路径；扫描 PT_PHDR 段记录 phdr 地址。
  - 新增 `load_interp()` 将解释器加载到固定基址 `0x40000000`（1GB），并设 entry 为解释器入口。
  - `prepare_stack()` 输出完整 auxv：AT_PHDR、AT_PHENT、AT_PHNUM、AT_PAGESZ、AT_BASE、AT_ENTRY、AT_RANDOM、AT_NULL（仅在有 interp 时输出 AT_BASE/AT_ENTRY）。
  - interpreter 路径支持 fallback：原路径 → `/musl/lib/libc.so` → `/lib/libc.so` 等变体。
  - 静态 ELF（无 PT_INTERP）走原有流程不受影响。
- 理由：oscomp 2026 评测的用户程序均为 musl 动态链接；不支持 PT_INTERP 就无法执行任何测试。
- 代价：加载器按 RWX 映射 interpreter 所有段（忽略段权限），依赖 mprotect 桩不报错；后续如需 W^X 需回填。
- 关联：N_OPEN_FILE_PER_PROC 从 10 提升至 32（动态链接器 open 较多 fd）。

## D5（2026-06-03）dup3 (24) 实现策略
- 决策：关闭 newfd 已有文件（若有），`file_dup(open_file[oldfd])` 存入 newfd 槽位，忽略 flags（O_CLOEXEC 无实现）。
- 理由：musl/busybox 在 fork/exec 路径大量使用 dup3；O_CLOEXEC 在单线程无 exec-close 语义时无影响。

## D6（2026-06-03）mprotect (226) 桩实现
- 决策：直接返回 0，不做任何页表权限修改。
- 理由：动态链接器自重定位时调用 mprotect 修改段权限；由于 D4 已用 RWX 映射所有段，权限实际已满足，桩即可。

## D7（2026-06-04）mmap (222) len 自动 page 对齐
- 决策：`sys_mmap` 对非页对齐的 `len` 参数自动向上对齐：`aligned_len = (len + PGSIZE - 1) & ~(PGSIZE - 1)`；同时将 `len` 类型从 `uint32` 改为 `uint64`（超过 4GB 映射场景安全）。
- 理由：Linux 内核 mmap(2) 规范明确说明 length 会自动向上对齐到页边界。musl 的 `__init_tp`（TLS 初始化）调用 `mmap(NULL, tls_size, ...)` 时 `tls_size` 不一定对齐，原内核直接返回 -1 导致 `tp = -1`，后续 `*(tp - 0x6F8)` = `0xfffffffffffff908` 触发 store page fault，所有 musl 测试崩溃于 sepc=0x1048a8。
- 代价：多分配 < 1 页内存（可接受）；start=0 时 `start % PGSIZE != 0` 检查保留 `start != 0` 守卫，避免误拒内核选址请求。

## D8（2026-06-04）用户态非法访问 panic → proc_exit(-11)
- 决策：`trap_user.c` 的异常 default 分支和 `uvm_ustack_grow` 失败时，改为 `proc_exit(-11)`（SIGSEGV 语义），不再 `panic`。
- 理由：用户程序 bug 不应拉垮整个内核；`proc_exit` 让进程终止、父进程 wait 回收，后续测试可继续运行。原 panic 会导致评测在第一个出错测试处停止，无法评估后续测试的通过情况。
- 代价：非法访问变"静默失败"而非"硬停"，调试时需依赖 `[SEGV]` printf 日志定位。

## D9（2026-06-04）信号系统最小实现（itimer → SIGALRM → 信号投递链）
- 决策：实现完整的信号投递闭环：
  1. `proc_t` 增加 `sig_handler[65]`、`sig_restorer`、`sig_pending`、`sig_delivering`、`itimer_expire`、`itimer_interval` 字段。
  2. `sys_rt_sigaction` 读取 musl sigaction 结构（152B），存储 handler 和 restorer。
  3. `sys_setitimer` 将 ITIMER_REAL 超时转换为 CLINT 到期时间写入 proc_t。
  4. `timer_interrupt_handler` 检查 `p->itimer_expire`，到期时置 `sig_pending` SIGALRM bit 并重设 interval。
  5. `trap_user_handler` 在返回用户态前检查 `sig_pending`，构建 256B signal frame（epc + 31 GP 寄存器）写入用户栈，重定向 tf 执行 handler。
  6. `sys_rt_sigreturn` 从用户栈恢复 signal frame，epc -= 4 补偿 syscall 路径的 epc += 4。
- 理由：dhry2reg 测试依赖 setitimer/SIGALRM 判断 10 秒计时结束；无信号投递则 dhry2reg 死循环，无法产出分数。
- 代价：signal frame 仅保存 GP 寄存器（不含 FP/向量寄存器）；嵌套信号通过 `sig_delivering` 标志简单阻止（非 Linux 完整语义）。
