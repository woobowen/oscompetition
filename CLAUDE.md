# CLAUDE.md — SeaOS / LoongArch B 线（oscomp 2026）项目指引

> 本文件由 Claude Code / agent 每次会话自动加载。它是 **LoongArch（B 线）** 的工作记忆：项目整体情况、构建运行环境、**oscomp 评测环境与方法**、LoongArch 关键技术约定、当前状态与阻塞点、后续任务路线图、协作硬约束。
> 规则/约定（全项目通用，偏 RISC-V）另见 `AGENTS.md`；syscall 号权威表见 `docs/SYSCALL_STATUS.md`；设计取舍见 `docs/DECISIONS.md`；详细开发日志见 `la-current.md`（LA）与 `rv-current.md`（RV）。

---

## 0. TL;DR（先读这段）

- **项目**：SeaOS — 华东师大花狮小队，oscomp 2026「OS 内核实现赛道」初赛。xv6 风格内核，目标跑通 `/musl`、`/glibc` 下的 oscomp 测试脚本。
- **两条线**：**A 线 = RISC-V**（`src/kernel/`，成熟，101+ syscall、动态链接、pipe、memfs 齐备，是事实参考实现）；**B 线 = LoongArch**（`src/kernel/loongarch/`，移植中，**本文档对象**）。LoongArch 与 RISC-V 共用同一套 Linux「通用 ABI」syscall 号表。
- **当前状态（2026-06-12）**：**Step 11（脚本执行 / shebang）已完成并实测通过**——busybox 能 `sh` 解释执行 `*_testcode.sh`，echo 子命令打印评测标记、wait4 回收、相对路径 `exec ./libc-bench` 成功。§6 的 mallocng 崩溃（TLB 一致性）已修。**下一阻塞点**：libc-bench 需 ~80KB 用户栈但内核只预分配 32KB → Step 17 栈自动增长；libc-bench 调 socket 族 syscall（`UNKNOWN #0x42/0x71/0xa9`）→ Step 16。详见 §6。
- **常用命令（必须在 Docker 容器内执行，见 §3）**：
  - 构建：`docker exec nostalgic_khayyam bash -lc 'cd /coursegrader/submit && make build-la'`（或 `make all`）
  - 用真实测试镜像跑：`docker exec nostalgic_khayyam bash -lc 'cd /coursegrader/submit && /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 -no-reboot'`

---

## 一、项目整体情况

- **赛道**：oscomp 2026 内核实现初赛。语言 C；允许「最小 Linux 兼容语义 + 文档化的桩实现」，但必须保持行为边界清楚（返回值/errno/日志/DECISIONS 都能说清为什么）。
- **评测目标**：内核挂载 `sdcard-la.img`（4GB ext4），`initcode`（第一个用户进程）扫描文件系统、找到 `*_testcode.sh` 测试脚本，经 fork/exec/wait 执行（脚本由 `/musl/busybox sh <script>` 解释）。被测程序用 `write(fd=1,…)` 往串口输出，评测机据串口日志判分。
- **双线关系**：A 线（RV）已跑通大量测试；B 线（LA）是从零移植。**LA 的实现应以 RV 线 `src/kernel/` 为蓝本移植/翻译**，复用同一 syscall 号、同一 errno、同一测试镜像。LA 改动**不得影响 `kernel-rv`**。

---

## 二、目录结构与关键文件

### LoongArch 内核源（B 线，本线工作区）`src/kernel/loongarch/`

