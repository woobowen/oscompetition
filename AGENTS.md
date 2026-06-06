# SeaOS — Claude Code 项目记忆

> 本文件由 Claude Code 每次会话自动加载。这里只放"规则/约定/命令"；历史与进度见 README.md 和 docs/。

## 项目一句话
SeaOS：华东师大·花狮小队，参加 oscomp 2026「OS 内核实现赛道」初赛。xv6 风格 RISC-V 内核。当前主线 = 补齐 Linux/RISC-V syscall 兼容层，跑通 /musl 测试脚本。

## 字符编码要求
- 所有源代码、头文件、脚本、Markdown 文档和测试文件都必须使用 UTF-8 编码。
- 修改文件时必须保留原有中文注释，不得将中文注释改写成乱码、问号、Unicode replacement character（U+FFFD）或 GBK/CP936 形式的错误字符。
- 如果需要新增注释，优先使用英文注释；除非用户明确要求中文注释。
- 在 Windows、PowerShell、VS Code 或终端环境中操作文件前，应假设编码风险较高。涉及中文内容时，先确认文件编码为 UTF-8，再修改文件。
- 如果发现文件中已有乱码，不要继续基于乱码内容改写；应根据代码内容与上下文恢复原始中文注释，再修改。

## 工具链与构建（见 common.mk）
- 工具链前缀：`riscv64-linux-gnu-`；运行器：`qemu-system-riscv64`
- CFLAGS 含 `-Wall -Werror` —— ⚠️ 任何未使用的变量/参数都会编译失败，写桩函数时禁止声明未使用的变量。
- `make build` → `target/kernel/kernel-qemu.elf` + `target/mkfs/disk.img`
- `make all`   → build 后复制为根目录 `kernel-rv` 和 `kernel-la`（评测入口）
- `make run`（QEMU 运行） / `make debug`（QEMU+gdb） / `make clean`

## 评测复现
固定测评 docker 命令见 SETUP.md §4.3：
`docker run --rm `
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" `
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" `
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip`（路径按本机 clone 位置改）。
  测评结果见 `os_serial_out_rv.txt`。
  关键日志：
- 启动成功：`initcode: started`
- 进入测试：`run /musl/unixbench_testcode.sh`
- syscall 缺口：`unknown syscall N from pid = M`

## 硬约束（违反即回退）
1. 禁止让 `kernel-rv` 构建或运行回退；合并前用固定 docker 命令复跑确认无倒退。
2. 禁止改动`data\sdcard-rv.img.gz`和`data\sdcard-la.img.gz`（评测镜像）。
3. 改公共文件须在改动说明里显式提示风险：`Makefile`、`common.mk`、通用头、
   `src/kernel/syscall/type.h`、`src/kernel/syscall/syscall.c`、`src/kernel/lib/mod.h`、`src/kernel/lib/type.h`。
4. LoongArch（B 线）改动不得影响 `kernel-rv`。当前 `kernel-la` 仍是 RISC-V 复制品。

## 正确性、安全与稳定性原则
- 以 SeaOS 内核本身的安全、稳定、可解释、单调改进为优先目标；评测通过只能作为验证结果，不能反过来牺牲内核基本正确性。
- 允许“最小 Linux 兼容语义”和有文档说明的桩实现，但必须保持行为边界清楚：返回值、errno、日志、DECISIONS/SYSCALL_STATUS 都要能说明为什么这样做。
- 最小兼容只是首选落点，不是硬性上限；如果无法用最小实现保持正确语义、安全性、稳定性或测试可解释性，就必须转向完整实现/系统性修改，并同步记录设计取舍。
- 禁止通过小技巧虚假掩盖真实错误：不得吞掉 panic/SEGV/unknown syscall 日志，不得把失败测试伪装成成功，不得修改评测脚本/测试镜像/自动测评代码绕过问题。
- 禁止为了消除 warning/error 文本而隐藏问题本身；只有在确认 warning 是可选能力提示、且真实故障另有根因时，才可以降噪，并需在 docs/DECISIONS.md 记录依据。
- 修复应尽量单调：新补丁必须保持已通过的 unixbench-musl、busybox-musl 不回退；若引入兼容策略或资源上限调整，需说明风险和后续收敛方向。
- 对 syscall/FS/进程/内存等公共路径的改动，优先修正真实语义或最小兼容语义；不要用硬编码分数、硬编码测试输出、跳过用户程序执行等方式“过测”。

## 推理强度使用策略
默认使用 `high`。只有当 bug 跨多个 OS 模块、测试信息极少、怀疑内存破坏/竞态/ABI 问题，或 `high` 多轮无法收敛时，才临时切换到 `xhigh`。`xhigh` 只用于深度定位和系统性分析，不用于无节制重写；定位完成后切回 `high`，并执行最小必要 patch。

## 架构地图（注意：arch/ 目录名有误导，实为纯 RISC-V，无 LoongArch 脚手架）
`src/kernel/{boot,mem,proc,trap,syscall,fs,lock,arch,lib}`
- `syscall/`：`syscall.c`（按号索引的稀疏函数指针分发表）、`sysfunc.c`（实现）、`type.h`（号定义，`SYS_MAX_NUM=502`）
- syscall 号取自 `tf->a7`，返回值写回 `tf->a0`，类型 `uint64`。

## 新增一个 syscall 的约定（四处都要改）
1. `src/kernel/syscall/type.h`    ：`#define SYS_xxx <号>`  // 注明 Linux/RISC-V ABI 号
2. `src/kernel/syscall/method.h`  ：`uint64 sys_xxx();`      // 声明
3. `src/kernel/syscall/syscall.c` ：跳转表加 `[SYS_xxx] sys_xxx,`
4. `src/kernel/syscall/sysfunc.c` ：写实现，函数头注明参数语义与返回值含义

## 错误码约定（见 docs/DECISIONS.md D1）
- 命名错误码统一在 `src/kernel/lib/errno.h`（标准 Linux/asm-generic 数值）。
- syscall 出错返回 `(uint64)(-EXXX)`；musl 据此判错（`> -4096UL` 视为错误并置 errno）。
- 未知/未实现 syscall 返回 `(uint64)(-ENOSYS)` 并 printf，不 panic（D2）。

## 进度与状态的权威位置（不要在多处重复记，避免漂移）
- 评测对接进度：README.md
- syscall 逐个状态台账：docs/SYSCALL_STATUS.md
- 设计决策日志：docs/DECISIONS.md
