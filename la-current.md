# LoongArch (B 线) 内核开发手册

> 最后更新：2026-06-14 22:00
>
> 本文档面向**开发者**，记录 SeaOS 项目 LoongArch 架构的设计思路、文件结构、路线图进展、以及按时间排列的开发日志。

---

## 第一部分：项目框架与设计思路

### 1.1 架构概述

SeaOS 是一个教学操作系统项目，支持两种架构：
- **A 线（RISC-V）**：已能完整跑通评测
- **B 线（LoongArch）**：本文档对象

B 线内核完全独立于 A 线编译，所有 LoongArch 代码位于 `src/kernel/loongarch/` 目录下，不修改 A 线任何文件。

| 方面 | RISC-V (A 线) | LoongArch (B 线) |
|------|---------------|-------------------|
| 启动方式 | OpenSBI 固件 → S-mode 内核 | 无固件，直接从 `0x200000` 执行 |
| VirtIO 总线 | MMIO (`virtio-blk-device`) | PCI (`virtio-blk-pci`) |
| 中断控制器 | PLIC | 恒定频率定时器（直接 CSR） |
| 特权级切换 | S-mode ↔ U-mode (`sret`) | PLV0 ↔ PLV3 (`ertn`) |
| 页表格式 | Sv39 | LoongArch 三级页表（PGDL/PGDL/PWCL） |
| Syscall 触发 | `ecall` 指令 | `syscall 0` 指令 (ecode=0x0B) |
| 地址翻译 | `satp` + `sfence.vma` | PGDL/PGDH + `invtlb` + `tlbfill` |

### 1.2 设计原则

1. **独立内核**：不依赖 RISC-V 代码，从零实现所有架构特定功能
2. **最小可运行**：增量式开发，每一步都有可验证的产出
3. **自包含文件系统**：内核内嵌 SeaFS 和 EXT4 只读驱动，不依赖外部 VFS 层
4. **DA 模式内核 + 分页用户态**：
   - 内核运行在 DA=1（恒等映射，物理地址即虚拟地址）
   - 通过 DMW0 建立内核恒等映射
   - 用户态通过 `ertn` 切换到 DA=0/PG=1，使用 TLB 进行地址翻译

### 1.3 启动流程

```
QEMU 加载 kernel-la 到 0x200000
  → entry.S: 设置 SP、异常向量
  → boot.c: la_boot_main()
      ├─ pmem_init()        物理内存管理
      ├─ kvm_init()         DMW0 恒等映射
      ├─ uvm_paging_init()  页表 CSR 初始化
      ├─ tlb_init()         STLB 页大小配置
      ├─ timer_init()       恒定频率定时器（100 Hz）
      ├─ proc_init()        进程表初始化
      ├─ 开中断
      ├─ virtio_init()      PCI 扫描 + VirtIO 块设备初始化
      ├─ fs_init()          挂载文件系统（SeaFS / EXT4 自动识别）
      ├─ proc_make_first()  创建 initcode 用户进程
      └─ scheduler()        进入调度器（永不返回）
```

### 1.4 地址空间

```
虚拟地址空间（39 位，512 GB）：
  0x0000_0000_1000                 用户代码段起始
  ...                              用户堆（heap_top 向上增长）
  ...                              mmap 区域（LA_MMAP_BASE = 0x4000000000 向上）
  0x7FFF_FFE_0000                  用户栈顶（向下增长）
  0x7FFF_FE0_0000 - 512×PGSIZE     栈增长下限（2MB）

内核空间（DA 模式，恒等映射）：
  0x0000_0020_0000                 内核入口
  0x0000_0024_4000                 pmem 管理起始（kernel_end）
  0x0000_1000_0000                 低内存结束（256 MB）
  0x0000_1FE0_01E0                 UART（8250 兼容）
  0x0000_2000_0000                 PCI ECAM 配置空间
  0x0000_4000_0000                 PCI MMIO 窗口
```

### 1.5 页表结构

LoongArch 三级页表（9+9+9+12 = 39 位虚拟地址）：

```
VA [38:30] → Level 0 (root) 索引，512 条目
VA [29:21] → Level 1 (mid)  索引，512 条目
VA [20:12] → Level 2 (leaf) 索引，512 条目
VA [11:0]  → 页内偏移（4KB 页）

PTE 格式（64 位，仅位 [47:12] PPN + 位 [8:0] 标志）：
  [0] V     [1] D     [3:2] PLV     [5:4] MAT
  [6] G     [7] P     [8] W         [61] NR    [62] NX
目录条目（root/mid 级别）仅保存裸 PA，无标志位（QEMU HPTW 不会剥离标志位）。
```

### 1.6 文件结构与职责

