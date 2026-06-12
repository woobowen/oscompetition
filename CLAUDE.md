# CLAUDE.md — SeaOS / LoongArch B 线（oscomp 2026）项目指引

> 本文件由 Claude Code / agent 每次会话自动加载。它是 **LoongArch（B 线）** 的工作记忆：项目整体情况、构建运行环境、**oscomp 评测环境与方法**、LoongArch 关键技术约定、当前状态与阻塞点、后续任务路线图、协作硬约束。
> 规则/约定（全项目通用，偏 RISC-V）另见 `AGENTS.md`；syscall 号权威表见 `docs/SYSCALL_STATUS.md`；设计取舍见 `docs/DECISIONS.md`；详细开发日志见 `la-current.md`（LA）与 `rv-current.md`（RV）。

---

## 0. TL;DR（先读这段）

- **项目**：SeaOS — 华东师大花狮小队，oscomp 2026「OS 内核实现赛道」初赛。xv6 风格内核，目标跑通 `/musl`、`/glibc` 下的 oscomp 测试脚本。
- **两条线**：**A 线 = RISC-V**（`src/kernel/`，成熟，101+ syscall、动态链接、pipe、memfs 齐备，是事实参考实现）；**B 线 = LoongArch**（`src/kernel/loongarch/`，移植中，**本文档对象**）。LoongArch 与 RISC-V 共用同一套 Linux「通用 ABI」syscall 号表。
- **当前状态（2026-06-12 21:00）**：**Step 11 + Step 17 + Step 19 + Step 16a（clone CLONE_VM 线程 + futex）已完成并实测通过**——栈按需扩栈、用户态崩溃不挂死、exec/exit/wait 现在释放用户页表+内核栈（`initcode: fork fail!` 13→0，物理内存不再耗尽）、clone(CLONE_VM) 创建真正的共享地址空间线程（每批 2–8 个，12 个已创建和 join，badv=0x28 失效）。**当前状态**：libcbench-musl 已通过线程创建阶段，在运行快结束时存在一个已存在的 ADEF→INE 级联（与 clone 无关，是 `proc.c` 调度器注释中描述的过时 PGDL 问题）。**下一步优先级**：Step 14（管道，busybox 脚本需要 `|`）> 继续补全 Step 16（socket 族）> Step 12（定时器抢占）。socket 族 UNKNOWN #0x42/#0x71/#0xa9 已用 ENOSYS 处理，不是崩溃原因，但 iperf/netperf 需要完整实现。详见 §6。
- **常用命令（必须在 Docker 容器内执行，见 §3）**：
  - 构建：`docker exec nostalgic_khayyam bash -lc 'cd /workspace && make build-la'`（或 `make all`）
  - 用真实测试镜像跑：`docker exec nostalgic_khayyam bash -lc 'cd /workspace && /opt/qemu-bin-10.0.2/bin/qemu-system-loongarch64 -kernel kernel-la -m 1G -nographic -smp 1 -drive file=sdcard-la.img,if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 -no-reboot'`
  - ⚠️ 容器 `nostalgic_khayyam` 的仓库挂载点是 **`/workspace`**（实测），非 `/coursegrader/submit`（那是官方评测容器的路径）。

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
- `docs/LOONGARCH-ARCHITECTURE.md`：**LA 内核架构（小白向）总览**——按真实源码逐子系统图解（硬件背景 / 启动 / 内存 / 进程调度 / trap·中断·syscall / exec / fs / virtio / initcode），含"一次测试脚本执行的完整旅程"全链路串联与 10 条"踩过的坑"精华。新人入门 LA 线先读此篇。
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
- **Step 17（栈自动增长 + 用户态异常不再挂死）已通**（实测 `/tmp/la-step17c.log`）：① 用户栈按需向下扩栈（`la_uvm_grow_stack`，上限 `LA_MAX_STACK_PAGES=512` 页/2MB），`badv=0x7ffffea5f8`（libc-bench ~80KB 栈）不再挂死、连续扩栈成功；② trap.c 两处 `for(;;){}` 改为终结出错用户进程（`la_proc_exit(-11)`，内部清 ISTLBR）+ 内核态才 panic——单个测试崩溃不再拖死后续 15 个；③ `sys_exit`/`exit_group` 复用 `la_proc_exit`，fork 复制 `stack_bottom`。实测 libcbench-musl 完整跑通、**0 崩溃 0 挂死**，initcode 继续进入 `/glibc/` 组。
- **Step 19（进程资源回收）已通**（实测 `/tmp/la-step19.log`）：① 新增 `la_uvm_free_pgtbl`（`uvm_la.c`）整表释放（数据页+leaf/mid/root 表页）；② `la_proc_free`（`proc.c`，改 public）释放 pgtbl+kstack；③ `sys_wait` 回收子进程改为 `la_proc_free(child)`；④ exec 在 argv 拷贝完成、安装新表、TLB 清+填之后释放旧表 `old_pgtbl`。**安全前提**：fork 深拷贝页表（`uvm_la.c:404-416` 逐字节复制，无共享/COW），故每张表下页归唯一进程私有。实测 `initcode: fork fail!` **13→0**（物理内存不再耗尽），构建 `(source)` 0 warning，无 use-after-free（initcode 从不 exec、其页表不被任何释放路径触及）。