| 文件 | 职责 |
| --- | --- |
| `entry.S` / `boot.c` / `kernel.ld` | 内核入口、早期初始化（SP/异常向量/DMW0 恒等映射）、链接脚本 |
| `early_boot.h` | **核心常量**：地址布局（`LA_USER_BASE`/`LA_USER_STACK`/`LA_MMAP_BASE`）、PTE 位、内存布局、CSR 宏、全部子系统原型 |
| `pmem.c` | 物理页分配器（`la_pmem_alloc/free`） |
| `kvm.c` | 虚拟化/CSR 初始化占位 |
| `uvm_la.c` | **用户虚拟内存**：三级页表 walk、`la_uvm_alloc_page/map_page/copy_in`、`la_uva_to_pa`、页表深拷贝、`paging_init`（开 HPTW） |
| `tlb_la.c` | TLB 初始化、`tlb_refill`/`tlb_fill_all`、软件页表遍历 |
| `virtio_la.c` | **VirtIO PCI 块设备**：ECAM 枚举、BAR、队列初始化、`la_virtio_blk_read/write`（可靠性见 §5） |
| `fs_la.c` | **只读文件系统**：自动识别 SeaFS（`disk.img`，magic 0x12341234）与 EXT4（`sdcard-la.img`，magic 0xEF53@+0x438）。路径解析/读文件/目录枚举 |
| `proc.c` / `proc.h` | PCB、调度器、内核线程/用户进程创建、sleep/wakeup、`swtch` 上下文切换 |
| `swtch.S` / `userret.S` / `userret.c` | 上下文切换、用户态返回（DA=0/PG=1 → `ertn`） |
| `trap.c` / `trap_entry.S` / `trap_layout.h` / `trap.h` | 异常向量、trapframe、`trap_dispatch`（syscall/timer/TLB/页错误分发） |
| `timer.c` | 稳定时钟中断（100MHz/100Hz）；⚠️ 当前只计数不抢占（见 Step 12） |
| `syscall.c` | **syscall 实现 + 分发表**（`la_syscall_dispatch` @ ~L1065） |
| `exec_la.c` | ELF 加载、shebang 脚本解释器、auxv 构造、`heap_top`/`mmap_top` 初始化 |
| `initcode_la.h` | 第一个用户进程（C 源码内嵌为头）：扫描 `/musl`、`/glibc` 找 `*_testcode.sh` 并 fork/exec |

### 参考线与文档

- `src/kernel/`（A 线 RV）：成熟蓝本，syscall 实现可逐个对照移植。
- `docs/SYSCALL_STATUS.md`：syscall 号权威台账（号 = Linux 通用 ABI，LA 与 RV 共用）。
- `docs/DECISIONS.md`：不可轻易反悔的设计决策（errno 策略、动态链接 D4、mmap 约定等）。
- `AGENTS.md`：全项目规则/约定（编码、构建、评测复现、硬约束）。**本文件与 AGENTS.md 互补，不重复其规则**。
- `autotest-for-oskernel/`：oscomp 官方评测脚本与判分逻辑（见 §4）。
- `data/`：评测用的压缩镜像（`sdcard-la.img.gz`、`sdcard-rv.img.gz`）——**只读，禁止改动**。
- 根目录 `sdcard-la.img`、`sdcard-rv.img`：解压后的可写运行副本（4GB ext4）。

---

## 三、构建与运行环境（⚠️ 关键）

### 为什么必须在 Docker 容器内

`Makefile:38` 的 `LA_BUILD_MODE` 会**自动探测**：若 PATH 上有 `loongarch64-linux-gnu-gcc` + `-ld` 则 `source`（真源码构建），否则回退 `stub`（用 `la_elfgen` 生成一个最小脚手架 ELF，不含真实内核逻辑）。

- **macOS 宿主无 LoongArch 工具链** → 本地 `make build-la` 会静默走 `stub`，产物是个空壳。所有真实构建**必须在容器内**。
- 容器：`nostalgic_khayyam`（镜像 `zhouzhouyi/os-contest:20260510`，含 LoongArch 工具链 + QEMU 10.0.2）。项目挂载点 `/coursegrader/submit`（即宿主仓库根）。

### 构建命令

```bash
# 1. 确保 docker 容器在跑
docker start nostalgic_khayyam   # 若已停

# 2. 容器内构建（产物 target/loongarch/kernel-la.elf）
docker exec nostalgic_khayyam bash -lc 'cd /coursegrader/submit && make build-la'
# 或一步到位：build + 复制到根 kernel-la / kernel-rv（评测入口）
docker exec nostalgic_khayyam bash -lc 'cd /coursegrader/submit && make all'
```

- 工件：`target/loongarch/kernel-la.elf` → `make all` 复制为根 `kernel-la`（**评测机消费的就是根 `kernel-la`**）。
- `make clean` 清 `target/`。CFLAGS 含 `-Wall -Werror`：写桩函数时**禁止留未使用变量/参数**，否则编译失败。

### 运行命令（注意镜像陷阱）

- ⚠️ **`make run-la` 默认挂 `target/mkfs/disk.img`（SeaFS，magic 0x12341234），不是真实测试镜像**。跑真实 oscomp 测试必须改挂 `sdcard-la.img`（ext4）：