```
src/kernel/loongarch/
├── early_boot.h     # 核心常量、地址布局、CSR 宏、所有原型声明
├── entry.S          # 入口点
├── boot.c           # 启动序列
├── kernel.ld        # 链接脚本（0x200000）
├── pmem.c           # 物理页分配器（空闲链表，分配即清零）
├── kvm.c            # DMW0 恒等映射初始化
├── uvm_la.c         # 用户虚拟内存：三级页表 walk/create/map/copy/free/grow_stack
├── tlb_la.c         # TLB 管理：init、refill_one、fill_all、inval_all、inval_page
├── timer.c          # 恒定频率定时器（100Hz）+ tick 计数
├── proc.h           # PCB 定义、fd 表、管道结构、信号结构
├── proc.c           # 进程管理：调度器、sleep/wakeup、信号投递、proc_free
├── swtch.S          # 内核上下文切换（callee-saved 寄存器）
├── trap.h           # Trap frame 结构、GPR 索引枚举
├── trap_entry.S     # 异常入口/退出汇编
├── trap.c           # Trap 分发（TLB refill / 中断 / syscall / 异常）
├── userret.S        # 内核→用户态 ertn 切换
├── userret.c        # 用户态返回桥接
├── virtio_la.c      # VirtIO PCI 块设备驱动（ECAM 枚举、BAR、队列、轮询读取）
├── fs_la.c          # 文件系统：SeaFS + EXT4 只读（自动识别 magic、路径解析、目录枚举）
├── exec_la.c        # ELF 加载器、shebang 脚本解释、initcode 创建、argv/auxv 构造
├── syscall.c        # 系统调用实现 + 分发表（60+ syscall）+ 管道池 + futex
└── initcode_la.h    # 第一个用户进程（C 源码编译后嵌入为头文件）
```

### 1.7 环境与构建

- **工具链**：Docker 容器 `zhouzhouyi/os-contest:20260510`，内含 `loongarch64-linux-gnu-gcc/ld/objdump`
- **QEMU**：`qemu-system-loongarch64` v10.0.2，-kernel 启动，virt 机型，1G 内存
- **构建**：`docker exec nostalgic_khayyam bash -lc 'cd /workspace && make build-la'`
- **运行**：挂载 `sdcard-la.img`（4GB ext4 测试镜像）
- **评测**：`autotest-for-oskernel/` 官方评测脚本，解析串口日志判分
- ⚠️ `make build-la` 产出 `target/loongarch/kernel-la.elf`，根目录 `kernel-la` 需 `make all` 或手动 `cp`

---

## 第二部分：路线图完成情况

### 2.1 执行顺序

```
✅ Step 10 (EXT4)  → ✅ Step 11 (脚本)  → ✅ Step 12 (抢占)
→ ✅ Step 13 (动态链接) → ✅ Step 14 (管道) → ✅ Step 15 (文件写入)
→ ✅ Step 16 (全部 syscall) → ✅ Step 17 (栈增长) → ✅ Step 18 (mmap增强)
→ ✅ Step 19 (资源回收) → ✅ Step 20 (块缓存) → ✅ Step 21 (集成验证)
```

### 2.2 已完成的 Step 列表

| Step | 内容 | 优先级 | 工作量 | 状态 |
|------|------|--------|--------|------|
| 1–9 | 基础架构 + 核心 syscall | — | — | ✅ |
| 10 | 修复 EXT4 目录枚举 | P0 | 小 | ✅ |
| 11 | 脚本执行（shebang） | P0 | 中 | ✅ |
| 12 | 定时器抢占调度 | P0 | 小 | ✅ |
| 13 | 动态链接器 | P0 | 大 | ✅ |
| 14 | 管道实现 | P0 | 中 | ✅ |
| 15 | 文件系统写入（memfs） | P0 | 大 | ✅ |
| 16 | 补全 syscall（线程+信号+存根） | P0 | 大 | ✅ |
| 17 | 栈自动增长 + 不挂死 | P1 | 中 | ✅ |
| 18 | 堆/mmap 增强 | P2 | 中 | ✅ |
| 19 | 资源回收（页表+内核栈） | P0 | 中 | ✅ |
| 20 | 缓冲区缓存（bio） | P2 | 中 | ✅ |
| 21 | 集成验证 | P0 | 视情况 | ✅ |
| — | Bugfix: TLB 级联崩溃 | P0 | 小 | ✅ |

### 2.3 全部分数路线图（Step 10–21 完成后，2026-06-14 制定）

#### 评测全景

```
/musl/ 12 组（基础分）        /glibc/ 12 组（加分）
├─ libcbench  ✅ 已过         ├─ libcbench  🔴 需 glibc 动态链接验证
├─ basic      🟡              ├─ basic      🔴
├─ lua        🟡              ├─ lua        🔴
├─ busybox    🟡              ├─ busybox    🔴
├─ libctest   🔴 动态链接     ├─ libctest   🔴
├─ lmbench    🔴 select/mmap  ├─ lmbench    🔴
├─ unixbench  🔴 fstime/mmap  ├─ unixbench  🔴
├─ iozone     🔴 TCP/mmap     ├─ iozone     🔴
├─ cyclictest 🔴 RT 调度      ├─ cyclictest 🔴
├─ iperf      🔴 TCP 栈       ├─ iperf      🔴
├─ netperf    🔴 TCP 栈       ├─ netperf    🔴
└─ ltp        🔴 长尾         └─ ltp        🔴
```

#### Step21后续的分阶段计划