### 已解决：busybox 启动崩溃（mallocng 一致性检查）—— Step 11 期间定位并修复

- **现象**：busybox 启动到 `getcwd` 后崩，`pc=0x1201ac9b8 badv=0x0`（musl `a_crash()`，mallocng `get_meta()` 一致性检查失败）；写 brk 堆页 `0x1201ff028` 的 store 被丢弃。
- **真根因（TLB 一致性，非页表只读）**：所有叶 PTE 带 G（全局）位、**无 ASID**，TLB 条目按 VPPN（even/odd 对）索引。exec 跨地址空间后，旧镜像残留的全局 TLB 条目与新映射同 VPPN 别名，CPU 命中陈旧条目 → store 落到错误物理页（被吞）。
- **修复**：`proc.c` 在 `la_proc_return` 与调度器**每次进入用户地址空间前**都 `la_tlb_inval_all()` 再 `la_tlb_fill_all(p->pgtbl)`（不再只对 pid==1 清）；`uvm_la.c::la_uvm_map_page` 每次新映射后 `la_tlb_inval_page(va)`，防止半对（even/odd）影子。
- **配套修复**：`sys_open`/initcode 改 openat ABI（曾误读 a0 当路径 → EPERM）；`sys_wait` 无子返回 `-ECHILD` + 支持 `WNOHANG`；`sys_exec` 失败返回 `-ENOENT`（非 -1，避免 musl 把 -1 读成 EPERM 掩盖真因）；`la_do_exec_syscall` 三大 argv 数组改 `static`（4KB 内核栈撑不住 4.5KB 局部数组，第二次 exec 取指 ADEF）；`fs_la.c` 相对路径从 `la_fs_cwd_ino` 起查。

### 当前阻塞点（属后续 Step）

1. **`clone(CLONE_VM)` 线程支持 + futex（Step 16a，P0）— ✅ 已完成（2026-06-12）**：clone(CLONE_VM) 现在创建真正的共享地址空间线程（共享 pgtbl + mm 游标），futex(98) WAIT/WAKE 使 pthread_join 能返回。实测 12 个线程在 4 批中创建并成功 exit=0。`clone: CLONE_VM not supported` 和 `badv=0x28` 空指针解引用已消失。**实现**：`proc.h/c`（shared_vm、clear_child_tid、channel-keyed sleep/wakeup、tf 释放修复、共享 pgtbl 所有权转移）、`syscall.c`（重写 sys_clone 适配 LoongArch ABI `clone(flags,stack,ptid,ctid,tls)`、新增 sys_futex、thread-exit cleartid + futex wake、exit_group 组终止、sys_wait shared_vm 过滤器、brk/mmap mm 重构）、`exec_la.c`（mm 重构、旧 tf 释放）。详情见 `la-current.md` §Step 16a。
2. **socket 族 syscall `#0x42/#0x71/#0xa9`（Step 16 剩余项）**：libc-bench **已正确处理 ENOSYS**（在 clone 崩溃发生之前有 60,000+ 行 UNKNOWN 打印，仍正常运行），所以 socket 存根不是崩溃原因。iperf/netperf 需要完整的 loopback socket 实现；libc-bench 可优雅降级。
3. **管道（Step 14，P0）**：脚本 `a | b` 依赖 `pipe2`，当前是桩（环形缓冲未实现），busybox 测试脚本广泛使用。**这是 clone+futex 完成后最高杠杆的下一步。**
2. **定时器抢占（Step 12）**：`la_timer_interrupt()` 仍只计数不调度，长任务独占 CPU（目前单测试串行尚可，多测试时有风险）。
3. **管道（Step 14）**：脚本 `a | b` 依赖 `pipe2`，当前是桩（环形缓冲未实现），unixbench 等跑不动。