```bash
docker exec nostalgic_khayyam bash -lc 'cd /coursegrader/submit && \
  /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 \
    -kernel kernel-la -m 1G -nographic -smp 1 -no-reboot \
    -drive file=sdcard-la.img,if=none,format=raw,id=x0 \
    -device virtio-blk-pci,drive=x0'
```

- `make check-la`：8s 超时冒烟测试，只 grep `loongarch boot start` + `[init] kernel ready`（用 disk.img，非真实测试）。
- 退出 QEMU：`Ctrl-A X`。

---

## 四、评测环境与方法（oscomp）

### 评测流程（官方 autotest）

1. 评测机 `git clone` 你的仓库，在 `zhouzhouyi/os-contest:20260510` 容器内 `make all` 构建 → 得 `kernel-la`。
2. 用 `sdcard-la.img` 作 virtio-blk 后端启动内核。
3. 内核 `initcode` 扫描 ext4 根、找到 `*_testcode.sh`，fork/exec 执行（`busybox sh <script>`），子进程串口输出结果。
4. 评测机解析串口日志，按每个测试用例的预期输出判 pass/fail。

### 本地完整复现评测（来自 AGENTS.md / SETUP.md §4.3）

```bash
sudo docker run --rm \
  -v "<本机仓库>:/coursegrader/submit" \
  -v "<本机仓库>/data:/coursegrader/testdata" \
  -v "<本机仓库>/autotest-for-oskernel:/cg" \
  -v "<本机仓库>/data:/mnt/cghook/" \
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```
（路径按本机 clone 位置改；macOS 下 `sudo` 视权限可省。）停止用 `docker stop`，别强杀 python（会留锁）。

### 测试镜像内容（`sdcard-la.img`）

- `/musl/`：unixbench、busybox（**静态链接** ELF）、cyclictest、lmbench、libctest/libcbench 等二进制 + `*_testcode.sh` 脚本。
- `/glibc/`：glibc 版同类测试。
- 所有二进制：`ELF 64-bit LSB executable, LoongArch`。部分（如 dhry2）**动态链接**，interpreter `/lib64/ld-musl-loongarch-lp64d.so.1`。

### 关键串口日志标记

| 标记 | 含义 |
| --- | --- |
| `loongarch boot start` / `[init] kernel ready` | 启动成功（`check-la` 检查这两条） |
| `run /musl/<xxx>_testcode.sh` | 进入某个测试 |
| `syscall: UNKNOWN #N` | 缺口 syscall（号 N）——按号补实现 |
| `initcode: exec fail!` | exec 失败（shebang/ELF 加载问题） |
| `trap: TLB refill FAIL badv=… pc=…` | 用户态页错误/崩溃现场 |

### 评测硬约束（违反即回退/判负）

1. **禁止改 `data/sdcard-*.img.gz`**（评测镜像）；也禁止改评测脚本/测试用例绕过判分。
2. **禁止虚假过测**：不得吞 panic/SEGV/unknown-syscall 日志、不得把失败伪装成成功、不得硬编码分数/测试输出/跳过用户程序。
3. LA 改动**不得让 `kernel-rv` 构建或运行回退**；改公共文件（`Makefile`/`common.mk`/通用头/`src/kernel/syscall/*`）须在提交说明提示风险。

---

## 五、LoongArch 关键技术约定（写代码前必读）

### syscall ABI

- 系统调用号在 `$a7`（`tf->gpr[LA_GPR_A7]`），参数 `a0–a5`，返回值放 `a0`。
- 号表 = Linux 通用 ABI（与 RISC-V/ARM64 一致）。权威表见 `docs/SYSCALL_STATUS.md`。
- 失败返回 `(uint64_t)(-errno)`（见 DECISIONS D1/D2）；未知号返回 `-ENOSYS` 并打印 `syscall: UNKNOWN #N`（不 panic）。

### 地址空间布局（`early_boot.h`）

| 常量 | 值 | 含义 |
| --- | --- | --- |
| `LA_USER_BASE` | `0x1000` | 用户代码最低地址 |
| （heap） | `p->heap_top` 起 | brk 堆，向上生长；exec 初始化为最高 LOAD 段末尾页对齐 |
| `LA_MMAP_BASE` | `0x4000000000`（256GB） | mmap 区，向上生长；**与 brk 堆/栈分离**（`p->mmap_top`） |
| `LA_USER_STACK` | `0x7FFFFFE000` | 用户栈顶（39 位空间） |
| 内核 | `0x200000` 入口，DMW0 恒等映射 | 低内存 0x0–0x10000000 管理内核/用户物理页 |

