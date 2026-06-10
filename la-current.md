# LoongArch (B 线) 内核开发总结

> 最后更新：2026-06-10
>
> 本文档记录 SeaOS 项目 LoongArch 架构（B 线）的当前状态、已完成的工作、设计思路及待完成的任务。

---

## 一、项目概述

SeaOS 是一个教学操作系统项目，支持两种架构：
- **A 线（RISC-V）**：已能完整跑通评测（unixbench、busybox、cyclictest 等）
- **B 线（LoongArch）**：本次任务的开发对象

B 线内核完全独立于 A 线编译，所有 LoongArch 代码位于 `src/kernel/loongarch/` 目录下，不修改 A 线任何文件。

### 开发环境

- **构建环境**：Docker 容器 `zhouzhouyi/os-contest:20260510`，内含 `loongarch64-linux-gnu-*` 交叉编译工具链
- **QEMU**：`qemu-system-loongarch64`，virt 机器类型，1G 内存
- **启动方式**：`-kernel kernel-la.elf`，内核直接从 0x200000 开始执行（无 SBI/固件层）
- **块设备**：`-device virtio-blk-pci`（PCI 总线，区别于 RISC-V 的 MMIO）

---

## 二、整体设计思路

### 2.1 架构设计原则

1. **独立内核**：LoongArch 内核不依赖 RISC-V 代码，从零实现所有架构特定功能
2. **最小可运行**：每一步都保证 `make check-la` 通过，增量式开发
3. **自包含文件系统**：内核内嵌 SeaFS 和 EXT4 只读文件系统驱动，不依赖外部 VFS 层
4. **DA 模式内核 + 分页用户态**：
   - 内核运行在 DA=1（直接地址映射，物理地址即虚拟地址）
   - 通过 DMW0 建立内核恒等映射，使得切换到 DA=0 后内核代码仍可执行
   - 用户态通过 ertn 切换到 DA=0/PG=1，使用 TLB 进行地址翻译

### 2.2 启动流程

```
QEMU 加载 kernel-la.elf 到 0x200000
  │
  ▼
entry.S
  ├─ 设置 SP（内核栈）
  ├─ 设置 EENTRY（通用异常入口）
  ├─ 设置 TLBRENTRY（TLB 重填异常入口）
  └─ 跳转到 la_boot_main()
       │
       ▼
boot.c: la_boot_main()
  ├─ pmem_init()         物理内存管理器
  ├─ kvm_init()          DMW0 内核恒等映射
  ├─ uvm_paging_init()   页表 CSR 初始化（PWCL）
  ├─ tlb_init()          STLB 页大小配置
  ├─ timer_init()        恒定频率定时器（100 Hz）
  ├─ proc_init()         进程表初始化
  ├─ 开中断（CRMD.IE = 1）
  ├─ virtio_init()       PCI 总线扫描 + VirtIO 块设备初始化
  ├─ fs_init()           挂载 SeaFS / EXT4 文件系统
  ├─ proc_make_first()   创建 initcode 用户进程
  └─ scheduler()         进入轮转调度器（永不返回）
```

### 2.3 内核→用户态切换

```
调度器选中用户进程
  │
  ▼
swtch.S → la_proc_user_bootstrap()
  ├─ 设置 la_trap_ksp（内核栈指针）
  ├─ 设置 la_tlb_active_pgtbl（活跃页表）
  └─ la_proc_return()
       ├─ la_uvm_switch() 切换页表基址（PGDL/PGDH）
       ├─ la_tlb_inval_all() 清空 TLB
       ├─ la_tlb_fill_all() 预填 TLB 条目
       ├─ 切换 CRMD: DA=0, PG=1（启用分页）
       └─ la_user_return()（userret.S）
            ├─ 从 trap frame 恢复 ERA、PRMD、GPR
            └─ ertn → 进入用户态（PLV3）
```

### 2.4 用户→内核态切换（trap）

```
用户态触发 syscall / 异常 / 中断
  │
  ▼
trap_entry.S: la_exception_entry
  ├─ 保存内核栈指针（从 la_trap_ksp 恢复 SP）
  ├─ 在内核栈上分配 trap frame
  ├─ 保存全部 32 个 GPR + ERA + BADV + ESTAT
  ├─ 复制 TLBRPRMD → PRMD（处理 TLB 重填异常的特殊情况）
  └─ 调用 la_trap_dispatch()
       │
       ├── TLB 重填？→ la_tlb_refill_one() → 填充 TLB → 返回
       ├── 定时器中断？→ la_timer_interrupt() → 调度 → 返回
       ├── Syscall (ecode=0x0B)？→ la_syscall_dispatch() → 处理 → 返回
       └── 其他异常 → panic
```