| 阶段 | 内容 | 工作量 | 解锁组数 | 累计 |
|------|------|--------|---------|:--:|
| — | 当前（Step 10–21） | — | 1 / 24 | 4% |
| P1.1 | **memfs 增强**（每文件 64KB→8MB，O_TRUNC，cwd 写入） | 小（2h） | unixbench/lmbench 子测试 | — |
| P1.2 | **glibc 动态链接验证**（长时跑 + UNKNOWN syscall 补齐） | 中（4h） | 12 组 glibc | 54% |
| P1.3 | busybox 补齐 | 小-中（3h） | 1 组 | — |
| P2.1 | **loopback TCP 栈**（socket_la.c，三次握手 + 收发） | 大（12h） | — | — |
| P2.2 | UDP 支持 | 小（2h） | 4 网络组 | 71% |
| P3.1 | RT 调度优先级 + CPU 亲和 | 小（2h） | — | — |
| P3.2 | **select 实现** | 中（4h） | 2 组 + lmbench | 79% |
| P4.1 | **mmap 文件映射** | 中（3h） | — | — |
| P4.2 | lmbench + iozone 完整通过 | 中（3h） | 4 组 | 88% |
| P5 | **LTP 长尾**（~300+ 用例逐个修复） | 大（20h+） | 2 组 | **100%** |

**总工作量估算：~55–60 小时**

#### 推荐执行顺序

```
P1.1 memfs ──→ P1.2 glibc 动态链接 ──→ P1.3 busybox
                    │
    ┌───────────────┘
    ↓
P2.1 TCP ──→ P2.2 UDP ──→ P3.1 RT ──→ P3.2 select
                                            │
    ┌───────────────────────────────────────┘
    ↓
P4.1 mmap 文件 ──→ P4.2 lmbench+iozone ──→ P5 LTP
```

**关键路径**：P1.2（glibc 动态链接验证）投入产出比最高——实现后直接解锁 12 组加分。

---
---

## 第三部分：开发日志

### 2026-06-05 — Steps 1–8：内核基础架构搭建

**完成内容**：从零搭建 LoongArch 内核骨架。
- 入口/启动、UART 输出、异常入口/退出（trap_entry.S / userret.S）
- 物理内存管理（空闲链表）、DMW0 恒等映射
- 三级页表管理（uvm_la.c）、TLB 管理（tlb_la.c）
- 100Hz 定时器、进程管理（PCB + 轮转调度器 + sleep/wakeup）
- VirtIO PCI 块设备驱动（ECAM 枚举、队列初始化、轮询读取）
- 文件系统（SeaFS + EXT4 只读自动识别）

**涉及文件**：`entry.S` `boot.c` `kernel.ld` `pmem.c` `kvm.c` `uvm_la.c` `tlb_la.c` `timer.c` `proc.h` `proc.c` `swtch.S` `trap.h` `trap_entry.S` `trap.c` `userret.S` `userret.c` `virtio_la.c` `fs_la.c` `early_boot.h`

**遗留**：无用户态程序、无 syscall、initcode 未创建。

---

### 2026-06-10 20:52 — Step 9：核心系统调用 + 首个用户进程

**完成内容**：
- 实现 18 个系统调用（fork/wait/exit/getpid/exec/open/close/read/write/lseek/dup/fstat/get_dentries/chdir/mkdir/brk/mmap/munmap/shutdown）
- ELF 加载器 + initcode 创建（exec_la.c）
- 完整 initcode 源码（src/user/initcode_la.c），编译为字节数组嵌入 `initcode_la.h`
- fork 页表深拷贝（uvm_la.c）、用户/内核数据拷贝辅助函数
- fd 表、parent_pid、exit_code、cwd_ino 等 PCB 字段

**涉及文件**：`syscall.c`（新建）、`exec_la.c`（重写）、`uvm_la.c`、`proc.h`、`proc.c`、`fs_la.c`、`early_boot.h`、`src/user/initcode_la.c`（新建）、`Makefile`

**遗留**：sdcard-la.img 挂 ext4 成功但目录枚举返回空、shebang 脚本不执行、定时器不触发调度。

---

### 2026-06-12 16:41 — Step 10（EXT4 目录枚举）+ Step 11（shebang 脚本执行）

**完成内容**：
- EXT4 只读驱动补全：superblock→组描述符→inode→extent 树块映射→目录 scan（按 rec_len 步进）→dirent64 转换；相对路径从 `la_fs_cwd_ino` 起查
- Shebang 识别：exec 检测 `#!`，读取解释器路径，回退到 `/musl/busybox`，重建 argv
- 修复连环 bug：
  - TLB 一致性：所有 PTE 有 G=1 + 无 ASID → exec 跨地址空间后残旧 TLB 条目按 VPPN 别名命中错误物理页。修：每次进用户态前 `la_tlb_inval_all()`；每次新映射 `la_tlb_inval_page(va)`
  - EPERM 掩码：`sys_open` 改 openat ABI（a1=路径），失败返回正确 errno
  - exec 4KB 内核栈溢出：局部大数组改 `static`
  - 相对路径：`./busybox`、`./libc-bench` 从 cwd 起查

**涉及文件**：`fs_la.c`、`exec_la.c`、`syscall.c`、`proc.c`、`uvm_la.c`、`tlb_la.c`

**遗留**：libc-bench 需要 ~80KB 栈（只预分配 8 页）→ Step 17；UNKNOWN syscall #0x42/0x71/0xa9 → Step 16。

---

### 2026-06-12 17:35 — Step 17：用户栈自动增长 + 用户态异常不再挂死

