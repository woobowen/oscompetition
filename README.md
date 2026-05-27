## 与 oscomp 初赛测评要求的差距分析（当前仓库状态）

根据当前仓库实现与初赛公开测评要求对照，**SeaOS 目前还不能直接接入初赛自动测评**，已经确认的差距如下。

### 1. 构建入口不满足要求

初赛要求评测系统在项目根目录执行 `make all`。

当前仓库的 `Makefile` 只有 `build`、`run`、`debug`、`clean` 目标，没有 `all` 目标，因此会在评测的编译阶段直接失败。

- 现状位置：[Makefile:128-146](Makefile#L128-L146)
- 已实测现象：公开评测环境返回 `make: *** No rule to make target 'all'. Stop.`

### 2. 未生成要求的内核产物名

初赛要求 `make all` 编译后在项目根目录生成：

- `kernel-rv`
- `kernel-la`

当前仓库只生成：

- `target/kernel/kernel-qemu.elf`
- `target/mkfs/disk.img`

并不会在项目根目录生成 `kernel-rv`，也完全没有生成 `kernel-la` 的 LoongArch 内核。

- 现状位置：[Makefile:21-23](Makefile#L21-L23)
- 当前构建目标：[Makefile:119-130](Makefile#L119-L130)

### 3. 仅支持 RISC-V，未支持 LoongArch

初赛要求同时支持：

- RISC-V：`kernel-rv`
- LoongArch：`kernel-la`

当前仓库的构建、启动、链接脚本、启动汇编都围绕 RISC-V 展开，没有 LoongArch 对应实现。

- QEMU 配置：只定义了 `qemu-system-riscv64`，见 [Makefile:53-57](Makefile#L53-L57)
- 启动代码：使用 RISC-V CSR 与 `mhartid`，见 [src/kernel/boot/entry.S:7-18](src/kernel/boot/entry.S#L7-L18)
- 启动流程：直接操作 `mstatus`、`mepc`、`medeleg`、`mideleg` 等 RISC-V M 态寄存器，见 [src/kernel/boot/start.c:42-72](src/kernel/boot/start.c#L42-L72)

这意味着当前仓库**不具备生成 `kernel-la` 的基础**。

### 4. 启动方式与初赛 QEMU 参数不匹配

初赛 RISC-V 评测使用：

- `-bios default`
- `-kernel kernel-rv`

当前仓库本地运行使用：

- `-bios none`
- `-kernel target/kernel/kernel-qemu.elf`

见 [Makefile:53-57](Makefile#L53-L57)。

同时当前启动代码假定系统从 M 态直接进入：

- `entry.S` 直接读取 `mhartid`：见 [src/kernel/boot/entry.S:13](src/kernel/boot/entry.S#L13)
- `start()` 中直接配置 M 态寄存器并通过 `mret` 进入 S 态：见 [src/kernel/boot/start.c:50-72](src/kernel/boot/start.c#L50-L72)

因此当前实现**很可能不能直接适配 `-bios default` 的启动路径**。

### 5. 磁盘镜像使用方式与初赛要求不匹配

初赛要求：

- 主测试磁盘由评测机通过 `-drive file={fs}` 挂载
- 该磁盘是 **EXT4 文件系统、无分区表**
- 根目录中包含若干预编译 ELF 和 `xxxxx_testcode.sh` 脚本
- 操作系统启动后要主动扫描该磁盘、依次运行测试点
- 项目自行生成的 `disk.img` 只是可选辅助盘

当前仓库生成的 `disk.img` 是由自定义 `mkfs` 工具生成的私有镜像，不是说明中要求的评测主盘格式。

- 现状位置：[Makefile:123-126](Makefile#L123-L126)
- 用户程序被打入自制镜像，而不是从评测主盘动态扫描

也就是说，当前 SeaOS 的设计更像“把自带测试程序预置进自己的镜像”，而不是“启动后扫描评测机挂载的 EXT4 测试盘并依次执行脚本”。

### 6. 目前没有按初赛要求主动扫描并串行执行测试脚本

初赛要求操作系统：

- 扫描测试盘根目录
- 找到 `xxxxx_testcode.sh`
- 依次串行运行每个测试点
- 输出规定格式的测试开始/结束提示
- 运行完成后主动关机

当前仓库的用户程序组织是固定内置测试程序集，README 中列出的用户态程序包括：

- `test_1.c` 到 `test_5.c`
- `test_mlfq_aging.c`
- `test_mlfq_preempt.c`
- `test_schedstat.c`
- `test_workload_cpu.c`
- `test_workload_io.c`
- `test_workload_mix.c`

见 [README.md:129-147](README.md#L129-L147)。

这说明当前仓库**尚未体现“扫描测试盘并按脚本逐个执行公开测试点”的机制**。

### 7. 系统调用 ABI 与公开测试程序预期大概率不一致

初赛测试盘中的 ELF 可执行文件通常会按比赛规定 ABI 编译。当前 SeaOS 使用的是自定义 syscall 编号，例如：

- `SYS_read = 12`
- `SYS_write = 13`
- `SYS_open = 10`
- `SYS_exit = 6`

见 [src/user/syscall_num.h:1-25](src/user/syscall_num.h#L1-L25)。

如果初赛测试程序按 Linux/RISC-V 用户态 ABI 编译，那么当前 syscall 编号体系将不能直接兼容，测试程序无法正常运行。

### 8. 第三方依赖提交方式基本符合要求

初赛要求如果依赖第三方工具/库，应以源代码形式提交，不应直接提交二进制。

当前仓库从代码结构看主要为自带源码构建，且已经把 `bin2c` 作为源码工具构建，方向上是符合要求的。

但仍需注意：若后续为适配评测引入额外工具链、文件系统实现或脚本解释器，也应一并以源码方式进入仓库。

### 当前结论

综合来看，当前 SeaOS 与初赛评测要求之间的差距主要有两类：

1. **构建/产物层面的硬性不匹配**
   - 没有 `make all`
   - 没有根目录 `kernel-rv`
   - 没有 `kernel-la`

2. **运行模型层面的结构性不匹配**
   - 当前启动路径假定 `-bios none` + RISC-V M 态直启
   - 当前磁盘/文件系统模型不是“评测机挂载 EXT4 测试盘后由系统主动扫描执行”
   - 当前还没有公开测试脚本驱动框架
   - 当前 syscall ABI 与公开测试 ELF 的兼容性未知且大概率不兼容

因此，**当前仓库距离“可直接参加初赛自动测评”还有明显差距，且不仅是改 Makefile 即可解决的问题**。