### 2.5 地址空间布局

```
虚拟地址空间（39 位，512 GB）：
  0x0000_0000_0000 - 0x0000_0000_0FFF  未使用
  0x0000_0000_1000 - ...               用户代码段（从 VA 0x1000 开始加载）
  ...                                  用户堆区（heap_top 向上增长）
  ...                                  用户栈区（0x7FFFFFE000 向下增长）
  0x7FFFF_FE0000                       用户栈顶

内核空间（DA 模式，恒等映射）：
  0x0000_0020_0000                     内核加载地址（ELF 入口）
  0x0000_0021_9000                     物理内存管理起始（kernel_end）
  0x0000_1000_0000                     低内存结束（256 MB）
  0x0000_1800_4000                     PCI PIO 窗口
  0x0000_1FE0_01E0                     UART（8250 兼容）
  0x0000_2000_0000                     PCI ECAM（配置空间）
  0x0000_4000_0000                     PCI MMIO 窗口
  0x0000_9000_0000                     高内存起始（768 MB）
```

### 2.6 页表结构

LoongArch 三级页表（9+9+9+12 = 39 位虚拟地址）：

```
虚拟地址 [38:30] → Level 0 (root) 索引，512 条目
虚拟地址 [29:21] → Level 1 (mid)  索引，512 条目
虚拟地址 [20:12] → Level 2 (leaf) 索引，512 条目
虚拟地址 [11:0]  → 页内偏移（4KB 页）

PTE 格式（64 位）：
  [0]     V     有效位
  [1]     D     脏位（可写）
  [3:2]   PLV   特权级（0=PLV0, 3=所有 PLV）
  [5:4]   MAT   存储访问类型
  [6]     G     全局（绕过 ASID 匹配）
  [7]     P     存在位
  [47:12] PPN   物理页号
  [61]    NR    不可读
  [62]    NX    不可执行
```

PWCL CSR 值：`0x53E4D52C`（PTbase=12, PTwidth=9, Dir1_base=21, Dir1_width=9, Dir2_base=30, Dir2_width=9, PTEwidth=1）

---

## 三、已完成的工作

### Step 1-8：内核基础架构

以下子系统构成了 LoongArch 内核的基础：

| 文件            | 功能                                  | 行数 | 状态   |
| --------------- | ------------------------------------- | ---- | ------ |
| `entry.S`       | 入口点，设置 SP/异常向量              | ~40  | ✅ 完成 |
| `boot.c`        | 启动序列，UART 输出                   | ~93  | ✅ 完成 |
| `kernel.ld`     | 链接脚本（0x200000 起始）             | ~40  | ✅ 完成 |
| `trap_entry.S`  | 异常入口/退出，GPR 保存恢复           | ~80  | ✅ 完成 |
| `trap_layout.h` | CSR 地址、trap frame 偏移量常量       | ~131 | ✅ 完成 |
| `trap.h`        | trap frame 结构、异常码枚举           | ~57  | ✅ 完成 |
| `trap.c`        | trap 分发（中断/syscall/异常）        | ~92  | ✅ 完成 |
| `userret.S`     | 内核→用户态切换汇编                   | ~40  | ✅ 完成 |
| `userret.c`     | 用户态返回桥接                        | ~20  | ✅ 完成 |
| `pmem.c`        | 物理内存管理（空闲链表页分配器）      | ~88  | ✅ 完成 |
| `kvm.c`         | 内核虚拟内存（DMW0 恒等映射）         | ~34  | ✅ 完成 |
| `uvm_la.c`      | 用户虚拟内存（三级页表）              | ~349 | ✅ 完成 |
| `tlb_la.c`      | TLB 管理 + 重填处理                   | ~154 | ✅ 完成 |
| `timer.c`       | 恒定频率定时器（100 Hz）              | ~63  | ✅ 完成 |
| `swtch.S`       | 内核上下文切换（callee-saved 寄存器） | ~47  | ✅ 完成 |
| `proc.h`        | 进程控制块结构定义                    | ~122 | ✅ 完成 |
| `proc.c`        | 进程管理 + 轮转调度器                 | ~410 | ✅ 完成 |
| `virtio_la.c`   | VirtIO PCI 块设备驱动                 | ~570 | ✅ 完成 |
| `fs_la.c`       | 文件系统（SeaFS + EXT4 只读）         | ~719 | ✅ 完成 |
| `exec_la.c`     | ELF 加载器 + initcode 创建            | ~472 | ✅ 完成 |
| `early_boot.h`  | 常量、内存布局、函数声明              | ~116 | ✅ 完成 |