> **brk/mmap 必须分离**：早期 mmap 从 `heap_top` 分配，与 brk 互踩导致 musl malloc 元数据损坏。已修复——mmap 走独立高地址区。fork 时须复制 `child->mmap_top = parent->mmap_top`。

### 页表项（PTE）位（`uvm_la.c`）

```
[0]V 有效 | [1]D 脏(=可写) | [3:2]PLV 特权(3=所有 PLV 可访问=用户) |
[5:4]MAT 内存访问类型 | [6]G 全局 | [7]P Present | [8]W Write(HPTW) |
[61]NR NoRead | [62]NX NoExecute
- LA_PTE_U_RWX = V|D|PLV_USER|P|W = 0x18F   ← 用户 RWX（代码/堆/mmap 常用）
- 目录项(root/mid)只存 PA，无标志；叶项(leaf)用完整 V|D|PLV|P|W。
```
- 开启了 HPTW（`paging_init` 写 PWCH bit24=HPTW_EN），QEMU 直接走页表填 TLB，大幅减少 TLB refill 异常。

### VirtIO 块读可靠性约定（`virtio_la.c`，已踩大坑）

- 轮询完成时必须 `dbar 0`（数据屏障）保证 CPU 看到 DMA 写。
- **仅靠 dbar 不可靠**：QEMU 单 CPU 异步 I/O 下，纯 RAM 轮询死循环不会让出 vCPU 给设备层。**必须在轮询循环里周期性重写 `VIRTIO_PCI_QUEUE_NOTIFY`（MMIO）**（每 `0x3fff` 次）触发队列再处理；配大超时（`50_000_000`）。
- available ring 发布要 `dbar` 排序：`avail[idx]=desc; dbar; avail->idx++; dbar;`。`la_used` 与 `la_used_idx` 须 `volatile`。

### 当前已实现 syscall（LA 分发表，`syscall.c`，约 45 个）

进程：`fork wait waitid exit exit_group getpid gettid getppid getcwd exec clone`
文件 I/O：`open close read write lseek dup dup3 fstat get_dentries ioctl newfstatat faccessat readlinkat fcntl`
目录：`chdir mkdir`
内存：`brk mmap munmap mprotect`
信号：`rt_sigaction rt_sigprocmask`（桩）
身份：`getuid geteuid getgid getegid`（均返回 0=root）
线程：`set_tid_address set_robust_list`
信息：`statx uname`
其他：`msync pipe2 sched_yield setrlimit getrlimit prlimit64`
系统：`shutdown`

> 号表/语义对照 `docs/SYSCALL_STATUS.md`（RV 已有 101+ 实现，多数可直接移植）。`SYS_pipe2` 当前**桩**（环形缓冲区尚未实现，见 Step 14）。

### 动态链接状态

- exec 已识别 `#!` shebang，把 `sh` 脚本交给 `/musl/busybox`（静态链接）解释——**当前主线走静态 busybox**。
- **PT_INTERP 动态链接尚未实现**（RV 线 D4 已有蓝本：扫 PT_INTERP/PT_PHDR，加载 interp 到固定基址，auxv 输出 AT_PHDR/AT_BASE/AT_ENTRY）。LA 若要跑 dhry2 等动态程序需移植（见 Step 13，可延后）。

---

## 六、当前状态与阻塞点（2026-06-12）

### 已跑通

- 内核启动 → 用户态 → syscall → 返回，整链路通。
- VirtIO 块读稳定（dbar + 周期重写 QUEUE_NOTIFY）。
- brk/mmap 地址分离；ext4 挂载/路径解析/读文件/目录枚举可用。
- `initcode` 能在 ext4 找到 `/musl/libcbench_testcode.sh`，exec 成功加载 `/musl/busybox sh /musl/libcbench_testcode.sh`。
- **Step 11（脚本执行）已通**（实测 `/tmp/la-step11b.log`）：busybox `sh` 解释执行脚本，`./busybox echo "#### OS COMP TEST GROUP START libcbench-musl ####"` 打印到串口、子进程 exit=0 被 wait4 回收；继续 `exec ./libc-bench`（相对路径解析成功）加载运行并向 stdout 写。

