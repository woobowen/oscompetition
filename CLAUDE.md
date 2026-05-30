# SeaOS — Claude Code 项目记忆

> 本文件由 Claude Code 每次会话自动加载。这里只放"规则/约定/命令"；历史与进度见 README.md 和 docs/。

## 项目一句话
SeaOS：华东师大·花狮小队，参加 oscomp 2026「OS 内核实现赛道」初赛。xv6 风格 RISC-V 内核。当前主线 = 补齐 Linux/RISC-V syscall 兼容层，跑通 /musl 测试脚本。

## 工具链与构建（见 common.mk）
- 工具链前缀：`riscv64-linux-gnu-`；运行器：`qemu-system-riscv64`
- CFLAGS 含 `-Wall -Werror` —— ⚠️ 任何未使用的变量/参数都会编译失败，写桩函数时禁止声明未使用的变量。
- `make build` → `target/kernel/kernel-qemu.elf` + `target/mkfs/disk.img`
- `make all`   → build 后复制为根目录 `kernel-rv` 和 `kernel-la`（评测入口）
- `make run`（QEMU 运行） / `make debug`（QEMU+gdb） / `make clean`

## 评测复现
固定 docker 命令见 SETUP.md §4.3（路径按本机 clone 位置改）。关键日志：
- 启动成功：`initcode: started`
- 进入测试：`run /musl/unixbench_testcode.sh`
- syscall 缺口：`unknown syscall N from pid = M`

## 硬约束（违反即回退）
1. 禁止让 `kernel-rv` 构建或运行回退；合并前用固定 docker 命令复跑确认无倒退。
2. 改公共文件须在改动说明里显式提示风险：`Makefile`、`common.mk`、通用头、
   `src/kernel/syscall/type.h`、`src/kernel/syscall/syscall.c`、`src/kernel/lib/mod.h`、`src/kernel/lib/type.h`。
3. LoongArch（B 线）改动不得影响 `kernel-rv`。当前 `kernel-la` 仍是 RISC-V 复制品。

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