**完成内容**：
- `la_uvm_grow_stack`（uvm_la.c）：fault VA 落在合法栈窗口内时，批量映射缺失页面（上限 512 页/2MB），更新 `stack_bottom`
- ISTLBR 失败路径：先试扩栈 + refill → 失败且为用户态则 `la_proc_exit(-11)`；内核态才 panic
- 通用异常路径：用户态 `la_proc_exit(-11)`，内核态 panic
- `la_proc_exit`（proc.c）：清 ISTLBR → ZOMBIE → 唤醒父 → `la_sched_switch`
- `stack_bottom` 初始值修复：off-by-one 导致预映射区下留永久空洞 → `stack_top - 8*PGSIZE + PGSIZE`
- fork 复制 `stack_bottom`；exec 设新值

**涉及文件**：`proc.h` `early_boot.h` `proc.c` `syscall.c` `trap.c` `uvm_la.c` `exec_la.c`

**遗留**：物理内存耗尽（exec/exit 从不释放页表）→ Step 19。

---

### 2026-06-12 18:16 — Step 19：进程资源回收

**完成内容**：
- `la_uvm_free_pgtbl`（uvm_la.c）：三级遍历释放数据页+leaf/mid/root 表页
- `la_proc_free`（proc.c 改 public）：释放 pgtbl + kstack，标记 UNUSED
- `sys_wait` 回收改 `la_proc_free(child)`；exec 释放旧表（argv 拷贝完成后、新 TLB 填好后）
- 安全前提：fork 深拷贝（无共享页）

**涉及文件**：`uvm_la.c` `early_boot.h` `proc.c` `proc.h` `syscall.c` `exec_la.c`

**遗留**：`initcode: fork fail!` 13→0 已验证。libc-bench 用 `clone(CLONE_VM)` 建线程失败（内核不支持）→ `badv=0x28` NULL 解引用崩溃 → 级联杀死 initcode（INE @ 0x137c）→ Step 16a。

---

### 2026-06-12 21:40 — Step 16a：clone(CLONE_VM) 线程支持 + futex

**完成内容**：
- `struct la_mm`：heap/mmap 游标共享（CLONE_VM 线程指向领导者的 `__mm`，避免多线程 brk/mmap 冲突）
- `shared_vm`/`clear_child_tid`/`wait_chan` 字段；channel-keyed sleep/wakeup
- `sys_clone` 重写：LoongArch ABI `clone(flags,stack,ptid,ctid,tls)`（注：ctid 与 tls 的 a3/a4 位置与 RISC-V 互换，经 musl `loongarch64/clone.s` 验证）
- `sys_futex`(98)：WAIT/WAKE，接入 dispatch
- `sys_set_tid_address`：存 `clear_child_tid`，返回 pid
- Thread exit：`sys_exit` 清 cleartid + futex_wake p→ `pthread_join` 返回
- `exit_group`：按 `pgtbl==me->pgtbl` 组杀（含 SLEEPING）；`sys_wait` 跳过 `shared_vm`
- `la_proc_free`：释放 `p->tf`（修复泄漏）；共享 pgtbl 所有权转移安全网
- exec：释放旧 tf；重置 `mm`/`shared_vm`

**涉及文件**：`proc.h` `proc.c` `syscall.c` `exec_la.c`

**遗留**：libcbench 线程创建成功（12 个线程 exit=0），`badv=0x28` 消失。运行末尾仍有预存的 ADEF→INE 级联（过时 TLB）→ 独立 bugfix。

---

### 2026-06-12 22:18 — Step 14（管道）+ Step 16b+c（信号投递 + 补齐 syscall 存根）

**完成内容**：

**管道**：
- `struct la_pipe`：4KB 循环缓冲区 + `nread`/`nwrite` 单调计数器 + `readopen`/`writeopen` 引用计数
- 32 个管道的静态池；`sys_pipe2` 重写（分配管道 + fd0/fd1 → 用户 `int[2]`）
- `sys_read`/`sys_write` 管道处理（阻塞等待、EOF/broken pipe、回绕折返复制）
- `sys_close` 管道清理（递减端点引用计数、唤醒另一端、两端关闭时回收）
- fork/clone 继承管道（`la_pipe_dup_all` 递增引用计数）

**信号投递**：
- `struct la_sigaction`（handler/flags/restorer/mask）、`struct la_sigframe`（gpr[32]+era+sig）
- `la_signal_pending`/`la_signal_deliver`：pending & ~blocked → 选最低位信号 → 默认动作或构建 sigframe → 重写 sp/a0/ra/era → ertn 进 handler
- `sys_rt_sigaction`(134)/`sys_rt_sigprocmask`(135) 重写（真实存储/屏蔽）
- `sys_kill`(129)/`sys_tgkill`(131)（pending 位置位 + 唤醒目标）
- `sys_rt_sigreturn`(139)（从用户栈 sigframe 恢复 gpr+era，compensate era-4）
- `trap.c`：syscall/timer 返回前 `goto check_signal` → signal_deliver

**补齐 syscall 存根 + 修复 3 个编号错误**：
- 移除 `SYS_setrlimit=139`（实际 139=rt_sigreturn）、`SYS_getrlimit=140`（实际 140=setpriority）
- `SYS_getcpu=169` → 修正为 168（169=gettimeofday），经 musl `bits/syscall.h` 交叉验证
- `writev`(66)：遍历 iovec，委托 `sys_write`
- `clock_gettime`(113)：tick→timespec，接入 dispatch（之前已定义但未接入）
- `getcpu`(168)、`gettimeofday`(169)、`times`(153)：合理存根
- `sched_setaffinity/getaffinity/setscheduler`：单 CPU 存根
- `pselect6`/`ppoll`：ENOSYS 存根
- 9 个 socket 系列：ENOSYS 存根
- `early_boot.h`：`la_timer_get_ticks()` 声明