### 评测覆盖与功能依赖全景（2026-06-12 实测 24 个 testcode.sh 后总结）

**判分机制**（`autotest/.../run.py`+`verdict.py`）：解析串口输出——每组靠 `#### OS COMP TEST GROUP START/END <name> ####` 定界，组内靠 `testcase <名> success` 或具体数值（lmbench 解析 latency/bandwidth）判 pass。**程序必须真跑完并打印成功标记/正确数值才算过；崩溃 / stub 返回错误 / 缺 syscall = fail。** 故"能跑"远不够，要"语义正确且产出"。

24 组 = 下表 12 测试 × `/musl`+`/glibc` 两套。**实测脚本内容后**，各测试真实依赖与路线图覆盖对照：

| 测试 | 真实依赖（从脚本确认） | 覆盖 | 关联 Step |
| --- | --- | --- | --- |
| basic / lua | busybox + 文件读 | ✅ 基本就绪 | — |
| busybox | 管道 `cat\|while read`、`eval`、几十个 busybox 子命令 | 🟡 | 14 |
| libctest | `run-static.sh` + **`run-dynamic.sh`** | 🔴 需动态链接 | 13 |
| libcbench | `clone(CLONE_VM)` 线程 + socket（**当前阻塞点**） | 🟡 | 16 |
| lmbench | `lat_sig`(信号)、`lat_pipe`、`lat_select`(select)、fork/exec、fs 写 `/var/tmp`、mmap | 🔴 信号/select 无 step | 14/15/16 |
| unixbench | dhry2(可能动态)、pipe、`fstime`(fs 写)、spawn、execl | 🟡 | 13/14/15 |
| iozone | `-t4` **线程** + fs 读写 | 🟡 | 15/16 |
| cyclictest | `-t8` 线程 + `-p99`(RT 调度) + `-a`(CPU 亲和) + 后台 hackbench + sleep | 🔴 sched 未列 | 16 |
| iperf / netperf | **真实 loopback socket 栈** + fork + 后台进程 + 信号 | 🔴 stub 过不了 | 16 |
| ltp | 跑 `testcases/bin/*` **几百个用例**，逐个测 syscall 语义 | 🔴 长尾 wildcard | 16+21 |
| /glibc 全 12 组 | glibc 程序天然**动态链接** | 🔴 需动态链接 | 13 |

**结论：现有 Step 计划是必要脚手架，但不是充分条件。** 路线图里**未显式列出但评测必需**的硬依赖：(1) 信号栈（lmbench `lat_sig` + 作业控制）——已并入 Step 16；(2) **futex**（pthread 线程 mutex 必需，clone CLONE_VM 的隐形前置）——并入 Step 16；(3) **动态链接 Step 13 从"可延后"提级为必需**（libctest 动态组 + 整个 /glibc/）；(4) 调度优先级/亲和 `sched_setscheduler/sched_setaffinity`（cyclictest）；(5) 真实 loopback socket（iperf/netperf，非 errno-stub）；(6) `select/poll`（lmbench）。

**两层 inherent 不确定性**：① **打地鼠**——当前 initcode 在第 1 个测试（libc-bench）就崩，其余 23 组触发哪些 UNKNOWN syscall 尚未观测到，每让一个新测试跑起来就冒新缺口；② **正确性深度**——LTP/unixbench 测边界语义，"已实现"≠"语义对"，LTP 几百用例是长尾大头。**现实预期**：做完 14/15/16（含上面 6 个缺口）+ 13，可拿"像样部分分"（basic/lua/busybox 子集/简单 fs）；**24/24 全过**还需完整信号栈+futex+动态链接(glibc)+真实网络+RT 调度+LTP 长尾打磨，是持续迭代过程（即 Step 21 的真实工作量，现写得过简）。

