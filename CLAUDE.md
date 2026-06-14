# CLAUDE.md — SeaOS / LoongArch B 线（oscomp 2026）项目指引

> 本文件由 Claude Code / agent 每次会话自动加载。它是 **LoongArch（B 线）** 的工作记忆：项目整体情况、构建运行环境、**oscomp 评测环境与方法**、LoongArch 关键技术约定、当前状态与阻塞点、后续任务路线图、协作硬约束。
> 规则/约定（全项目通用，偏 RISC-V）另见 `AGENTS.md`；syscall 号权威表见 `docs/SYSCALL_STATUS.md`；设计取舍见 `docs/DECISIONS.md`；详细开发日志见 `la-current.md`（LA）与 `rv-current.md`（RV）。

---

## 0. TL;DR（先读这段）

- **项目**：SeaOS — 华东师大花狮小队，oscomp 2026「OS 内核实现赛道」初赛。xv6 风格内核，目标跑通 `/musl`、`/glibc` 下的 oscomp 测试脚本。
- **两条线**：**A 线 = RISC-V**（`src/kernel/`，成熟，101+ syscall、动态链接、pipe、memfs 齐备，是事实参考实现）；**B 线 = LoongArch**（`src/kernel/loongarch/`，Step 10–21 全部完成，**本文档对象**）。LoongArch 与 RISC-V 共用同一套 Linux「通用 ABI」syscall 号表。
- **当前状态（2026-06-14）**：**Phase 1–5 全部实施完毕**（P1 memfs/glibc/busybox → P2 loopback TCP/UDP → P3 RT调度+select → P4 mmap文件映射 → P5 LTP syscall补齐）。101 syscall dispatch 入口（95 真实实现 + 3 ENOSYS stub）。新增 `socket_la.c/h` loopback 网络栈。运行时 **0 个 UNKNOWN syscall**，构建 0 错误 0 警告。libcbench-musl 6/6 exit=0。⚠️ 已知问题：libcbench 退出阶段 ADEF 嵌套异常（QEMU invtlb 缺陷），阻止 GROUP END 打印但不影响子测试结果（均 exit=0）。详见 §6。
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
| `fs_la.c` | **文件系统**：自动识别 SeaFS（`disk.img`，magic 0x12341234）与 EXT4（`sdcard-la.img`，magic 0xEF53@+0x438）。路径解析/读文件/目录枚举，通过 `bio_read` 走缓冲区缓存 |
| `memfs_la.c` / `memfs_la.h` | **可写内存文件系统**：128 inodes，每文件最多 2048 数据页（8MB），接管 O_CREAT/O_TRUNC/write/mkdir/unlinkat，cwd 相对路径解析 |
| `bio_la.c` / `bio_la.h` | **缓冲区缓存**：256 块 × 4KB = 1MB LRU 缓存，轮转时钟淘汰，脏块回写 |
| `socket_la.c` / `socket_la.h` | **Loopback 网络栈**：AF_INET TCP/UDP，socket/bind/listen/accept/connect/send/recv，64KB 环接收缓冲，UDP 数据报队列 |
| `proc.c` / `proc.h` | PCB、调度器（优先级+轮转+抢占）、内核线程/用户进程创建、sleep/wakeup、`swtch` 上下文切换、信号投递 |
| `swtch.S` / `userret.S` / `userret.c` | 上下文切换、用户态返回（DA=0/PG=1 → `ertn`） |
| `trap.c` / `trap_entry.S` / `trap_layout.h` / `trap.h` | 异常向量、trapframe、`trap_dispatch`（syscall/timer/TLB/页错误分发）、定时器抢占 |
| `timer.c` | 稳定时钟中断（100MHz/100Hz）+ tick 计数 + heartbeat |
| `syscall.c` | **syscall 实现 + 分发表**（101 dispatch 入口，95 真实实现）：进程/文件/管道/信号/线程/内存/时间/系统/网络/调度 |
| `exec_la.c` | ELF 加载、PT_INTERP 动态链接器加载（`la_load_interp`）、shebang 脚本解释、auxv 构造 |
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
- 容器：`nostalgic_khayyam`（镜像 `zhouzhouyi/os-contest:20260510`，含 LoongArch 工具链 + QEMU 10.0.2）。项目挂载点 `/workspace`（即宿主仓库根；官方评测容器路径为 `/coursegrader/submit`）。

### 构建命令