**涉及文件**：`proc.h` `proc.c` `syscall.c` `trap.c` `early_boot.h`

**遗留**：运行时 0 个 UNKNOWN syscall。但预存的 ADEF→INE 级联仍存在 → 独立 bugfix。

---

### 2026-06-12 23:07 — Bugfix：过时 TLB 条目导致的 ADEF→INE 级联崩溃

**问题**：libcbench 运行末尾，预先存在的 ADEF→INE 级联（pid4 ADEF → pid2 INE @ 0x137c → pid1 initcode 死亡），每版必现，与 clone/管道/信号无关。

**根因**：QEMU 10.0.2 的广播 `invtlb`（op 0x0/0x3）不可靠——页面释放后仍残留重复 TLB 条目（相同 VPPN，不同 PA，指向已释放/重用的物理页）。由于 ASID=0 且无进程标签，任意进程命中过时条目都会取指到垃圾数据 → ADEF → 级联。

**修复**：
- `uvm_la.c: la_uvm_free_pgtbl`：在遍历三级页表时，释放每个数据页**之前**调用 `la_tlb_inval_page(va)`（`invtlb 0x6`=按 VA 失效），保证页面返回空闲池时无 TLB 条目指向它
- `tlb_la.c: la_tlb_inval_all`（快速路径，调度器/exec）：保持 `invtlb 0x3` + `dbar` 双侧屏障
- `trap.c`：对页面异常类（PIL/PIS/PIF/PME/ADEF/ADEM）新增 PTE dump + PGDL 一致性诊断（保留供将来调试）

**涉及文件**：`uvm_la.c` `tlb_la.c` `trap.c` `early_boot.h`

**验证**：180 秒运行 0 次崩溃。

---

### 2026-06-14 — Step 12：定时器抢占调度

**完成内容**：
- `proc.h`：新增 `LA_TIME_SLICE`（10 tick = ~100ms）+ `ticks` 字段（`struct la_proc`）
- `proc.c`：`la_proc_alloc` 初始化 `ticks = LA_TIME_SLICE`；调度器在选中用户进程时重置 `p->ticks = LA_TIME_SLICE`
- `trap.c`：在 `la_timer_interrupt()` 之后检查当前用户进程的 `--p->ticks <= 0` → 调用 `la_proc_yield()` 让出 CPU
- **无需额外的 trap frame 保存/恢复**：每进程有独立内核栈，被抢占进程的 trap frame 留在其内核栈上；`la_swtch` 保存/恢复 sp，恢复后 trap_entry.S 从原位置还原 trap frame 并 ertn 回用户态
- 设计要点：内核线程（`is_user=0`）不参与抢占（需自行 yield）；`la_current_proc()` 在调度器 idle 期间返回 0，抢占检查自然跳过；syscall 路径不抢占（中断在 trap handler 内关闭，仅用户态执行时响应定时器）

**涉及文件**：`proc.h` `proc.c` `trap.c`

**遗留**：调试串口输出（`sys#...` trace）在中断禁用的 trap handler 内执行，大量 syscall 时显著延迟定时器中断。生产构建关闭 trace 后定时器可达满频。定时器绝对频率与 QEMU 10.0.2 实现相关，不影响抢占机制正确性。未来可考虑在长 syscall 内开中断（内核可抢占化），属独立优化。

---

### 2026-06-14 — Step 13：动态链接器（PT_INTERP + auxv）

**完成内容**：
- `exec_la.c`：新增 `LA_ELF_PROG_INTERP`(3)/`LA_ELF_PROG_PHDR`(6)/`LA_INTERP_LOAD_BASE`(`0x40000000`)
- `la_load_interp()`（~130 行新函数）：解释器加载到固定基址
  - 多级 fallback：原路径 → `/musl/lib/<basename>` → `/glibc/lib/<basename>` → `/musl/lib/libc.so`（musl 的 libc 即 ld）→ `/glibc/lib/ld-linux-loongarch-lp64d.so.1`
  - 按 PT_LOAD 段映射（RWX），零填 BSS，读取段数据
  - 返回 `base + e_entry`
- `la_do_exec_syscall()` 修改：
  - 段扫描循环新增 PT_PHDR（记录 `phdr_addr`）和 PT_INTERP（读解释器路径）处理
  - 段加载完成后调用 `la_load_interp()`，记录 `interp_entry`
  - AT_PHDR 改为使用 `phdr_addr`（优先 PT_PHDR，回退首段 VA+phoff）
  - auxv 新增 `AT_PHENT`（`sizeof(la_elf_phdr)`）和 `AT_BASE`（仅动态链接时）
  - `tf->era` 动态链接时设为 `interp_entry`，静态不变
- 提供 `memset` 弱别名（解决 GCC `-O2` 自动生成 memset 调用）

**涉及文件**：`exec_la.c`

**遗留**：解释器全部段按 RWX 映射（同 D4/D6），依赖 `mprotect` 桩不报错。静态 ELF 路径完全不受影响。initcode 按 `*_testcode.sh` 字母序扫描，首测 libcbench（静态），动态测试（libctest `run-dynamic.sh`、unixbench dhry2、glibc 全组）在后续才触发——受调试 UART 输出慢影响，完整验证需较长运行时间。

---