### 旁注

- 近期一次全量跑显示 16/16 测试 `initcode: exec fail!`，与本线单进程 exec 已通的结论**有出入**——疑为那次跑用了 `disk.img`（SeaFS）而非 `sdcard-la.img`，或镜像未挂载。回归前先确认启动命令挂的是 `sdcard-la.img`。
- ⚠️ 容器 `nostalgic_khayyam` 仓库挂载点是 **`/workspace`**（实测，§0/§3 命令已据此修正），非 `/coursegrader/submit`。

---

## 七、后续任务路线图（Step 10–21）

### 推荐执行顺序

```
✅ Step 10 (修EXT4 bug) → ✅ Step 11 (脚本执行) → ⬜ Step 12 (定时器抢占)
  → ⬜ Step 13 (动态链接) → ⬜ Step 14 (管道) → ⬜ Step 15 (文件写入)
  → 🟡 Step 16 (clone+futex ✅，socket/信号/select/sched 待做)
  → ✅ Step 17 (栈增长+不挂死) → ⬜ Step 18 (堆增强)
  → ✅ Step 19 (资源回收) → ⬜ Step 20 (块缓存) → ⬜ Step 21 (集成验证)
```
> **Step 10、11、16a（clone+futex）、17、19 已完成并实测通过**（详见 §6 + `la-current.md`）。**当前最短解阻塞路径 = Step 14（管道）**——clone+futex 解除 0x137c 崩溃后，脚本中的 `|` 是阻塞 busybox 测试的下一瓶颈；busybox 作为所有测试脚本的解释器，管道是其最高杠杆的下一步。其次是 socket 族（Step 16 剩余项）和定时器抢占（Step 12）。

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

#### Step 13：动态链接器（🔴 P0，**非可延后**——覆盖半数测试组）
- 扫 PT_INTERP/PT_PHDR，加载 musl dynamic linker，auxv 输出 AT_PHDR/AT_PHNUM/AT_ENTRY/AT_BASE，入口改 interp entry。蓝本：RV 线 DECISIONS D4。涉及 `exec_la.c`。
- **为何提级**：`libctest_testcode.sh` 显式跑 `run-dynamic.sh`；unixbench 的 dhry2 动态链接；**整个 `/glibc/` 12 组** glibc 程序天然动态链接。不实现 = 直接放弃 libctest 动态组 + 全部 /glibc/（24 组里约 12 组受影响）。原先标"可延后"是低估，实测脚本内容后纠正。

#### Step 14：管道实现（🔴 P0）
- `la_pipe`（4KB 环形缓冲 + 读/写指针 + 端开闭标志 + sleep/wakeup）；`SYS_pipe/pipe2`、read/write/close 对 pipe fd、fork 继承。
- 脚本大量用管道（`a | b`），无管道 unixbench 跑不动。涉及 `syscall.c proc.h`。

#### Step 15：文件系统写入（🔴 P0）
- 推荐 **memfs**（内存小 FS，16MB，挂可写路径）：`creat/unlink/mkdir/rmdir`、fd write 落 memfs，不动只读 ext4。
- 备选：ext4 写入（块/inode/目录项分配，复杂）。涉及 `fs_la.c syscall.c early_boot.h proc.h`。

#### Step 16：补全关键 syscall（🔴 P0——**范围比标题大**，当前在线程部分已完成，socket/信号/select 待做）
对照 `docs/SYSCALL_STATUS.md`，按 `syscall: UNKNOWN #N` 日志逐个补。

