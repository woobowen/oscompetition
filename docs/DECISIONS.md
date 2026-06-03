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
