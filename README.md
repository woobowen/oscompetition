# SeaOS 当前对接 oscomp 初赛评测进度（截至本次调试）

## 一、已完成事项

### 1) 评测构建入口已打通

- 已支持评测要求的 `make all`。
- 根目录可生成：
  - `kernel-rv`
  - `kernel-la`（当前为占位复制，见后文说明）

相关位置：

- [Makefile:128-134](Makefile#L128-L134)

### 2) RISC-V 启动路径已适配评测参数

根据 `os_serial_out_rv.txt`，当前可在评测参数下正常启动：

- `qemu-system-riscv64 -bios default -kernel kernel-rv ...`
- OpenSBI 正常进入
- 内核完成启动、调度、文件系统初始化
- `initcode: started` 正常输出

说明 RISC-V 启动链路（`kernel-rv` + `-bios default`）已经打通。

### 3) EXT4 测试盘识别与脚本执行入口已打通

当前日志已出现：

- `ext4 filesystem detected on primary disk`
- `run /musl/unixbench_testcode.sh unixbench_testcode`

并且 `proc_exec` 已经把 `.sh` 脚本切换到解释器执行路径（日志尾部 `script`）：

- `proc_exec: ... exec done ... script`

这表示：

- 测试盘扫描与测试脚本选取已进入实跑阶段
- shebang/解释器选择链路已生效（`parse_shebang` / `pick_script_interpreter`）

---

## 二、当前阻塞点（最新）

RISC-V 目前阻塞在用户态程序启动后的 Linux 兼容 syscall 缺失：

- `unknown syscall 96 from pid = 2`
- `panic! syscall`

对应 `os_serial_out_rv.txt` 末尾可见。

`96` 在 Linux/RISC-V ABI 中对应 `set_tid_address`。许多 musl/busybox 程序启动阶段会调用它；当前内核未实现，且“未知 syscall 直接 panic”，导致测试中断。

**结论：现在不是启动问题，不是 ext4 扫描问题，也不是脚本解释器问题；核心是 syscall 兼容层还不够。**

---

## 三、LoongArch 当前状态

LoongArch 仍未实装：

- `kernel-la` 目前不是 LoongArch 原生内核构建链产物
- 评测侧仍会出现：`qemu-system-loongarch64: could not load kernel 'kernel-la': Failed to load ELF`

本轮工作优先级是先把 RISC-V 流程跑通，LoongArch 作为后续独立工作项。

---

## 四、分工

A、B 两条线可以并行推进：A 负责 RISC-V 得分主线，B 负责 LoongArch 架构启动主线。两边尽量避免同时大改公共文件；如果必须改 `Makefile`、通用头文件或 syscall 公共定义，需要提前同步。

### A：RISC-V syscall 兼容与测试推进

目标：从当前 `unknown syscall 96` 开始，补齐 Linux/RISC-V 兼容 syscall，至少跑通第一个 `/musl/unixbench_testcode.sh` 测试；如果时间允许，继续推进到下一个测试脚本。

当前起点：

- RISC-V 已经能启动
- EXT4 测试盘已识别
- 已经执行到 `/musl/unixbench_testcode.sh`
- `.sh` 脚本解释器路径已生效
- 当前阻塞在 `unknown syscall 96`

具体任务：

1. 补 `syscall 96 (set_tid_address)` 的最小实现。
2. 用固定 docker 评测命令继续跑，记录下一个 `unknown syscall N` 或新的崩溃点。
3. 按“最小可运行兼容”原则继续补 musl/busybox 启动期所需 syscall。
4. 优先目标是让 `unixbench_testcode.sh` 能完整执行并返回。
5. 若第一个测试跑通，继续尝试推进下一个测试脚本。

验收标准：

- 必须：不再停在 `unknown syscall 96`
- 必须：`unixbench_testcode.sh` 至少能继续明显向后执行
- 目标：完整跑完 `unixbench_testcode.sh`
- 加分：继续进入并推进下一个测试脚本

### B：LoongArch 架构启动主线

目标：把 LoongArch 从“QEMU 无法加载 kernel-la”推进到“能加载、能进入早期内核、能看到明确启动日志”。不强求进入测试脚本，但最好可以像现在的risc-v一样能够进入测试脚本。

当前起点：

- `kernel-la` 还不是合法 LoongArch 内核
- QEMU 当前报 `could not load kernel 'kernel-la': Failed to load ELF`
- 当前仓库主体仍是 RISC-V 架构实现

具体任务：

1. 建立 LoongArch 独立最小构建路径，生成真正的 LoongArch ELF，而不是复制 RISC-V ELF。
2. 实现最小 LoongArch 入口和链接脚本，让 `qemu-system-loongarch64 -kernel kernel-la ...` 不再报 `Failed to load ELF`。
3. 打通最小串口输出，至少能在 `os_serial_out_la.txt` 中看到自定义启动日志，例如 `loongarch boot start`。
4. 梳理后续要进入测试还缺的模块清单：trap、syscall 入口、用户态返回、virtio 块设备、EXT4、initcode/test 扫描等。
5. 尽量把 LoongArch 代码放在独立架构目录中，避免破坏 RISC-V 当前可运行路径。

验收标准：

- 必须：`kernel-la` 不再是 RISC-V ELF 复制品
- 必须：QEMU LoongArch 不再报 `Failed to load ELF`
- 目标：`os_serial_out_la.txt` 能看到 LoongArch 早期启动日志
- 加分：进入更完整的内核初始化阶段，但不强制进入测试脚本

### 协作要求

- A 的 RISC-V 主线优先保证不回退。
- B 的 LoongArch 改动不要影响 `kernel-rv` 的构建和运行。
- 合并前都用固定 docker 评测命令跑一次，确认 RISC-V 状态没有倒退。