1. **【✅ 已完成，2026-06-12】`clone(CLONE_VM|CLONE_THREAD)` 真线程 + `futex`(98)**：libc-bench / iozone(`-t4`) / cyclictest(`-t8`) 依赖。实现共享 pgtbl + mm 游标的 CLONE_VM 线程、channel-keyed futex WAIT/WAKE、thread-exit cleartid + futex_wake、exit_group 组终止、shared_vm 过滤 wait4、brk/mmap mm 共享重构、修复 tf 泄漏。详见 `la-current.md` §Step 16a。
2. **信号栈**：`rt_sigaction/rt_sigprocmask`（当前桩）+ **`rt_sigreturn`/`kill/tgkill` + 真实信号投递**。lmbench `lat_sig`(install/catch/prot)、netperf/cyclictest 后台进程 `&` 的作业控制（SIGCHLD）都依赖。当前无专门 step，并入此处。
3. **socket 族真实实现**（当前返回 ENOSYS，libc-bench 已优雅降级）：`socket(0x29)/bind/listen/accept/connect/sendto/recvfrom` + loopback。iperf/netperf 要能在 `127.0.0.1` 跑 TCP，stub 直接 fail。libc-bench 的 `#0x42/#0x71/#0xa9` 也属此。
4. **`select/pselect6/poll`**：lmbench `lat_select`。
5. **`sched_setscheduler/sched_setaffinity/getcpu`**：cyclictest `-p99`(SCHED_FIFO)/`-a`(CPU 亲和)。
6. 时间/信息类：`gettimeofday(78)/clock_gettime(113)/times(100)`、`uname/fcntl/ioctl` 补全。
- 涉及：`syscall.c proc.c/proc.h trap.c`（信号投递+线程调度）。

#### Step 17：栈自动增长 + 用户态异常不挂死（🟡 P1）— ✅ 已完成
- `la_uvm_grow_stack`（uvm_la.c）：fault VA 落在 `[LA_USER_STACK-512*PGSIZE, stack_bottom)` 时映射 `[fault_page, stack_bottom)` 全部缺失页，更新 `stack_bottom`。trap.c ISTLBR 失败先试扩栈+重填（成功 return，保持 ISTLBR 给 ertn），失败且为用户进程则 `la_proc_exit(-11)`；通用异常同样 kill 用户进程。
- `la_proc_exit`（proc.c）：清 ISTLBR（**最关键**，防 swtch 走后下一个 trap 被误判为 TLB 重填）+ ZOMBIE + 唤醒父 + `la_sched_switch`。`sys_exit`/`exit_group` 复用之；fork 复制 `stack_bottom`；exec 设 `stack_bottom = stack_top - 8*PGSIZE + PGSIZE`（=最低映射页，**勿 off-by-one**，否则预映射区下方留永久空洞）。
- 涉及 `proc.h early_boot.h proc.c syscall.c trap.c uvm_la.c exec_la.c`。验证：libcbench-musl 完整跑通、0 崩溃 0 挂死，initcode 继续进 `/glibc/` 组（`/tmp/la-step17c.log`）。

#### Step 18：堆/mmap 增强（🟢 P2）
- mmap 支持 `MAP_ANONYMOUS/MAP_FIXED`；fork 正确复制/共享 mmap。涉及 `syscall.c uvm_la.c`。

#### Step 19：资源回收与稳定性（🟡 P1）— ✅ 已完成
- 实现：`la_uvm_free_pgtbl`（`uvm_la.c`，整表释放数据页+表页）；`la_proc_free`（`proc.c` 改 public，释放 pgtbl+kstack）；`sys_wait` 回收改 `la_proc_free(child)`；exec 在 argv 拷贝完成+装新表+TLB 清填后释放 `old_pgtbl`。安全前提=fork 深拷贝（无共享页）。验证：`fork fail!` 13→0、构建 0 warning、无 use-after-free。（`LA_NPROC` 暂未上调，留待集成期。）涉及 `uvm_la.c early_boot.h proc.c proc.h syscall.c exec_la.c`。

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
| 13 | 动态链接器 | 🔴 P0（**提级**：libctest 动态组+整个 /glibc/ 需要） | 大 | Step 11 | ⬜ 待做 |
| 14 | 管道实现 | 🔴 P0 | 中 | Step 11 | ⬜ 待做 |
| 15 | 文件系统写入 | 🔴 P0 | 大 | Step 10 | ⬜ 待做 |
| 16 | 补全关键 syscall | 🔴 P0 | **大**（clone+futex✅, socket+信号+select+sched待做） | Step 14 | 🟡 部分完成（**clone+futex 已完成**） |
| 17 | 栈增长+不挂死 | 🟡 P1 | 中 | 无 | ✅ 已完成 |
| 18 | 堆/mmap 增强 | 🟢 P2 | 中 | 无 | ⬜ 待做 |
| 19 | 资源回收 | 🔴 P0 | 中 | 无 | ✅ 已完成 |
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