### 已解决：busybox 启动崩溃（mallocng 一致性检查）—— Step 11 期间定位并修复

- **现象**：busybox 启动到 `getcwd` 后崩，`pc=0x1201ac9b8 badv=0x0`（musl `a_crash()`，mallocng `get_meta()` 一致性检查失败）；写 brk 堆页 `0x1201ff028` 的 store 被丢弃。
- **真根因（TLB 一致性，非页表只读）**：所有叶 PTE 带 G（全局）位、**无 ASID**，TLB 条目按 VPPN（even/odd 对）索引。exec 跨地址空间后，旧镜像残留的全局 TLB 条目与新映射同 VPPN 别名，CPU 命中陈旧条目 → store 落到错误物理页（被吞）。
- **修复**：`proc.c` 在 `la_proc_return` 与调度器**每次进入用户地址空间前**都 `la_tlb_inval_all()` 再 `la_tlb_fill_all(p->pgtbl)`（不再只对 pid==1 清）；`uvm_la.c::la_uvm_map_page` 每次新映射后 `la_tlb_inval_page(va)`，防止半对（even/odd）影子。
- **配套修复**：`sys_open`/initcode 改 openat ABI（曾误读 a0 当路径 → EPERM）；`sys_wait` 无子返回 `-ECHILD` + 支持 `WNOHANG`；`sys_exec` 失败返回 `-ENOENT`（非 -1，避免 musl 把 -1 读成 EPERM 掩盖真因）；`la_do_exec_syscall` 三大 argv 数组改 `static`（4KB 内核栈撑不住 4.5KB 局部数组，第二次 exec 取指 ADEF）；`fs_la.c` 相对路径从 `la_fs_cwd_ino` 起查。

### 当前阻塞点（属后续 Step）

1. **用户栈自动增长（Step 17）**：libc-bench 需 ~80KB 栈，内核 exec 只预分配 8 页（32KB）→ `trap: TLB refill FAIL badv=0x7ffffea5f8`（栈区缺页）。trap.c 当前 TLB refill 失败即 `for(;;){}` 挂死，导致一个重栈程序崩溃后内核停摆、后续测试无法进行。
2. **socket 族 syscall（Step 16）**：libc-bench 调 `connect(0x42)` 等 → `syscall: UNKNOWN #N`。需按号补实现或返回合理 errno。

### 旁注

- 近期一次全量跑显示 16/16 测试 `initcode: exec fail!`，与本线单进程 exec 已通的结论**有出入**——疑为那次跑用了 `disk.img`（SeaFS）而非 `sdcard-la.img`，或镜像未挂载。回归前先确认启动命令挂的是 `sdcard-la.img`。

---

## 七、后续任务路线图（Step 10–21）

### 推荐执行顺序

```
✅ Step 10 (修EXT4 bug) → ✅ Step 11 (脚本执行) → ⬜ Step 12 (定时器抢占)
  → ⬜ Step 13 (动态链接) → ⬜ Step 14 (管道) → ⬜ Step 15 (文件写入)
  → ⬜ Step 16 (补全syscall) → ⬜ Step 17 (栈增长) → ⬜ Step 18 (堆增强)
  → ⬜ Step 19 (资源回收) → ⬜ Step 20 (块缓存) → ⬜ Step 21 (集成验证)
```
> **Step 10、Step 11 已完成并实测通过**（详见 §6 + `la-current.md`）。**当前最短解阻塞路径 = Step 17（栈自动增长）+ Step 16（socket 族 syscall）**，二者是 libc-bench 这类重栈/用网络的程序跑通的必要条件；Step 12（定时器抢占）可防止单进程长跑独占 CPU。

### 各步详情

#### Step 10：修复 EXT4 目录枚举 Bug（🔴 P0）— ✅ 已完成
- 问题：挂 ext4 成功但 `open("/musl")+get_dentries` 返回空。已修；现能找到测试脚本。
- 实现（`fs_la.c`）：只读 EXT4 驱动——superblock/组描述符/inode/extent 树块映射（`e4_lbn2pb`，支持多级索引+叶子 extent）/文件读；`e4_dir_lookup` 按 `rec_len` 线性扫描、`e4_get_dentries` 转 Linux `dirent64`；按 magic 自动识别 SeaFS vs EXT4。
- 验证：`open '/musl' -> ino=0xc`、`getdents n=0x3f8`，initcode 扫到 `*_testcode.sh`。