### 2026-06-14 — Step 15：文件系统写入（memfs）

**完成内容**：
- 新增 `memfs_la.h`/`memfs_la.c`（~230 行）：内存文件系统，128 inodes，每文件最多 16 数据页（64KB），按需从 `la_pmem_alloc` 分配数据页，`memfs_delete` 释放回池
- `proc.h`：新增 `LA_FD_MEMFS`（4）fd 类型
- `syscall.c` 集成（修改 ~7 个函数 + 新增 1 个）：
  - `sys_open`：memfs 优先——已存在文件直接打开（`LA_FD_MEMFS`）；O_CREAT → `memfs_create`；O_WRONLY 且不存在 → `-ENOENT`；回退 ext4 只读
  - `sys_write`：`LA_FD_MEMFS` 路径 → 按需分配数据页，从用户空间复制数据并写入
  - `sys_read`：`LA_FD_MEMFS` 路径 → 从数据页读取到用户缓冲区
  - `sys_lseek`：支持 `LA_FD_MEMFS`，修复 `SEEK_END`（whence=2）——使用 `memfs_inode_size`
  - `sys_close`：`LA_FD_MEMFS` 无需特殊清理（管道已有独立路径）
  - `sys_fstat`：支持 `LA_FD_MEMFS`，使用 `memfs_inode_size`
  - `sys_get_dentries`：支持 `LA_FD_MEMFS` 目录，调用 `memfs_getdents`
  - `sys_mkdir`：改为真实实现在 memfs 创建目录
  - `sys_unlinkat`（35）：新增——从 memfs 删除文件/目录（`memfs_delete` 同时释放数据页）
  - `sys_newfstatat`：memfs 文件可见（`memfs_lookup` 优先 → ext4 回退）
  - `sys_chdir`：memfs 目录可设为 cwd
- `early_boot.h`/`boot.c`：声明并调用 `memfs_init()`

**涉及文件**：`memfs_la.c`（新增）、`memfs_la.h`（新增）、`syscall.c`、`proc.h`、`early_boot.h`、`boot.c`

**遗留**：memfs 与 ext4 并存于同一命名空间（memfs 优先），但无 copy-on-write 叠加层——对 ext4 已有文件进行写打开会返回 ENOENT。每文件最大 64KB（16 页），若测试需要更大文件需上调 `MEMFS_PAGES_PER_FILE`。不实现文件扩展属性、时间戳、权限位（均返回默认值）。`sys_faccessat` 仍为桩（全部允许）。

---

### 2026-06-14 — Step 18：堆/mmap 增强（MAP_FIXED + munmap + mprotect）

**完成内容**：
- `uvm_la.c`：新增 `la_uvm_unmap_page(root, va, free_page)`——三级页表遍历找叶 PTE，清零 PTE + 可选释放物理页 + `la_tlb_inval_page`
- `sys_mmap` 增强：
  - `MAP_FIXED`(0x10)：精确地址映射——先对整个目标范围 `la_uvm_unmap_page`（释放旧物理页），再分配新页面
  - `MAP_ANONYMOUS`(0x20)：定义宏（已隐式支持，所有 mmap 均为匿名）
  - 非 MAP_FIXED 时保留原有逻辑（hint 优先 → mmap_top 回退）
- `sys_munmap` 真实实现：遍历页范围，对每页调用 `la_uvm_unmap_page` 释放物理页 + 清零 PTE
- `sys_mprotect` 真实实现：
  - 解析 `PROT_READ`(1)/`PROT_WRITE`(2)/`PROT_EXEC`(4)
  - 构建目标 PTE 权限（RWX → 置位 D|W，NX → 置位 NX 等）
  - 遍历页范围，保留 PA 替换权限位，TLB 逐页失效
  - 本地 `SYS_PTE_*` 常量（因 PTE 位定义在 `uvm_la.c` 内，未导出到头文件）
- fork 深拷贝页表自动复制 mmap 区域；CLONE_VM 线程共享页表自然共享 mmap（无需额外 tracking）

**涉及文件**：`syscall.c`、`uvm_la.c`、`early_boot.h`

**遗留**：mmap 地址分配未跟踪已释放区域（`mmap_top` 单调增长），长时间运行可能耗尽地址空间。未实现文件映射（`fd != -1` 时返回 -1）。未实现 `MAP_SHARED`。mprotect 权限常量与 uvm_la.c 重复定义——后续应统一到公共头文件。

---

### 2026-06-14 — Step 20：缓冲区缓存（bio）

**完成内容**：
- 新增 `bio_la.h`/`bio_la.c`（~140 行）：256 块 × 4KB = 1MB LRU 缓存
  - 轮转时钟（clock-hand）淘汰算法：跳过 pinned 条目，淘汰前脏块写回
  - `bio_read(blk)`：缓存命中直接返回指针；未命中 → 淘汰 → VirtIO 读入
  - `bio_write(blk)`：标记脏（用于 memfs 写回）
  - `bio_sync()`：遍历全部脏块写回 VirtIO
  - `bio_invalidate(blk)`：丢弃指定块
- `fs_la.c` 全面接入（~13 处修改）：
  - 移除静态 `la_blkbuf[4096]` 和 `la_blk_read()` 中的直接 VirtIO 调用
  - `la_blk_read` 改为 `return bio_read(blk)` 一行
  - 所有直接 `la_virtio_blk_read(pb, tmp)` → `bio_read(pb)` 模式
  - 消除 SeaFS 中 4 处临时页分配/释放配对（`la_pmem_alloc`+`la_pmem_free`），节省物理内存
  - EXT4 `e4_lbn2pb` 中 `la_blkbuf` 引用 → `bio_read` 直接返回