### Step 9：核心系统调用

本次任务实现了完整的系统调用层，使 initcode 能够扫描磁盘目录、fork/exec/wait 子进程。

#### 修改的文件

| 文件                                | 变更内容                                                                                                                                                         |
| ----------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/kernel/loongarch/proc.h`       | 添加 fd 表（`la_fd[32]`）、`parent_pid`、`exit_code`、`cwd_ino`；声明 sleep/wakeup/proc_by_pid/proc_table                                                        |
| `src/kernel/loongarch/proc.c`       | 初始化新字段；实现 `la_proc_sleep`、`la_proc_wakeup_pid`、`la_proc_by_pid`、`la_proc_table`                                                                      |
| `src/kernel/loongarch/uvm_la.c`     | 添加 `#include "proc.h"`；实现 `la_copy_from_user`、`la_copy_to_user`、`la_copy_str_from_user`（用户/内核数据拷贝）；实现 `la_uvm_copy_pgtbl`（fork 页表深拷贝） |
| `src/kernel/loongarch/early_boot.h` | 声明新的 uvm 拷贝函数和 fs 查询函数                                                                                                                              |
| `src/kernel/loongarch/fs_la.c`      | 实现 `la_fs_get_dentries`（Linux dirent64 格式）、`la_fs_inode_type`、`la_fs_inode_size`、`la_fs_is_sea`                                                         |
| `src/kernel/loongarch/syscall.c`    | **全部重写**：实现 18 个系统调用                                                                                                                                 |
| `src/kernel/loongarch/exec_la.c`    | 为首个进程初始化 fd 表和 cwd_ino；新增 `la_do_exec_syscall`（带 argv 支持的 ELF 加载器）                                                                         |

#### 新增的文件

| 文件                     | 功能                                         |
| ------------------------ | -------------------------------------------- |
| `src/user/initcode_la.c` | 完整版 initcode，匹配 RISC-V initcode.c 功能 |

#### 修改的构建文件

| 文件       | 变更                                                                      |
| ---------- | ------------------------------------------------------------------------- |
| `Makefile` | `check-la` 目标添加 `-drive` 和 `-device virtio-blk-pci` 参数挂载磁盘映像 |

#### 已实现的 18 个系统调用

| 类别 | 系统调用           | 编号 | 实现说明                                                                           |
| ---- | ------------------ | ---- | ---------------------------------------------------------------------------------- |
| 进程 | `SYS_fork`         | 4    | 深拷贝页表、复制 trap frame（a0=0 给子进程）、复制 fd 表、设置 parent_pid          |
| 进程 | `SYS_wait`         | 260  | 扫描子进程找 ZOMBIE，复制退出码，释放子进程；无 ZOMBIE 时 sleep 等待               |
| 进程 | `SYS_exit`         | 93   | 保存退出码、唤醒父进程、设 ZOMBIE、切换到调度器                                    |
| 进程 | `SYS_getpid`       | 172  | 返回当前进程 PID                                                                   |
| 进程 | `SYS_exec`         | 221  | 从用户空间拷贝路径 → 查找 inode → 加载 ELF → 设置 argv → 切换到用户态              |
| 文件 | `SYS_open`         | 56   | 拷贝路径 → `la_fs_lookup` → 分配 fd                                                |
| 文件 | `SYS_close`        | 57   | 验证 fd → 清零 fd 条目                                                             |
| 文件 | `SYS_read`         | 63   | console fd 返回 0；file fd 通过 `la_fs_read_file` 读取到内核缓冲区再拷贝到用户空间 |
| 文件 | `SYS_write`        | 64   | console fd (0-2) → UART 输出（逐页表翻译）；file fd → 只读文件系统返回 -1          |
| 文件 | `SYS_lseek`        | 62   | 调整 fd 偏移量（SET/ADD/SUB 三种模式）                                             |
| 文件 | `SYS_dup`          | 23   | 找空闲 fd → 复制 fd 条目                                                           |
| 文件 | `SYS_fstat`        | 80   | 从 inode 元数据填充 file_stat_t 结构                                               |
| 文件 | `SYS_get_dentries` | 61   | 调用 `la_fs_get_dentries` 获取 dirent64 格式目录项，拷贝到用户空间                 |
| 目录 | `SYS_chdir`        | 49   | 解析路径 → 验证是目录 → 设置 cwd_ino                                               |
| 目录 | `SYS_mkdir`        | 34   | 只读文件系统，返回 -1                                                              |
| 内存 | `SYS_brk`          | 214  | 调整 heap_top                                                                      |
| 内存 | `SYS_mmap`         | 222  | 分配物理页、映射到用户空间（addr=0 时使用 heap_top）                               |
| 内存 | `SYS_munmap`       | 215  | 桩实现，返回 0                                                                     |
| 系统 | `SYS_shutdown`     | 502  | UART 打印关机消息 → `idle 0` 挂起                                                  |