#### Step 11：脚本执行（#! shebang）（🔴 P0）— ✅ 已完成
- exec 识别 `#!`，读解释器路径，原路径失败回退 `/musl/busybox`，重建 argv `[解释器, 脚本路径, 原argv…]`，加载静态 busybox。**已跑通**：busybox `sh` 解释执行脚本，`echo` 打印评测标记、wait4 回收、相对路径 `exec ./libc-bench` 成功。
- 本轮修复（`exec_la.c`/`syscall.c`/`proc.c`/`uvm_la.c`/`tlb_la.c`/`fs_la.c`）：§6 mallocng 崩溃（TLB 一致性）、EPERM 掩码（openat ABI + 正确 errno）、exec 4KB 内核栈溢出（大数组改 static）、相对路径解析（`la_fs_cwd_ino`）。详见 §6。

#### Step 12：定时器抢占调度（🔴 P0）
- `la_timer_interrupt()` 当前只计数不调度，长任务独占 CPU。
- 加 tick 计数，超时间片（如 10 ticks）标记 RUNNABLE + `la_proc_yield()`；trap 返回路径保存/恢复 trapframe。
- 涉及：`timer.c proc.c trap.c proc.h`（加 `ticks` 字段）。

#### Step 13：动态链接器（🟡 P1，可延后）
- 扫 PT_INTERP/PT_PHDR，加载 musl dynamic linker，auxv 输出 AT_PHDR/AT_PHNUM/AT_ENTRY/AT_BASE，入口改 interp entry。蓝本：RV 线 DECISIONS D4。
- 备选：busybox 静态，先跑 busybox 组。涉及 `exec_la.c`。

#### Step 14：管道实现（🔴 P0）
- `la_pipe`（4KB 环形缓冲 + 读/写指针 + 端开闭标志 + sleep/wakeup）；`SYS_pipe/pipe2`、read/write/close 对 pipe fd、fork 继承。
- 脚本大量用管道（`a | b`），无管道 unixbench 跑不动。涉及 `syscall.c proc.h`。

#### Step 15：文件系统写入（🔴 P0）
- 推荐 **memfs**（内存小 FS，16MB，挂可写路径）：`creat/unlink/mkdir/rmdir`、fd write 落 memfs，不动只读 ext4。
- 备选：ext4 写入（块/inode/目录项分配，复杂）。涉及 `fs_la.c syscall.c early_boot.h proc.h`。

#### Step 16：补全关键 syscall（🟡 P1）
对照 `docs/SYSCALL_STATUS.md`，按 `syscall: UNKNOWN #N` 日志逐个补：`gettimeofday(78) clock_gettime(113) times(100) clone(2/220) ioctl(29已有桩) fcntl(25已有) uname(160已有) …`。涉及 `syscall.c`。

#### Step 17：栈自动增长（🟡 P1）
- trap 检测栈区页错误，在 `[stack_bottom, stack_top)` 内按需分配页（上限如 16 页=64KB）。`proc.h` 加 `stack_bottom`。涉及 `trap.c uvm_la.c proc.h`。

#### Step 18：堆/mmap 增强（🟢 P2）
- mmap 支持 `MAP_ANONYMOUS/MAP_FIXED`；fork 正确复制/共享 mmap。涉及 `syscall.c uvm_la.c`。

#### Step 19：资源回收与稳定性（🟡 P1）
- exec/exit 后释放旧页表与物理页；wait 回收子进程全部资源；`LA_NPROC` 提到 64+（unixbench 多 fork）。涉及 `proc.c uvm_la.c exec_la.c`。

#### Step 20：缓冲区缓存（🟢 P2）
- LRU 块缓存（如 256 块=1MB），读先查缓存，未命中再 VirtIO。涉及新增 `bio_la.c` 或并入 `fs_la.c`。

#### Step 21：综合集成与验证
- `sdcard-la.img` 全量跑；逐个验证 unixbench/busybox/cyclictest musl 子测试；`make all` + 本地评测复现全过。

### 实施优先级总览