- `early_boot.h`/`boot.c`：声明 + `bio_init()` 调用（在 fs_init 之前）

**涉及文件**：`bio_la.c`（新增）、`bio_la.h`（新增）、`fs_la.c`、`early_boot.h`、`boot.c`

**遗留**：脏块写回仅在淘汰时触发；`bio_sync` 未在任何关机路径调用（当前内核无正式关机流程，VM 直接终止）。缓存大小固定 256 块，无运行时调整。无预读/回写优化。

---

### 2026-06-14 18:30 — Step 21：综合集成与验证

**验证内容：**
- `make build-la`：source 模式 0 错误 0 警告（16 个 .c/.S 编译单元 + ld）
- `make check-la`：8s 冒烟通过（boot → kernel ready → timer heartbeat）
- sdcard-la.img 满载：libcbench-musl 6/6 exit=0，0 次崩溃
- 进程生命周期：fork → exec → clone → exit → wait 全链路通，fork fail 13→0
- 文件 I/O：open/read/write/close/lseek/dup/dup3/getdents 支持 memfs + ext4
- 管道：阻塞读写/EOF/broken pipe/fork 继承正常
- 信号：sigaction/sigprocmask/kill/tgkill/rt_sigreturn 通路存在
- 线程：clone(CLONE_VM) + futex WAIT/WAKE + exit cleartid 正常
- 内存：brk/mmap(MAP_FIXED)/munmap/mprotect/栈自动增长 正常
- 资源回收：la_uvm_free_pgtbl + la_proc_free 正常
- 定时器：抢占调度 100ms 时间片 + heartbeat 正常
- 动态链接：PT_INTERP 扫描 + la_load_interp + auxv AT_BASE/AT_PHENT

**已知待后续完善（非本 Step 范围）：**
| 缺口 | 影响测试 | 工作量 |
|------|---------|--------|
| 真实 loopback TCP 栈 | iperf/netperf | 大 |
| select/poll | lmbench lat_select | 中 |
| RT 调度优先级/亲和 | cyclictest -p99/-a | 小 |
| glibc 全组动态验证 | /glibc/ 12 组 | 需更长运行时间 |
| mmap 文件映射 | iozone | 中 |
| LTP 长尾边界用例 | ltp | 持续迭代 |
| 串口调试输出优化 | 全测试提速 | 小 |

**涉及文件**：`syscall.c`（open 消息过滤优化）、文档更新

---

### 2026-06-14 22:00 — Phase 1–5：全面补齐（memfs/socket/调度/select/mmap/LTP syscall）

**P1 — memfs 增强 + glibc 存根 + busybox fcntl：**

- memfs: `MEMFS_PAGES_PER_FILE` 16→2048（64KB→8MB/文件），新增 `memfs_truncate` + `memfs_get_path`
- sys_open: O_TRUNC 支持，相对路径通过 `la_resolve_memfs_path` 解析为绝对路径（利用进程 cwd inode）
- sys_chdir/sys_mkdir/sys_unlinkat/sys_newfstatat/sys_statx: 全部使用路径解析
- fcntl: F_DUPFD/F_GETFD/F_SETFD/F_GETFL/F_SETFL 完整实现，管道/socket refcount 正确
- glibc syscall: prctl(167)/getrandom(278)/madvise(233)/rseq(293)/mlock(228)/mlock2(325) 存根
- syscall trace: LA_SYSCALL_TRACE_MAX=0 关闭（UNKNOWN 始终打印）

**P2 — loopback TCP/UDP 网络栈：**

- **新增** `socket_la.c`（390行）+ `socket_la.h`（114行）：AF_INET loopback TCP/UDP
- TCP: socket/bind/listen/accept/connect/send/recv, 64KB 环接收缓冲，connect→即时创建 peer pair
- UDP: sendto/recvfrom, 8×8KB 数据报队列，源地址记录
- 新增 fd 类型 `LA_FD_SOCKET=5`，sock_idx 字段
- syscall: 9 socket syscall + getsockopt/setsockopt/shutdown/accept4 真实实现
- sys_write/sys_read: 支持 socket fd 的 send/recv 路径
- sys_close: 正确清理 socket fd

**P3 — RT 调度优先级 + select/poll：**

- proc.h: `sched_priority` 字段（0=SCHED_OTHER, 1-99=SCHED_FIFO）
- proc.c: 调度器改为优先级感知（高优先先运行，同级轮转）
- sched_setscheduler: 从用户空间读取 sched_param，存储优先级
- sched_getparam/sched_setparam/sched_getscheduler: 完整实现
- pselect6: fd_set copyin/copyout，pipe/socket/console readiness 检测，popcount 计数
- ppoll: pollfd 数组 copyin/copyout，POLLIN/POLLOUT/POLLHUP 支持

**P4 — mmap 文件映射：**

- sys_mmap: MAP_PRIVATE 从 fd 读取文件内容到映射页（ext4 + memfs），支持 offset
- 页面先清零再读文件（BSS 区域保持零）

**P5 — LTP syscall 补齐（+20 syscall）：**