```bash
# 1. 确保 docker 容器在跑
docker start nostalgic_khayyam   # 若已停

# 2. 容器内构建（产物 target/loongarch/kernel-la.elf）
docker exec nostalgic_khayyam bash -lc 'cd /workspace && make build-la'
# 或一步到位：build + 复制到根 kernel-la / kernel-rv（评测入口）
docker exec nostalgic_khayyam bash -lc 'cd /workspace && make all'
```

- 工件：`target/loongarch/kernel-la.elf` → `make all` 复制为根 `kernel-la`（**评测机消费的就是根 `kernel-la`**）。
- `make clean` 清 `target/`。CFLAGS 含 `-Wall -Werror`：写桩函数时**禁止留未使用变量/参数**，否则编译失败。

### 运行命令（注意镜像陷阱）

- ⚠️ **`make run-la` 默认挂 `target/mkfs/disk.img`（SeaFS，magic 0x12341234），不是真实测试镜像**。跑真实 oscomp 测试必须改挂 `sdcard-la.img`（ext4）：

```bash
docker exec nostalgic_khayyam bash -lc 'cd /workspace && \
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
  -v "<本机仓库>:/workspace" \
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

### 当前已实现 syscall（LA 分发表，`syscall.c`，~60 个）

进程：`fork wait waitid exit exit_group getpid gettid getppid getcwd exec clone`
文件 I/O：`open close read write lseek dup dup3 fstat get_dentries ioctl newfstatat faccessat readlinkat fcntl writev statx`
目录：`chdir mkdir unlinkat`
内存：`brk mmap(MAP_FIXED+ANONYMOUS) munmap mprotect msync`
信号：`rt_sigaction rt_sigprocmask rt_sigreturn kill tgkill`
身份：`getuid geteuid getgid getegid`（均返回 0=root）
线程：`clone(CLONE_VM) futex set_tid_address set_robust_list`
管道：`pipe2`（4KB 环形缓冲，阻塞读/写，fork 继承）
时间：`clock_gettime gettimeofday times`
系统：`uname sched_yield prlimit64 getcpu shutdown`
网络（存根）：`socket bind listen accept connect sendto recvfrom pselect6 ppoll`（均返回 ENOSYS）
调度（存根）：`sched_setaffinity sched_getaffinity sched_setscheduler`

> 号表/语义对照 `docs/SYSCALL_STATUS.md`。

### 动态链接状态

- **✅ PT_INTERP 已实现**（Step 13）：扫描 PT_INTERP 读解释器路径（如 `/lib64/ld-musl-loongarch-lp64d.so.1`），`la_load_interp()` 加载到固定基址 `0x40000000`，多级 fallback 找解释器文件（→ `/musl/lib/libc.so` → `/glibc/lib/ld-linux-...`），auxv 输出 AT_BASE/AT_PHENT/AT_PHDR，入口重定向至解释器。
- musl 动态二进制（如 `/musl/entry-dynamic.exe`、dhry2）和 glibc 全组（`/glibc/lib/ld-linux-loongarch-lp64d.so.1` 已在 fallback 链中）均可加载。
- 静态 ELF 不受影响。

---

## 六、当前状态（2026-06-14）

### 已完成（Phase 1–5，全部实施）

**P1 memfs/glibc/busybox + P2 loopback TCP/UDP + P3 RT调度+select + P4 mmap文件映射 + P5 LTP syscall 补齐，全部编码完成。** 详见 §7.8 历史记录表。

### 实测通过

- `make build-la`：**0 错误 0 警告**（source 模式）
- `make check-la`：8s 冒烟通过（boot → kernel ready → timer heartbeat）
- `sdcard-la.img` 10min 测试：libcbench-musl 全部 6 个子测试 `exit=0`，**0 次崩溃，0 UNKNOWN syscall**
- Syscall dispatch 入口 **101**（95 真实实现 + 3 ENOSYS stub 残留：sendmsg/recvmsg/sendfile）
- 新增文件：`socket_la.c`（390行）+ `socket_la.h`（114行），loopback TCP/UDP 完整实现
- 所有核心子系统通路：进程/文件/管道/信号/线程/内存/定时器/动态链接/块缓存/网络/socket

### 已知问题

| 问题 | 影响 | 根因 | 计划 |
|------|------|------|------|
| libcbench 退出阶段 ADEF 嵌套异常 | GROUP END 未打印（子测试 6/6 全部 exit=0） | QEMU 10.0.2 `invtlb` 不可靠 + ISTLBR 级联，ERA 被污染（0x20104c 附近） | 明日继续排查；症状稳定可复现，era 在 `la_exception_entry` 内部 |

### 关键修复（历史记录）

1. **ADEF→INE 级联（过时 TLB 条目）**：QEMU 10.0.2 广播 `invtlb` 不可靠。修复：`la_uvm_free_pgtbl` 中逐 VA `invtlb 0x6` 失效 + `la_tlb_inval_all()` 额外全刷。验证 180s 内 0 崩溃。
2. **busybox 启动崩溃（mallocng）**：TLB 一致性——exec 后全局 TLB 条目残留。修复：调度器每次切换用户地址空间前 `la_tlb_inval_all` + `la_tlb_fill_all`。
3. **物理内存耗尽**：exec/exit 从不释放页表。修复：`la_uvm_free_pgtbl` + `la_proc_free`（Step 19），`fork fail!` 13→0。
4. **memfs cwd 相对路径**：memfs 创建文件使用原始路径，chdir 后相对路径找不到。修复：`la_resolve_memfs_path` 解析 cwd→绝对路径。
5. **调度器优先级**：轮转调度改为优先级感知（SCHED_FIFO 1-99 > SCHED_OTHER 0），同级内轮转。

---

## 七、全部分数路线图（2026-06-14 更新）

> **Phase 1–5 全部编码完成**（101 syscall dispatch 入口，libcbench-musl 6/6 exit=0，0 UNKNOWN syscall）。
> 以下为**当前状态 → 24/24 全过**的进度追踪。

### 7.0 评测全景（更新后）

```
/musl/ 12 组（基础分）         /glibc/ 12 组（加分）
├─ libcbench  ✅ 6/6 exit=0    ├─ libcbench  🟡 基础设施就绪
├─ basic      🟡 基础设施就绪   ├─ basic      🟡 同上
├─ lua        🟡 基础设施就绪   ├─ lua        🟡 同上
├─ busybox    🟡 管道+fcntl就绪 ├─ busybox    🟡 同上
├─ libctest   🟡 动态链接就绪   ├─ libctest   🟡 同上
├─ lmbench    🟡 select+mmap就绪├─ lmbench    🟡 同上
├─ unixbench  🟡 memfs大文件就绪├─ unixbench  🟡 同上
├─ iozone     🟡 TCP+mmap+线程  ├─ iozone    🟡 同上
├─ cyclictest 🟡 RT调度+select  ├─ cyclictest🟡 同上
├─ iperf      🟡 TCP栈就绪     ├─ iperf     🟡 同上
├─ netperf    🟡 TCP栈就绪     ├─ netperf   🟡 同上
└─ ltp        🟡 +20 syscall补齐└─ ltp       🟡 同上
```

> 🟡 = 基础设施/代码已就绪，等待实际测试验证。⚠️ 当前阻塞：libcbench 退出阶段 ADEF（QEMU invtlb 缺陷），阻止 GROUP END 打印进而阻碍后续测试组启动。

---

### 7.1 第一阶段 ✅ 已完成（memfs + glibc 存根 + busybox fcntl）

| 子任务 | 内容 | 状态 |
|--------|------|:----:|
| P1.1 | memfs: 2048页/文件(8MB), O_TRUNC, cwd 相对路径解析 | ✅ |
| P1.2 | glibc: prctl/getrandom/madvise/rseq/mlock 存根, syscall trace 关闭 | ✅ |
| P1.3 | busybox: fcntl F_DUPFD/F_GETFL/F_SETFL 完整实现 | ✅ |

### 7.2 第二阶段 ✅ 已完成（loopback TCP/UDP）

| 子任务 | 内容 | 状态 |
|--------|------|:----:|
| P2.1 | 新增 socket_la.c/h: socket/bind/listen/accept/connect/send/recv/close | ✅ |
| P2.2 | UDP: sendto/recvfrom, 数据报队列 | ✅ |
| — | syscall.c: 9 socket syscall + getsockopt/setsockopt/shutdown/accept4 | ✅ |

### 7.3 第三阶段 ✅ 已完成（RT调度 + select/poll）

| 子任务 | 内容 | 状态 |
|--------|------|:----:|
| P3.1 | 调度器优先级感知 (SCHED_FIFO 1-99 > SCHED_OTHER 0), sched_* syscall | ✅ |
| P3.2 | pselect6 + ppoll 真实实现 (pipe/socket readiness 检测) | ✅ |

### 7.4 第四阶段 ✅ 已完成（mmap文件映射）

| 子任务 | 内容 | 状态 |
|--------|------|:----:|
| P4.1 | sys_mmap: MAP_PRIVATE 文件内容读取到映射页 (ext4 + memfs) | ✅ |

### 7.5 第五阶段 ✅ 已完成（LTP syscall 补齐）

| 子任务 | 内容 | 状态 |
|--------|------|:----:|
| P5 | +20 syscall: nanosleep, getrlimit, getrusage, sysinfo, statfs, fstatfs, fsync, umask, readv, ftruncate, getpgid, sched_getparam/setparam 等 | ✅ |

### 7.6 当前阻塞 & 下一步

| 优先级 | 任务 | 预计耗时 |
|:------:|------|:------:|
| 🔴 P0 | **修复 ADEF 嵌套异常**（libcbench 退出 → GROUP END 阻断链） | 2-4h |
| 🟡 P1 | **更快环境跑长测试**（Linux 原生 QEMU + KVM，速度 10-50×） | 环境搭建 |
| 🟡 P1 | 验证 glibc 组启动（PT_INTERP fallback 链已就绪） | 测试 |
| 🟢 P2 | 验证 netperf/iperf（TCP/UDP socket 新实现） | 测试 |
| 🟢 P2 | 跑通 unixbench/busybox 全量（memfs 大文件 + fcntl 已就绪） | 测试 |

### 7.7 新增/修改文件清单

| 文件 | 变更类型 | 关键内容 |
|------|:------:|------|
| `syscall.c` | 修改 | 101 dispatch 入口, memfs 路径解析, socket/mmap/select/fcntl 实现 |
| `socket_la.c` | **新增** | loopback TCP/UDP (390行) |
| `socket_la.h` | **新增** | socket 结构定义 (114行) |
| `memfs_la.c` | 修改 | 2048页/文件, memfs_get_path, memfs_truncate |
| `memfs_la.h` | 修改 | MEMFS_PAGES_PER_FILE 16→2048, 新 API |
| `proc.c` | 修改 | 优先级调度器, TLB 防御性冲刷 |
| `proc.h` | 修改 | LA_FD_SOCKET, sched_priority, sock_idx |
| `early_boot.h` | 修改 | socket + memfs 原型 |
| `boot.c` | 修改 | la_socket_init 调用 |
| `uvm_la.c` | 修改 | la_uvm_free_pgtbl 尾部全 TLB 冲刷 |
| `trap.c` | 修改 | ADEF 诊断增强 (ERA_PTE dump) |

### 7.8 已完成的 Phase 历史记录

| Phase | 内容 | 状态 |
| --- | --- | :---: |
| P1.1 | memfs 增强 (8MB文件, O_TRUNC, cwd路径) | ✅ |
| P1.2 | glibc syscall 存根 (prctl/getrandom/madvise等) | ✅ |
| P1.3 | busybox fcntl 补齐 (F_DUPFD等) | ✅ |
| P2.1 | loopback TCP 栈 (socket_la.c/h 新增) | ✅ |
| P2.2 | UDP 支持 (sendto/recvfrom) | ✅ |
| P3.1 | RT 调度优先级 | ✅ |
| P3.2 | select/poll 实现 | ✅ |
| P4.1 | mmap 文件映射 (MAP_PRIVATE) | ✅ |
| P5 | LTP syscall 补齐 (+20 syscall) | ✅ |

---

## 八、协作约定与硬约束

- **编码**：所有源码/头/脚本/文档 UTF-8；修改时**保留原有中文注释**，不得改成乱码/问号/U+FFFD/GBK；新增注释优先英文（除非用户要中文）。Windows/PowerShell 下操作中文内容前先确认编码。
- **不破坏 RV**：LA 改动不得让 `kernel-rv` 构建/运行回退；改公共文件须在提交说明标风险。
- **不碰镜像/评测**：禁止改 `data/sdcard-*.img.gz`、评测脚本、测试用例；禁止虚假过测（吞日志/伪装成功/硬编码输出/跳过用户程序）。
- **单调改进**：新补丁须保持已通过的 unixbench-musl、busybox-musl 不回退。
- **最小兼容要可解释**：允许桩实现，但返回值/errno/日志/DECISIONS 必须说清边界；无法用最小实现保正确性时转完整实现并记 DECISIONS。
- **提交**：仅在用户要求时 commit/push；提交信息结尾加 `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。默认分支 `master`，当前工作分支 `os2026-1`——若在默认分支须先开分支。
- **每次 Step 完成必须更新两份文档**：① `CLAUDE.md`（Agent 工作记忆：更新 §0 当前状态 + §6 阻塞点 + §7 路线图/优先级表）；② `la-current.md`（人类开发日志：在第三部分按时间追加条目，记录做了什么、涉及文件、遗留问题；更新第二部分表格 + 末尾最后更新时间）。不得跳过。
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