| Step | 内容 | 优先级 | 工作量 | 依赖 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 10 | 修复 EXT4 目录 bug | 🔴 P0 | 小 | 无 | ✅ 已完成 |
| 11 | 脚本执行 shebang | 🔴 P0 | 中 | Step 10 | ✅ 已完成 |
| 12 | 定时器抢占调度 | 🔴 P0 | 小 | 无 | ⬜ 待做 |
| 13 | 动态链接器 | 🟡 P1 | 大（可延后） | Step 11 | ⬜ 待做 |
| 14 | 管道实现 | 🔴 P0 | 中 | Step 11 | ⬜ 待做 |
| 15 | 文件系统写入 | 🔴 P0 | 大 | Step 10 | ⬜ 待做 |
| 16 | 补全关键 syscall | 🟡 P1 | 中 | Step 14,15 | ⬜ 待做（当前阻塞点） |
| 17 | 栈自动增长 | 🟡 P1 | 小 | 无 | ⬜ 待做（当前阻塞点） |
| 18 | 堆/mmap 增强 | 🟢 P2 | 中 | 无 | ⬜ 待做 |
| 19 | 资源回收 | 🟡 P1 | 中 | 无 | ⬜ 待做 |
| 20 | 缓冲区缓存 | 🟢 P2 | 中 | 无 | ⬜ 待做 |
| 21 | 集成验证 | 🔴 P0 | 视情况 | 全部 | ⬜ 待做 |

---

## 八、协作约定与硬约束

- **编码**：所有源码/头/脚本/文档 UTF-8；修改时**保留原有中文注释**，不得改成乱码/问号/U+FFFD/GBK；新增注释优先英文（除非用户要中文）。Windows/PowerShell 下操作中文内容前先确认编码。
- **不破坏 RV**：LA 改动不得让 `kernel-rv` 构建/运行回退；改公共文件须在提交说明标风险。
- **不碰镜像/评测**：禁止改 `data/sdcard-*.img.gz`、评测脚本、测试用例；禁止虚假过测（吞日志/伪装成功/硬编码输出/跳过用户程序）。
- **单调改进**：新补丁须保持已通过的 unixbench-musl、busybox-musl 不回退。
- **最小兼容要可解释**：允许桩实现，但返回值/errno/日志/DECISIONS 必须说清边界；无法用最小实现保正确性时转完整实现并记 DECISIONS。
- **提交**：仅在用户要求时 commit/push；提交信息结尾加 `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。默认分支 `master`，当前工作分支 `os2026-1`——若在默认分支须先开分支。
- 全项目通用规则详见 `AGENTS.md`。

---

## 九、调试技巧与常见坑

- **加诊断**：用 `la_uart_putc/puts/put_hex`（`early_boot.h`）。注意生产路径上临时诊断用完即清，避免刷屏拖慢（每条 UART 输出在 QEMU 下都很慢）。
- **反汇编被测二进制**（容器内）：
  - 从镜像取文件：`debugfs -R "dump /musl/busybox /tmp/bb" sdcard-la.img`
  - 程序头：`loongarch64-linux-gnu-readelf -l /tmp/bb`
  - 反汇编片段：`loongarch64-linux-gnu-objdump -d --start-address=0x... --stop-address=0x... /tmp/bb`
  - 静态 ELF 的 VA≈文件偏移（首段 file off 0 → VA 0x120000000），崩溃 VA 减基址即文件偏移。
- **trap 指令 dump 有 bug**：`trap.c` 崩溃时取指令用 `(tf->era>>3)&1` 选 8 字节字，**索引错误**，别盲信它打印的指令——以 objdump 反汇编为准。
- **常见崩溃模式速查**：
  - `badv=0x0` + pc 落在 `move $t0,$zero; st.b $zero,$t0,0` → musl/busybox **故意 abort**（`a_crash`），上游某项一致性/断言检查失败，往回看反汇编找触发条件（见 §6 案例）。
  - 堆/mmap 元数据错乱 → 多半是 brk/mmap 地址重叠、或匿名页未清零、或 store 被吞。
  - virtio 读随机超时 → 检查 dbar + 周期 QUEUE_NOTIFY 是否在位（§5）。
- **构建走 stub 的坑**：若日志显示内核行为像空壳、或体积异常小，先确认 `make build-la` 输出的是 `(source)` 而非 `(stub)`——stub = 本地无工具链，去容器内构建。