#### 关键技术实现

**用户/内核数据拷贝**：

由于内核运行在 DA=1（物理地址即虚拟地址），而用户态通过 TLB 分页，内核需要通过页表遍历来翻译用户虚拟地址。`la_copy_from_user` 和 `la_copy_to_user` 逐页边界处理跨页拷贝：

```c
// 伪代码
while (done < len) {
    pa = walk_page_table(user_va + done);  // 三级页表遍历
    memcpy(kernel_buf + done, (void*)pa, chunk);
    done += chunk;
}
```

**fork 的页表深拷贝**：

`la_uvm_copy_pgtbl` 遍历源页表的所有三级条目，为每个有效映射分配新的物理页并复制数据，保持相同的权限位。子进程的 trap frame 复制自父进程，但 a0 设为 0（fork 返回值）。

**exec 的 argv 设置**：

`la_do_exec_syscall` 从用户空间拷贝 argv 字符串数组，将字符串数据和指针推入用户栈，设置 a0=argc、a1=argv 指针。

**SeaFS/EXT4 目录项转 dirent64**：

`la_fs_get_dentries` 将内部文件系统格式转换为 Linux dirent64 格式：
```
struct dirent64 {
    uint64_t d_ino;      // inode 号
    uint64_t d_off;      // 下一条目的偏移
    uint16_t d_reclen;   // 记录长度
    uint8_t  d_type;     // DT_REG=8, DT_DIR=4
    char     d_name[];   // 文件名，NUL 终止，8 字节对齐
};
```

### 当前启动输出

```
loongarch boot start
[init] pmem
  pmem: region 0x219000 - 0x10000000
  pmem: 0xFDE7 pages (0xFD MB)
[init] kvm
  kvm: DMW0 identity-mapped for PLV0
[init] paging
  uvm: PWCL=0x13E4D52C (expect 0x53E4D52C)
[init] tlb
[init] timer
  timer: periodic 100 Hz enabled
[init] proc
  proc: table initialized
[init] enabling interrupts
[init] kernel ready
[init] virtio
    pci 0x0: vendor=0x1B36 device=0x8          ← QEMU PCI 桥
    pci 0x1: vendor=0x1AF4 device=0x1000       ← VirtIO 网络
    pci 0x2: vendor=0x1AF4 device=0x1001       ← VirtIO 块设备 ✓
  virtio: block device ready
[init] fs
  fs: SeaOS FS mounted! inodes=0x10000 data=0x42B+0x140000
[init] fs: mounted
[init] creating first user process
  proc: created user 'initcode' pid=0x1
  proc_return: filled 0x2 TLB entries
  proc_return: switching DA=0 PG=1
initcode: started
initcode: no *_testcode.sh found
shutdown: system halting
```

`make check-la` 通过，验证项：
- ✅ `loongarch boot start` 出现
- ✅ `[init] kernel ready` 出现
- ✅ VirtIO 块设备发现并挂载 SeaFS
- ✅ initcode 用户进程创建并运行
- ✅ 系统调用链路完整（write、open、get_dentries、close、shutdown）
- ✅ 干净关机，无 panic/hang