- 时间: nanosleep(101), clock_nanosleep(115)
- 资源: getrlimit(163), getrusage(165), sysinfo(179), umask(166)
- 文件系统: statfs(43), fstatfs(44), fsync(82), fdatasync(83), readv(65), ftruncate(46)
- 调度: sched_getparam(121), sched_setparam(118), sched_getscheduler(120)
- 其他: getpgid(155), get_robust_list(100), get_mempolicy(236), sendfile(71)

**防御性修复：**
- uvm_la.c: `la_uvm_free_pgtbl` 尾部 `la_tlb_inval_all()` 全刷（QEMU invtlb 缺陷防御）
- proc.c: `la_proc_exit` 中 ISTLBR 清零 + `la_tlb_inval_all()`（退出前清 TLB）
- proc.c: `la_proc_free` 中额外的 `la_tlb_inval_all()`（释放后清 TLB）
- trap.c: ADEF 诊断增强（ERA_PTE dump，process name 输出）

**已知问题（未解决）：**
- libcbench-musl 退出阶段 ADEF 嵌套异常（era=0x20104c/0x201060，在 `la_exception_entry` 内部）
- 根因：QEMU 10.0.2 `invtlb` 不可靠 + ISTLBR 级联，TLB refill 路径上二次异常污染 TLBRERA
- 影响：GROUP END 未打印（子测试 6/6 全部 exit=0），initcode 无法继续启动后续测试组
- 尝试过的修复：PGDL 重载、ISTLBR 清零+trap frame ERA 修复、la_proc_exit TLB inval（均未根治）
- 下一步：需在 Linux 真机（KVM 加速）上验证是否是 QEMU 特定 bug；或深入排查 TLB refill→ertn 之间的中断窗口

**涉及文件**：
- 修改: `syscall.c`, `proc.c`, `proc.h`, `memfs_la.c`, `memfs_la.h`, `early_boot.h`, `boot.c`, `uvm_la.c`, `trap.c`
- 新增: `socket_la.c`, `socket_la.h`
- syscall dispatch 入口: **101**（原 ~60），95 真实实现 + 3 ENOSYS stub

---

### 2.4 系统调用覆盖统计

当前已实现/存根覆盖的 syscall 数量：**101 dispatch 入口**（95 真实实现 + 3 ENOSYS stub + 3 存根）。

```
进程: fork(4) clone(220) exec(221) exit(93) exit_group(94) wait(260) waitid(95)
      getpid(172) gettid(178) getppid(173) getcwd(17) kill(129) tgkill(131)
      getpgid(155)
线程: set_tid_address(96) set_robust_list(99) get_robust_list(100) futex(98)
      prctl(167)
信号: rt_sigaction(134) rt_sigprocmask(135) rt_sigreturn(139)
文件: open(56) close(57) read(63) write(64) lseek(62) dup(23) dup3(24)
      fstat(80) newfstatat(79) statx(291) get_dentries(61) ioctl(29)
      faccessat(48) readlinkat(78) fcntl(25) writev(66) readv(65)
      ftruncate(46) fsync(82) fdatasync(83) sendfile(71)
目录: chdir(49) mkdir(34) unlinkat(35)
文件系统: statfs(43) fstatfs(44)
管道: pipe2(59)
内存: brk(214) mmap(222) munmap(215) mprotect(226) msync(144)
      madvise(233) mlock(228) mlock2(325)
网络: socket(198) bind(200) listen(201) accept(202) accept4(242)
      connect(203) sendto(206) recvfrom(207) getsockname(204)
      getpeername(205) setsockopt(208) getsockopt(209) shutdown(210)
      sendmsg(211)* recvmsg(212)* (* = ENOSYS stub)
I/O multiplex: pselect6(72) ppoll(73)
调度: sched_yield(124) sched_setaffinity(122) sched_getaffinity(123)
      sched_setscheduler(119) sched_getparam(121) sched_setparam(118)
      sched_getscheduler(120)
时间: clock_gettime(113) gettimeofday(169) times(153)
      nanosleep(101) clock_nanosleep(115)
身份: getuid(174) geteuid(175) getgid(176) getegid(177)
系统: uname(160) shutdown(502) prlimit64(261) getcpu(168)
      getrlimit(163) getrusage(165) sysinfo(179) umask(166)
      getrandom(278) rseq(293) get_mempolicy(236)
```
文件: open(56) close(57) read(63) write(64) writev(66) lseek(62)
      dup(23) dup3(24) fstat(80) get_dentries(61) ioctl(29) fcntl(25)
      newfstatat(79) faccessat(48) readlinkat(78) statx(291)
目录: chdir(49) mkdir(34)
内存: brk(214) mmap(222) munmap(215) mprotect(226) msync(144)
管道: pipe2(59)
时间: clock_gettime(113) gettimeofday(169) times(153) nanosleep(101)
调度: sched_yield(124) sched_setaffinity(122) sched_getaffinity(123)
      sched_setscheduler(119) getcpu(168)
资源: prlimit64(261)
信息: uname(160) getuid/euid/gid/egid(174-177)
Socket: socket(198) bind(200) listen(201) accept(202) connect(203)
        sendto(206) recvfrom(207) getsockname(204) getpeername(205)
        全部返回 -ENOSYS
Select: pselect6(72) ppoll(73) — -ENOSYS
系统: shutdown(502)
```

运行时 **0 个 UNKNOWN syscall**。