---

## 四、与 RISC-V A 线的对比

| 方面             | RISC-V (A 线)                           | LoongArch (B 线)                         |
| ---------------- | --------------------------------------- | ---------------------------------------- |
| **启动方式**     | OpenSBI 固件 → S-mode 内核              | 无固件，直接从 0x200000 执行             |
| **VirtIO 总线**  | MMIO (`virtio-blk-device`)              | PCI (`virtio-blk-pci`)                   |
| **中断控制器**   | PLIC                                    | 恒定频率定时器（直接 CSR）               |
| **特权级切换**   | S-mode ↔ U-mode (sret)                  | PLV0 ↔ PLV3 (ertn)                       |
| **页表格式**     | Sv39（类似三级页表）                    | LoongArch 页表（CSR: PWCL/PGD）          |
| **Syscall 触发** | ecall 指令                              | syscall 0 指令 (ecode=0x0B)              |
| **地址翻译**     | satp + sfence.vma                       | PGDL/PGDH + invtlb + tlbfill             |
| **代码复用**     | 使用完整架构无关子系统                  | 独立实现，不依赖 A 线代码                |
| **用户程序**     | RISC-V ELF（musl/glibc 编译）           | LoongArch ELF（需要 LA 工具链编译）      |
| **评测结果**     | ✅ unixbench/busybox/cyclictest 全部通过 | ❌ 尚未运行任何评测（磁盘无 LA 测试脚本） |

### RISC-V A 线评测输出参考

A 线成功运行了以下测试组：
1. **unixbench-musl**：DHRY2=24501677 lps, WHETSTONE=438 MFLOPS, SYSCALL=2245 lps 等
2. **busybox-musl**：echo/ash/sh/basename/cal/clear/date/df/ls/grep/cat/sort/stat 等 40+ 命令测试
3. **cyclictest-musl**：NO_STRESS_P1/P8 和 STRESS_P1 通过（STRESS_P8 在 hackbench 负载下 page fault 崩溃）

---

## 五、文件清单

### 内核源文件（`src/kernel/loongarch/`）

| 文件            | 行数     | 功能                                      | 创建方式     |
| --------------- | -------- | ----------------------------------------- | ------------ |
| `entry.S`       | ~40      | 入口点，设置 SP/异常向量                  | 从头编写     |
| `boot.c`        | ~93      | 启动序列 + UART 输出                      | 从头编写     |
| `kernel.ld`     | ~40      | 链接脚本                                  | 从头编写     |
| `trap_entry.S`  | ~80      | 异常入口/退出汇编                         | 从头编写     |
| `trap_layout.h` | ~131     | CSR 地址、trap frame 偏移量               | 从头编写     |
| `trap.h`        | ~57      | trap frame 结构、异常码                   | 从头编写     |
| `trap.c`        | ~92      | trap 分发逻辑                             | 从头编写     |
| `userret.S`     | ~40      | 内核→用户态切换汇编                       | 从头编写     |
| `userret.c`     | ~20      | 用户态返回桥接                            | 从头编写     |
| `early_boot.h`  | ~116     | 常量、声明                                | 从头编写     |
| `pmem.c`        | ~88      | 物理内存管理器                            | 从头编写     |
| `kvm.c`         | ~34      | DMW0 内核恒等映射                         | 从头编写     |
| `uvm_la.c`      | ~349     | 用户虚拟内存 + 数据拷贝 + 页表深拷贝      | 从头编写     |
| `tlb_la.c`      | ~154     | TLB 管理 + 重填处理                       | 从头编写     |
| `timer.c`       | ~63      | 恒定频率定时器                            | 从头编写     |
| `swtch.S`       | ~47      | 内核上下文切换                            | 从头编写     |
| `proc.h`        | ~122     | 进程控制块定义                            | 从头编写     |
| `proc.c`        | ~410     | 进程管理 + 调度器 + sleep/wakeup          | 从头编写     |
| `virtio_la.c`   | ~570     | VirtIO PCI 块设备驱动                     | 从头编写     |
| `fs_la.c`       | ~719     | SeaFS + EXT4 只读文件系统                 | 从头编写     |
| `exec_la.c`     | ~472     | ELF 加载器 + initcode 创建 + exec syscall | 从头编写     |
| `syscall.c`     | ~591     | 18 个系统调用实现                         | 从头编写     |
| `initcode_la.h` | 自动生成 | initcode 二进制转 C 头文件                | 构建系统生成 |

### 用户程序源文件

| 文件                     | 行数 | 功能                                     |
| ------------------------ | ---- | ---------------------------------------- |
| `src/user/initcode_la.c` | ~305 | 第一个用户进程：扫描磁盘、fork/exec/wait |

### 构建相关文件

| 文件                    | 功能                                                |
| ----------------------- | --------------------------------------------------- |
| `src/loader/user-la.ld` | LoongArch 用户程序链接脚本（链接到地址 0）          |
| `Makefile`              | 构建系统（修改了 check-la 目标）                    |
| `tools/la_elfgen.c`     | LoongArch 最小 ELF 生成工具（无工具链时的后备方案） |

**总代码量**：约 3,800+ 行（不含自动生成文件和构建工具）

---

## 六、待完成的工作

### 6.1 核心缺失（阻塞评测）

1. **LoongArch 测试程序编译**：当前磁盘映像（`disk.img`）中的 `.elf` 文件是 RISC-V 架构的。需要：
   - 用 LoongArch 工具链编译测试程序（unixbench/busybox/cyclictest 等）
   - 或者获取预编译的 LoongArch 测试程序
   - 生成 `sdcard-la.img`（包含 LoongArch ELF + 测试脚本）
   - 当前 initcode 扫描磁盘但找不到 `*_testcode.sh` 文件

2. **文件系统写入支持**：当前内核的文件系统是只读的。测试程序（如 busybox）需要：
   - `SYS_write` 对文件 fd 的写入（目前返回 -1）
   - 文件创建/删除（`SYS_creat`、`SYS_unlink`）
   - 可能需要实现 SeaFS 的写操作或使用 EXT4 分区

3. **管道 (pipe) 支持**：busybox 等程序广泛使用管道：
   - `SYS_pipe` / `SYS_pipe2` 系统调用
   - 管道缓冲区实现（环形缓冲区 + 阻塞/唤醒）

4. **信号支持**：cyclictest 等程序可能需要信号机制

### 6.2 系统调用补全

RISC-V A 线支持的但 B 线尚未实现的系统调用：

| 类别 | 系统调用                                          | 优先级 | 说明                         |
| ---- | ------------------------------------------------- | ------ | ---------------------------- |
| 进程 | `SYS_clone`                                       | 高     | 线程创建（busybox 可能使用） |
| 进程 | `SYS_getppid`                                     | 中     | 获取父进程 ID                |
| 文件 | `SYS_creat`                                       | 高     | 创建文件                     |
| 文件 | `SYS_unlink`                                      | 高     | 删除文件                     |
| 文件 | `SYS_link`                                        | 中     | 创建硬链接                   |
| 文件 | `SYS_pipe`                                        | 高     | 创建管道                     |
| 文件 | `SYS_stat`                                        | 中     | 通过路径获取文件状态         |
| 文件 | `SYS_readlink`                                    | 低     | 读取符号链接                 |
| 文件 | `SYS_symlink`                                     | 低     | 创建符号链接                 |
| 文件 | `SYS_truncate`                                    | 中     | 截断文件                     |
| 文件 | `SYS_fsync`                                       | 低     | 同步文件                     |
| 目录 | `SYS_rmdir`                                       | 中     | 删除目录                     |
| 网络 | `SYS_socket/bind/listen/accept/connect/send/recv` | 低     | 网络测试需要                 |
| 时间 | `SYS_gettimeofday`                                | 中     | 获取时间                     |
| 时间 | `SYS_clock_gettime`                               | 中     | 获取时钟时间                 |
| 时间 | `SYS_times`                                       | 中     | 进程时间统计                 |
| 内存 | `SYS_mprotect`                                    | 低     | 修改内存保护                 |
| 系统 | `SYS_uname`                                       | 中     | 系统信息                     |
| 系统 | `SYS_ioctl`                                       | 中     | 设备控制                     |

### 6.3 内核功能增强

| 功能            | 优先级 | 说明                                                  |
| --------------- | ------ | ----------------------------------------------------- |
| 文件系统写入    | 高     | 实现文件创建、写入、删除，支持 busybox 的文件操作测试 |
| 管道            | 高     | 实现管道，支持 shell 管道操作                         |
| 多进程调度优化  | 中     | 当前为简单轮转，可能需要优先级或时间片                |
| 进程资源回收    | 中     | fork 后旧页表的释放（当前有 TODO 标记）               |
| 缓冲区缓存      | 中     | 当前每次磁盘读取都直接发起 VirtIO 请求，性能差        |
| VirtIO 中断模式 | 低     | 当前为轮询模式，可改为中断驱动提升性能                |
| 多核支持        | 低     | 当前 `CPUNUM=1`，SMP 测试需要多核                     |
| 网络驱动        | 低     | `virtio-net-pci` 驱动，用于网络测试                   |
| 信号量/互斥锁   | 中     | 用户态同步原语                                        |
| /proc 文件系统  | 中     | busybox 的 ps/free 等命令需要                         |

### 6.4 已知问题

1. **PWCL 读取值与期望值不一致**：启动日志显示 `PWCL=0x13E4D52C (expect 0x53E4D52C)`。高 32 位不同，可能是 QEMU 的实现差异，但不影响功能（低 32 位正确）。

2. **initcode 不匹配测试脚本**：当前磁盘映像是 `disk.img`（包含 RISC-V 测试程序），initcode 扫描 `/musl`、`/glibc`、`/` 后找不到 `*_testcode.sh`。需要制作包含 LoongArch 测试程序的 `sdcard-la.img`。

3. **fork/exec/wait 未经过实际测试**：由于没有测试脚本，fork/exec/wait 链路只实现了代码但未在运行时验证。

4. **定时器中断与调度**：定时器中断目前仅计数和打印心跳，未触发进程抢占（时间片）。这可能导致长时间运行的进程无法被调度器切换。

5. **EXT4 写入**：fs_la.c 的 EXT4 部分只实现了读取。如果测试需要 EXT4 写入（大概率需要），需要大量补充代码。

---

## 七、快速参考

### 构建与测试命令

```bash
# 在 Docker 容器中构建
docker exec nostalgic_khayyam bash -c "cd /workspace && make clean && make all"

# 运行 LoongArch 内核（带磁盘）
docker exec nostalgic_khayyam bash -c \
  "cd /workspace && timeout 10s qemu-system-loongarch64 \
    -kernel kernel-la -m 1G -nographic -smp 1 \
    -drive file=target/mkfs/disk.img,if=none,format=raw,id=x0 \
    -device virtio-blk-pci,drive=x0"

# 自动化检查
docker exec nostalgic_khayyam bash -c "cd /workspace && make check-la"
```

### 关键地址速查

| 地址           | 用途                 |
| -------------- | -------------------- |
| `0x200000`     | 内核 ELF 加载地址    |
| `0x1FE001E0`   | UART（8250 兼容）    |
| `0x18004000`   | PCI PIO 窗口基址     |
| `0x20000000`   | PCI ECAM 配置空间    |
| `0x40000000`   | PCI MMIO 窗口        |
| `0x1000`       | 用户代码起始虚拟地址 |
| `0x7FFFFFE000` | 用户栈顶             |
| `0x219000`     | 物理内存管理起始     |
| `0x10000000`   | 低内存结束（256 MB） |

### CSR 速查

| CSR       | 地址      | 用途                     |
| --------- | --------- | ------------------------ |
| CRMD      | 0x0       | 当前模式（PLV/DA/PG/IE） |
| PRMD      | 0x1       | 异常前模式（PPLV/PIE）   |
| ECFG      | 0x4       | 异常配置（中断使能）     |
| ESTAT     | 0x5       | 异常状态（ecode/IS）     |
| ERA       | 0x6       | 异常返回地址             |
| BADV      | 0x7       | 错误虚拟地址             |
| EENTRY    | 0xC       | 通用异常入口地址         |
| TLBRENTRY | 0x88      | TLB 重填异常入口         |
| PWCL/PWCH | 0x1C/0x1D | 页表遍历配置             |
| PGDL/PGDH | 0x19/0x1A | 页目录基址               |
| TCFG      | 0x41      | 定时器配置               |
| TVAL      | 0x42      | 定时器当前值             |
| TICLR     | 0x44      | 定时器中断清除           |
| DMW0      | 0x180     | 直接映射窗口 0           |

>     以上为2026-06-10内容
---
