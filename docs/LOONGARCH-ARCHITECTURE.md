# SeaOS LoongArch 内核架构

> 目标读者：对 LoongArch 架构还不太熟的开发者。
> 目标：读完这篇，你能说清楚"从按下开机（启动 QEMU）到屏幕上打印出一个测试程序的结果"，SeaOS 的 LoongArch 内核**每一层都做了什么、为什么这么做**。
>
> 本文基于 `src/kernel/loongarch/` 的**真实源码**撰写，不是泛泛而谈。所有文件名、函数名、常量都可对照源码验证。
>
> 配套阅读：项目总指引见 `CLAUDE.md`；syscall 号台账见 `docs/SYSCALL_STATUS.md`；设计决策见 `docs/DECISIONS.md`；开发日志见 `la-current.md`。

---

## 0. 三分钟先建立全局印象

先用一句话讲完整个内核在干什么：

> **内核的本质，是一个"特权最高的常驻程序"，负责把 CPU、内存、磁盘这些硬件，公平、安全地分给若干个用户程序用，并在用户程序和硬件之间当"中间人"。**

SeaOS 的 LoongArch 内核（下文简称 **LA 内核**）是一个 xv6 风格的小内核。它做的事可以拆成 8 块，对应 8 个源码文件群：

| 子系统 | 核心文件 | 一句话职责 |
| --- | --- | --- |
| 启动引导 | `entry.S` / `boot.c` / `kernel.ld` | 上电后第一条指令，初始化硬件、各子系统、创建第一个用户进程 |
| 物理内存 | `pmem.c` | 管理物理内存，按"页（4KB）"为单位分配/回收 |
| 内核地址映射 | `kvm.c` | 让内核在"分页"开启后仍能直接访问物理内存 |
| 用户虚拟内存 | `uvm_la.c` | 给每个用户进程一套独立的"假地址"（虚拟地址）→ 物理地址的翻译 |
| TLB | `tlb_la.c` | 给地址翻译做硬件缓存，并处理缓存未命中 |
| 进程与调度 | `proc.c` / `proc.h` / `swtch.S` | 管理进程表、切换进程、决定下一个跑谁 |
| 异常/中断/系统调用 | `trap.c` / `trap_entry.S` / `userret.S` / `timer.c` | 处理所有"CPU 突然跳到内核"的情况 |
| 加载程序 | `exec_la.c` | 把磁盘上的 ELF / 脚本装进内存、准备好运行环境 |
| 文件系统 | `fs_la.c` | 只读地解析 EXT4（评测镜像）/ SeaFS，读文件、列目录 |
| 块设备驱动 | `virtio_la.c` | 驱动 VirtIO 虚拟磁盘，真正去读扇区 |
| 第一个进程 | `initcode_la.h` | 内核里内嵌的一段 C 程序，启动后跑它，由它去找并执行测试脚本 |

整条链路的"全景图"：

```
        ┌─────────────────────────────────────────────────────────────┐
        │                    QEMU 虚拟机（LoongArch）                  │
        │                                                             │
        │   CPU  ──异常/中断/系统调用──►  内核（本项目的 kernel-la）   │
        │    ▲                              │                         │
        │    │ 运行用户程序                  │ 用 syscall 服务用户程序 │
        │    │                              ▼                         │
        │  用户进程(initcode/busybox/…)   VirtIO 虚拟磁盘(sdcard-la.img)│
        └─────────────────────────────────────────────────────────────┘
```

后面的章节会从"硬件背景"开始，逐层往上讲。**如果某节看不懂，先跳过看下一节，最后回头看"全链路串联"那一章，往往就通了。**

---

## 1. 先懂一点 LoongArch 硬件（够用就行）

不懂硬件细节也能写内核，但有几个概念必须建立，否则后面全是问号。

### 1.1 寄存器：CPU 的"口袋"

LoongArch 有 32 个通用寄存器 `$r0`～`$r31`，它们是 CPU 内部最快、最小的存储。源码里给它们起了别名（见 `trap.h` 的 `enum la_gpr_index`）：

| 别名 | 编号 | 用途（约定） |
| --- | --- | --- |
| `$zero` | 0 | 永远是 0 |
| `$ra` | 1 | return address，函数返回地址 |
| `$tp` / `$sp` | 2 / 3 | 线程指针 / 栈指针 |
| `$a0`～`$a7` | 4～11 | 参数与返回值（**系统调用号放 `$a7`，返回值放 `$a0`**） |
| `$t0`～`$t8` | 12～20 | 临时寄存器 |
| `$s0`～`$s8` | 23～31 | 被调用方保存寄存器（函数调用前后值不变） |

> 类比：寄存器是 CPU 的"口袋"，就那么几个；内存是"仓库"，很大但要慢慢搬。内核代码绝大部分时间在"把仓库的东西搬进口袋、算一下、再搬回仓库"。

### 1.2 特权级：内核和用户"不在一个楼层"

LoongArch 有 4 个特权级 PLV0～PLV3，内核只用两个：

- **PLV0** = 内核态（最高权限，能碰所有硬件）
- **PLV3** = 用户态（普通程序，只能碰自己的内存，想用硬件必须"求"内核）

用户程序想读磁盘？它没权限，只能通过**系统调用（syscall）**"陷入"内核代劳，完事再"返回"用户态。

### 1.3 CSR：控制 CPU 行为的"开关面板"

除了通用寄存器，LoongArch 还有一堆 **CSR（Control/Status Register，控制状态寄存器）**，它们是 CPU 的"开关和仪表盘"。本项目用到的关键 CSR（定义在 `trap_layout.h`）：

| CSR | 地址 | 作用 |
| --- | --- | --- |
| `CRMD` | 0x0 | 当前特权级 + 是否开中断 + 是否开分页（DA/PG 位） |
| `PRMD` | 0x1 | 异常**前**的特权级与中断状态（用来返回时恢复） |
| `ECFG` / `ESTAT` | 0x4 / 0x5 | 中断使能 / 中断 pending（谁在请求中断） |
| `ERA` / `BADV` | 0x6 / 0x7 | 异常时的 PC / 出问题的虚拟地址 |
| `EENTRY` | 0xc | 通用异常入口地址（异常跳到这） |
| `TLBRENTRY` | 0x88 | TLB 未命中异常的专用入口 |
| `PGDL` / `PGDH` / `PGD` | 0x19/0x1A/0x1B | 当前页表根地址 |
| `PWCL` / `PWCH` | 0x1C/0x1D | 页表遍历参数（告诉硬件页表长什么样） |
| `DMW0` | 0x180 | 直接映射窗口（让一段虚拟地址=物理地址） |
| `TCFG` / `TICLR` | 0x41 / 0x44 | 定时器配置 / 清定时器中断 |

源码里用两个宏（`early_boot.h`）读写它们：

```c
la_csr_read(CSR)              // 读
la_csr_write(value, CSR)      // 写
```

> 你暂时不用背这张表，只需知道：**调整 CPU 行为 = 改某个 CSR；了解 CPU 状态 = 读某个 CSR。** 后面遇到再回来查。

### 1.4 地址：物理地址 vs 虚拟地址（全文最关键的概念之一）

- **物理地址（PA）**：真实内存条上的地址，`0x0` 开始往上数。QEMU 用 `-m 1G` 给了 1GB。
- **虚拟地址（VA）**：程序"以为"自己用的地址。用户程序眼里只有虚拟地址，根本不知道物理地址长啥样。

**内核的活儿之一，就是维护一张"VA → PA 的翻译表"（页表），并在 CPU 访问内存时按表翻译。** 这是整个内核最绕的部分，第 3 章专门讲。

---

## 2. 启动：从上电到进调度器

### 2.1 入口：`entry.S`（只有 25 行，却是万丈高楼第一步）

CPU 一上电，从 `LA_KERNEL_ENTRY = 0x200000`（链接脚本 `kernel.ld` 指定）取第一条指令，也就是 `_entry`（`entry.S`）。它做三件事：

```asm
_entry:
    la.local $sp, la_boot_stack_top        # 1. 设好内核栈
    csrwr   $t0, 0x4 ...                   # 2. 把"通用异常入口"指向 la_exception_entry
    csrwr   $t0, LA_CSR_TLBRENTRY          #    把"TLB未命中入口"也指向同一个处理函数
    bl      la_boot_main                   # 3. 跳进 C 代码 la_boot_main
```

> 类比：一栋楼刚通电，先把"电梯（异常入口）"指向管理处，再开门营业（进 C 代码）。

注意一个设计：**所有异常（包括 TLB 未命中）都先汇入同一个入口 `la_exception_entry`**，进去之后再分情况处理。这让异常处理逻辑集中，便于维护。

### 2.2 主引导：`boot.c::la_boot_main`

这是启动的"总指挥"，按固定顺序点亮各子系统（顺序不能乱，因为后面的依赖前面的）：

```
la_boot_main()
 ├─ 1. la_pmem_init()        物理内存分配器先就绪（后面谁都要内存）
 ├─ 2. la_kvm_init()         配置 DMW0（保证开分页后内核还能跑）
 ├─ 3. la_uvm_paging_init()  配置分页参数 + 开硬件页表遍历 HPTW
 ├─ 3b. la_tlb_init()        配置 TLB（STLB、4KB 页）
 ├─ 4. la_timer_init()       启动时钟（100Hz 心跳）
 ├─ 5. la_proc_init()        清空进程表
 ├─ 6. 开全局中断 (CRMD.IE=1)
 ├─ 7. la_virtio_init()      初始化虚拟磁盘
 ├─ 8. la_fs_init()          挂载文件系统
 ├─ 9. la_proc_make_first()  创建第一个用户进程（initcode）
 └─ 10. la_scheduler()       进入调度器循环（永不返回）
```

串口会依次打印 `[init] pmem`、`[init] kvm` …… 直到 `[init] kernel ready`。评测脚本就是靠 grep `loongarch boot start` + `[init] kernel ready` 来判断内核有没有正常启动（见 `CLAUDE.md` §4）。

> **为什么 `boot.c` 里 UART 输出函数（`la_uart_putc` 等）也在这里？** 因为串口是最早可用、最简单的输出手段——往固定地址 `0x1fe001e0` 写一个字节就吐到屏幕，不需要任何驱动。所以整个内核的"printf"都是基于它。

---

## 3. 内存管理（全文最重点，慢慢读）

这一章分四节：物理内存 → 内核怎么用内存 → 用户怎么用内存 → 地址空间布局。

### 3.1 物理内存分配器：`pmem.c`

**任务**：把物理内存切成一页页（4KB），谁要用就给谁一页，用完还回来。

实现极其朴素——一个**单向链表的"空闲页池"**：

```
free_list → [页A] → [页B] → [页C] → NULL
             │
             └── 每个空闲页的最前 8 字节，存"下一页的地址"
```

- `la_pmem_init`：把 `内核末尾 ~ 0x10000000`（低内存 256MB）的每一页串进链表。
- `la_pmem_alloc`：摘下链表头那一页，**清零**，返回。
- `la_pmem_free`：把页插回链表头。

> 类比：停车场只有一个进出口，每辆"车"（页）钥匙挂在墙上。取车拿第一把，还车挂最前面。简单、够用，但没有按大小查找能力（只能整页整页给）。

**清零很重要**：分配出去的页必须是全 0，否则上一任用过的残留数据会让程序（尤其 musl 的 malloc）误判，埋下各种玄学崩溃。

### 3.2 内核怎么访问内存：`kvm.c` 与 DMW0

这里有个关键矛盾：

- 内核想用**分页**（用户态必须用分页才有隔离）。
- 但开了分页后，内核自己的代码也必须能被翻译成物理地址才能跑——否则内核自己先崩了。

LA 内核的解法是 **DMW0（Direct Mapping Window，直接映射窗口）**（`kvm.c`）：

```
DMW0 的作用：规定"虚拟地址的高 4 位 = 0 的那一大段地址，直接等于物理地址，且只有 PLV0（内核）能访问"。
```

于是内核代码里的地址 `0x200000`，在 DMW0 下直接就是物理地址 `0x200000`，**无需页表翻译**。这样：

- 内核态（PLV0）：低地址（`0x0 ~ 0x10000000`）靠 DMW0 直接访问物理内存，简单可靠。
- 用户态（PLV3）：DMW0 对它不可见，只能走页表（TLB）翻译——天然隔离。

> 这就是为什么内核可以一直用"物理地址当指针"直接读写内存（比如 `pmem.c` 返回的就是物理地址指针），而用户程序必须用虚拟地址。

### 3.3 用户怎么访问内存：`uvm_la.c`（页表 + TLB + HPTW）

这是最绕的一节，分三小步。

#### 3.3.1 三级页表

每个用户进程拥有一张**专属页表**，记录"它的虚拟地址 → 物理地址"的映射。LA 内核用**三级页表**（`uvm_la.c` 注释画得很清楚）：

```
39 位虚拟地址拆成 4 段：
 [38:30]  [29:21]  [20:12]  [11:0]
   9位      9位      9位    12位(页内偏移)
   │        │        │
   ▼        ▼        ▼
 root表   mid表    leaf表   →  得到物理页号(PFN)
(512项)  (512项)  (512项)        + 页内偏移 = 物理地址
```

翻译过程像查三级字典：root 表里找第几项 → 指向一张 mid 表 → 再找第几项 → 指向一张 leaf 表 → 再找第几项 → 得到物理页。

每个表都是 1 页（512 项 × 8 字节 = 4096 字节）。

> 类比：找一本书"第 X 章 Y 节 Z 段第 N 个字"。root=章目录，mid=节目录，leaf=段目录，最后页内偏移=第几个字。

**两个必须记住的约定**（都在 `uvm_la.c` 的注释里，是踩过坑才总结的）：

1. **目录项（root/mid）只存"下一级表的物理地址"，不带任何标志位**。因为 QEMU 的硬件页表遍历器不会自动剥掉标志位，带了就会把标志位 OR 进地址，翻译出乱七八糟的物理地址。
2. **叶项（leaf）才带完整标志位**：有效(V)、可写(D)、特权级(PLV)、存在(P)、可写(HPTW用W位) 等。

常用权限组合（`uvm_la.c`）：

```c
LA_PTE_U_RWX = V|D|PLV_USER|MAT_CC|P|W = 0x18F   // 用户可读可写可执行（代码/堆/栈通用）
```

#### 3.3.2 TLB：给翻译做缓存（`tlb_la.c`）

每次访问内存都查三级页表太慢了。CPU 内置了 **TLB（Translation Lookaside Buffer，地址翻译缓存）**，把最近用过的翻译结果缓存起来。

LA 内核用的是 **STLB**（大 TLB，2048 项 = 256 组 × 8 路），而不是小的 MTLB（64 项）。2048 项足够装下 busybox 这种大程序的全部页，不会抖动。

⚠️ **LoongArch 的 TLB 有个反直觉的特性**：**一个 TLB 表项同时管两个相邻的页（even/odd 对）**，按 VPPN（虚拟页号÷2）索引。这给本项目带来过灾难性的 bug（见第 11 章"踩过的坑"）。

#### 3.3.3 HPTW：让硬件自己走页表

`la_uvm_paging_init` 做了一件大事——**开启 HPTW（Hardware Page Table Walker，硬件页表遍历器）**：

```c
la_csr_write(1UL << 24, LA_CSR_PWCH);   // PWCH 的 bit24 = HPTW_EN
```

之前（没开 HPTW）：CPU 访问一个没缓存的虚拟地址 → **触发"TLB 未命中"异常** → 内核软件走页表填 TLB → 返回。对于 busybox 这种几千页的程序，会触发几百万次异常，慢到无法接受。

开了 HPTW 后：CPU 访问没缓存的地址 → **硬件自己按 `PWCL` 描述的页表格式去走表、填 TLB**，全程不触发异常。性能天壤之别。

> `PWCL` 这个 CSR 就是"告诉硬件你的页表长什么样"：每级几位、偏移多少。本项目配的是上面那套三级 9+9+9+12 的格式。

> 即便开了 HPTW，`tlb_la.c` 仍保留了**软件 refill 路径**（`la_tlb_refill_one`、`la_tlb_fill_all`），用于 exec 后一次性预填 TLB、以及诊断。

#### 3.3.4 内核 ↔ 用户的数据拷贝

内核地址 = 物理地址（靠 DMW0），用户地址是虚拟地址（要查页表）。所以"把用户传进来的指针里的数据读出来"不能直接解引用，得**先按用户的页表把虚拟地址翻译成物理地址，再读**。

`uvm_la.c` 提供了这套安全拷贝函数：

| 函数 | 方向 | 用途 |
| --- | --- | --- |
| `la_copy_from_user(kdst, usrc, len)` | 用户 → 内核 | 读用户数据（如 read 的缓冲区指针） |
| `la_copy_to_user(udst, ksrc, len)` | 内核 → 用户 | 写数据给用户（如 write 回填） |
| `la_copy_str_from_user` | 用户 → 内核 | 读用户传的字符串（如 open 的路径） |
| `la_uva_to_pa` | — | 单次 VA→PA 翻译（核心原语） |

这套函数是系统调用能安全运行的基础，几乎所有带指针参数的 syscall 都靠它。

### 3.4 地址空间布局（`early_boot.h`）

把 512GB 虚拟地址空间切成几段，互不干扰：

```
高地址
  0x7FFFFFE000  ┌───────────────┐  ← 用户栈顶 LA_USER_STACK（往下长）
                │   用户栈        │   exec 预分配 8 页，不够时按需向下扩
                ├───────────────┤
                │   （空洞）      │
  0x4000000000  ├───────────────┤  ← mmap 区起点 LA_MMAP_BASE（往上长）
                │   mmap 区      │   与堆分离！避免踩 musl malloc 元数据
                ├───────────────┤
                │   （空洞）      │
                ├───────────────┤  ← heap_top（brk 堆，往上长）
                │   brk 堆       │   exec 初始化为"最高 LOAD 段末尾对齐"
                ├───────────────┤
        0x1000  └───────────────┘  ← 用户代码最低地址 LA_USER_BASE
低地址
```

> **堆（brk）和 mmap 必须分开**，是血泪教训：早期 mmap 从 `heap_top` 开始分配，结果和 brk 堆打架，把 musl malloc 的内部元数据踩坏了，程序莫名崩溃。修复后 mmap 搬到独立的高地址区。

---

## 4. 进程与调度：`proc.c` / `proc.h` / `swtch.S`

### 4.1 进程是什么：一个结构体

进程在内核眼里，就是 `struct la_proc`（`proc.h`）这张"档案卡"：

```c
struct la_proc {
    int pid;                   // 进程号
    enum la_proc_state state;  // 当前状态（见下）
    char name[16];             // 名字

    uint64_t kstack;           // 内核栈地址（每进程 1 页）
    struct la_context ctx;     // 内核上下文（切换时存的寄存器）

    // —— 用户态专属 ——
    struct la_trap_frame *tf;  // 陷阱帧（用户寄存器快照）
    uint64_t *pgtbl;           // 它的页表根
    uint64_t heap_top;         // 堆顶
    uint64_t mmap_top;         // mmap 区顶
    uint64_t stack_bottom;     // 栈底（往下扩的边界）
    int is_user;               // 1=用户进程

    // —— 关系 ——
    int parent_pid;            // 父进程 pid
    int exit_code;             // 退出码（给 wait 用）

    struct la_fd fds[32];      // 文件描述符表（每进程最多 32 个 fd）
    uint32_t cwd_ino;          // 当前工作目录的 inode 号
    uint64_t entry;            // 内核线程入口（仅内核线程用）
};
```

进程状态是个小状态机（`enum la_proc_state`）：

```
        ┌──────────┐  被调度选中   ┌──────────┐
   ───► │ RUNNABLE │ ──────────► │ RUNNING  │
        └──────────┘              └────┬─────┘
            ▲                          │ 时间片到/主动让出
            │                          ▼
            │                     ┌──────────┐
            └──────────────────── │ (yield)  │ 回 RUNNABLE
                                  └──────────┘
            ┌──────────┐  wait 等 IO   ┌──────────┐
            │ SLEEPING │ ◄─────────── │ RUNNING  │
            └────┬─────┘   被唤醒     └──────────┘
                 │
        exit()   │
                 ▼
            ┌──────────┐  父进程 wait 回收  ┌──────────┐
            │  ZOMBIE  │ ────────────────► │  UNUSED  │（槽位归还）
            └──────────┘                   └──────────┘
```

进程表是个定长数组 `la_procs[16]`（`LA_NPROC = 16`）。要跑 unixbench 这种大量 fork 的测试，这个数以后要加大（见路线图 Step 19）。

### 4.2 文件描述符（fd）

每个进程有 32 个 fd 槽（`struct la_fd`），每个记录：

```c
struct la_fd {
    uint32_t ino;      // 文件的 inode 号（0=空闲）
    uint32_t offset;   // 读写位置
    int type;          // CONSOLE(串口) / FILE(磁盘文件) / UNUSED
    int writable;      // 是否可写
};
```

`fd=0/1/2` 默认是 stdin/stdout/stderr → 指向串口（CONSOLE）。测试程序 `write(1, ...)` 就是往 fd=1 写，最终吐到串口给评测机判分。

### 4.3 调度器：`la_scheduler`

一个**轮询（round-robin）死循环**，永不返回：

```
loop:
  开中断
  从上次位置开始，在进程表里找下一个 RUNNABLE 的进程 p
  if 没找到:
      执行 idle 0   # 空转等中断（省电/让出 CPU）
      continue
  关中断
  p->state = RUNNING
  设好 p 的内核栈指针、页表
  la_swtch(从调度器切到 p)      ← 切过去跑 p，p 让出时再切回来
  回到调度器：如果 p 已是 ZOMBIE 且无父进程，回收它
```

> 类比：幼儿园老师（调度器）按花名册轮流派小朋友（进程）玩玩具（CPU）。没小朋友想玩就歇着（idle），有人玩完（yield/exit）就回来接着派下一个。

### 4.4 上下文切换：`swtch.S`

切换进程本质上就是**换一套寄存器**。`la_swtch(old, new)` 只保存/恢复**被调用方保存寄存器**（`ra/sp/fp/s0~s8`），因为别的寄存器调用方自己会管：

```asm
la_swtch:
    # 把当前 ra/sp/fp/s0~s8 存到 old 上下文
    st.d $r1, $a0, LA_CTX_RA
    ...
    # 从 new 上下文恢复 ra/sp/fp/s0~s8
    ld.d $r1, $a1, LA_CTX_RA
    ...
    jr $ra    # 跳到 new 上下文里存的 ra（即"切过去"）
```

> 关键点：**内核线程之间的切换只换这些寄存器；用户进程的寄存器则是靠 trap frame 保存/恢复**（见第 5 章）。两者是不同机制，别混淆。

### 4.5 进程的诞生与死亡

**创建**（`la_proc_create_kthread` / `la_proc_create_user`）：从进程表占一个空槽，分配 1 页内核栈，把 `ctx.ra` 设成一个"引导函数"：

- 内核线程 → `ctx.ra = la_proc_bootstrap`（第一次被调度时，跑它的入口函数）
- 用户进程 → `ctx.ra = la_proc_user_bootstrap`（第一次被调度时，设好栈/页表，再 `la_proc_return` 跳用户态）

**进入用户态**（`la_proc_return`）是关键仪式：

```
1. la_uvm_switch(p->pgtbl)     切到该进程页表
2. la_tlb_inval_all()          清空所有 TLB（坑：全局 TLB 无 ASID，见第11章）
3. la_tlb_fill_all(p->pgtbl)   预填该进程所有映射进 TLB
4. 设 CRMD: DA=0, PG=1         关闭直接映射、开启分页
5. la_user_return(tf)          恢复用户寄存器、ertn 真正跳到用户态
```

**死亡**（`la_proc_exit`）：标记 ZOMBIE、唤醒等待的父进程、切回调度器。有个**极易踩的坑**：如果是从 TLB 未命中异常路径调用 exit，必须手动**清掉 `ISTLBR` 位**（`TLBRERA & 1`），否则下一个进程一进异常就被错误地当成"TLB 未命中"处理，系统雪崩。这个细节在源码注释里写得很重。

---

## 5. 异常、中断、系统调用：`trap.c` / `trap_entry.S` / `userret.S` / `timer.c`

这章讲"CPU 为什么会突然跳进内核，以及进来之后怎么办"。

### 5.1 三种"打断"

| 类型 | 何时发生 | 典型例子 |
| --- | --- | --- |
| **中断（Interrupt）** | 外部硬件"喊"CPU | 定时器心跳、磁盘完成 |
| **异常（Exception）** | CPU 执行某条指令时出错 | 访问非法地址、非法指令 |
| **系统调用（Syscall）** | 用户程序**主动**请求 | `syscall` 指令（ecode=0xB） |

三者都让 CPU 自动跳到 `EENTRY` 指向的入口（本项目都是 `la_exception_entry`），区别在 `ESTAT` 里的原因码（ecode）。

### 5.2 异常入口：`trap_entry.S::la_exception_entry`

这是汇编写的"异常接待处"，做 4 件事：

```
1. 存两个临时寄存器到 CSR.KS0/KS1（待会儿还要用它们）
2. 判断是不是 TLB 未命中（TLBRERA 的 ISTLBR 位）：
     若是，把 TLBRPRMD 复制到 PRMD（让后面能用 PRMD 统一判断来源）
3. 判断异常来自哪里（读 PRMD.PPLV）：
     = 0 → 来自内核：SP 不变，直接在当前栈上开 trap frame
     = 3 → 来自用户：SP 换成该进程的内核栈（la_trap_ksp），再开 trap frame
4. 把全部 32 个通用寄存器 + ERA/BADV/ESTAT 存进 trap frame
   → 调用 la_trap_dispatch(tf)
   → 返回时反向恢复寄存器，ertn 回去
```

**trap frame**（`struct la_trap_frame`，288 字节）就是"用户被打断那一瞬间的完整现场快照"：32 个寄存器 + 出错 PC + 出错地址 + 原因码。系统调用的返回值也是写回 `tf->gpr[A0]`，恢复时带回用户态。

> 为什么来自用户态要换内核栈？因为用户态的 `$sp` 指向用户栈，内核不能在用户栈上干活（不安全、也放不下）。`la_trap_ksp` 这个全局变量专门存"当前用户进程的内核栈顶"，调度器每次切进程前会更新它。

### 5.3 分发：`trap.c::la_trap_dispatch`

拿到 trap frame 后，按优先级分流：

```
1. 是 TLB 未命中？(ISTLBR)
     ├─ la_tlb_refill_one(badv) 走页表填 TLB → 成功就返回（注意：不清 ISTLBR！）
     │     （清了会让 ertn 走错返回路径，导致分页没重新开 → 全是 ADEF）
     ├─ 失败 → 试 la_uvm_grow_stack（是不是栈不够用了？）→ 再 refill
     └─ 还失败 → 用户进程就 kill 它（segv），内核就 HALT

2. 是中断？(ecode=0)
     └─ 定时器位？→ la_timer_interrupt()；否则报告未知中断

3. 是系统调用？(ecode=0xB)
     └─ ret = la_syscall_dispatch(tf)
        tf->gpr[A0] = ret        # 返回值放 a0
        tf->era += 4             # PC 跳过 syscall 指令（否则死循环）

4. 都不是 → 不可恢复异常 → panic（用户进程 kill，内核 HALT）
```

> 注意 `trap.c` 里有大量 `la_uart_puts` 的**诊断输出**（读出错指令、查 TLB 项、审计物理页别名等）——这些是排查 mallocng 崩溃时加的"法医工具"，生产路径上其实应该精简。

### 5.4 返回用户态：`userret.S::la_user_return`

```asm
la_user_return:
    设 ERA = tf 里存的 PC
    设 PRMD = 0x7    # PPLV=3（回用户态）+ PIE=1（开中断）
    恢复全部通用寄存器（a0 最后恢复，避免被覆盖）
    ertn             # 这条指令原子地：切到 PLV3 + 恢复 PC + 恢复中断状态
```

`ertn`（exception return）是"从异常返回"的特权指令，是内核→用户的唯一合法通道。

### 5.5 时钟：`timer.c`

QEMU 提供一个 100MHz 的稳定定时器。`la_timer_init` 把它配成 **100Hz（每秒 100 次）周期中断**：

```c
tcfg = EN | PERIODIC | (interval << 2);   // interval = 100MHz/100 = 100万
```

每次中断 → `la_timer_interrupt`：清 pending（写 TICLR）、`ticks++`、每秒打一次心跳。

⚠️ **当前局限**：定时器目前**只计数、不抢占**（路线图 Step 12 待做）。也就是说长任务会独占 CPU，直到它主动让出。要实现"时间片到了强制换人"，需在中断里加 tick 计数 + 调用 `la_proc_yield`。

---

## 6. 加载程序：`exec_la.c`

### 6.1 ELF 是什么

Linux/LoongArch 上的可执行文件是 **ELF 格式**。它的核心是若干 **PT_LOAD 段**：每段说"把文件里 offset 处、file_sz 字节的内容，加载到虚拟地址 vaddr，占内存 mem_sz 字节"。

`la_do_exec_syscall`（exec 系统调用的实现）就是干这个的。

### 6.2 exec 的完整步骤

```
1. 解析路径（相对路径从 cwd 起查，见 §8）
2. 读文件头，判断是不是脚本？
     └─ 若以 "#!" 开头（shebang）：取出解释器路径（如 /musl/busybox），
        重建 argv = [解释器, 脚本路径, 原argv...]，改去加载解释器
3. 验证 ELF 头（魔数、机器类型=LoongArch）
4. 建新页表，遍历每个 PT_LOAD 段：
     a. 按 vaddr 逐页分配物理页 + 建映射（权限 LA_PTE_U_RWX 等）
     b. BSS 区（mem_sz > file_sz 的部分）必须清零  ← 坑：不清零 musl malloc 会崩
     c. 从文件把段内容读进这些页
5. 分配用户栈（LA_USER_STACK 往下，预分配 8 页，清零）
6. 在栈上搭好：argc、argv 指针数组、envp、auxv（辅助向量）
7. 构造 trap frame：era=程序入口，sp=栈顶，a0=argc，a1=argv
8. 切到新地址空间、la_proc_return 跳用户态
```

### 6.3 auxv 与 AT_RANDOM

**auxv（auxiliary vector，辅助向量）** 是内核塞给程序启动时的一组"运行时情报"：入口地址、页大小、随机数等。其中 **AT_RANDOM** 是个随机字节串，musl 的 malloc 用它派生内部密钥。

⚠️ 坑：AT_RANDOM 的摆放位置有讲究——不能放在 ELF loader 会"顺手解析"的地方，否则 musl 读错情报 → mallocng 一致性检查失败 → busybox 一启动就 abort。

### 6.4 shebang：让脚本能"执行"

测试脚本是 `*_testcode.sh`，本质是文本。直接当 ELF 加载必失败。exec 识别首行 `#!`，把脚本交给 `/musl/busybox sh` 解释执行——这是当前主线（busybox 是**静态链接**，不需要动态链接器）。

> 动态链接（dhry2 那种带 `PT_INTERP` 的）还没实现，是路线图 Step 13。

### 6.5 一个反复踩的坑：4KB 内核栈

`la_do_exec_syscall` 里构造 argv 的三个大数组，最初是栈上局部变量，加起来 ~4.5KB，超过了内核栈的 1 页（4KB）→ 第二次 exec 时取指 ADEF 崩溃。修复：改成 `static`（放 BSS，不占栈）。

> 教训：**内核栈非常小（本项目就 1 页），永远别在内核函数里开大数组。**

---

## 7. 系统调用：`syscall.c`

### 7.1 分发

`la_syscall_dispatch(tf)`：从 `tf->gpr[A7]` 取系统调用号，用 `switch` 分发到具体实现；参数从 `a0~a5` 取；返回值写回 `a0`。

```
号表 = Linux 通用 ABI（LA 与 RISC-V 共用同一套号）
```

### 7.2 错误返回约定（极重要）

**失败时返回 `(uint64_t)(-errno)`，不是返回 -1。** 比如 `open` 失败要返回 `-ENOENT` 而不是 `-1`。

为什么这么较真？因为 musl 的 syscall 封装会把返回值当 `unsigned` 看：如果你返回 `-1`，musl 会把它解释成 errno=1（EPERM），**把"文件不存在"误报成"权限不够"**，彻底掩盖真因。

### 7.3 已实现的 syscall（约 45 个）

| 类别 | 代表 syscall |
| --- | --- |
| 进程 | `fork wait waitid exit exit_group getpid gettid getppid getcwd exec clone` |
| 文件 I/O | `open close read write lseek dup dup3 fstat get_dentries ioctl newfstatat faccessat readlinkat fcntl` |
| 目录 | `chdir mkdir` |
| 内存 | `brk mmap munmap mprotect` |
| 信号 | `rt_sigaction rt_sigprocmask`（桩） |
| 身份 | `getuid geteuid getgid getegid`（都返回 0=root） |
| 线程 | `set_tid_address set_robust_list` |
| 信息 | `statx uname` |
| 其他 | `msync pipe2 sched_yield setrlimit getrlimit prlimit64 shutdown` |

其中 `pipe2` 目前是桩（环形缓冲没实现，路线图 Step 14）；socket 族（`connect` 等 0x42/0x71/0xa9）还没实现（路线图 Step 16，当前阻塞点之一）。

### 7.4 两个对正确性至关重要的细节

- **openat ABI**：`open` 在 Linux 实际是 `openat(dirfd, path, ...)`，第一个参数是 `dirfd` 不是路径。早期误把 `a0` 当路径 → EPERM。修复后按 openat 语义处理。
- **cwd 同步**：syscall 入口会把当前进程的 `cwd_ino` 同步到全局 `la_fs_cwd_ino`，这样文件系统层（`fs_la.c`）解析相对路径时能拿到正确的起点。

---

## 8. 文件系统与块设备：`fs_la.c` / `virtio_la.c`

### 8.1 块设备驱动：`virtio_la.c`

虚拟磁盘 `sdcard-la.img` 通过 **VirtIO**（一种"半虚拟化"高性能设备接口）挂载。驱动流程：

```
初始化 la_virtio_init():
  1. 扫描 PCI 总线（用 ECAM 配置空间 @0x20000000），找 vendor=0x1AF4 的设备
  2. 启用设备（总线主控 + 内存映射）
  3. BAR 地址映射（把设备的寄存器映射到 CPU 地址空间）
  4. VirtIO 标准握手：ACK → DRIVER → 协商特性 → 配队列 → DRIVER_OK

读一个块 la_virtio_blk_read(块号, buf):
  1. 组装 3 个描述符：[请求头(读哪个块/什么操作)] → [数据缓冲] → [状态字节]
  2. 把描述符链挂到 available 环，写环索引
  3. 写 QUEUE_NOTIFY 通知设备"有活干了"
  4. 轮询 used 环，直到设备把结果放进来
```

⚠️ **两个反复踩的可靠性坑**（都在 `CLAUDE.md` §5 记录）：

1. **必须 `dbar 0`（数据屏障）**：CPU 缓存和 DMA 写之间要同步，否则 CPU 读到旧数据。
2. **轮询时必须周期性重写 `QUEUE_NOTIFY`**：QEMU 单 CPU 下，纯 RAM 死循环轮询不会让出 vCPU 给设备层处理 → 死锁。解法是每轮询 `0x3fff` 次就重写一次通知寄存器，逼设备推进；再配大超时（`50_000_000`）。

> 没有这两个处理，VirtIO 读取会"随机超时"，是本项目最早期的玄学问题之一。

### 8.2 文件系统：`fs_la.c`（只读）

`la_fs_init` 读第 0 扇区，**按 magic 自动识别两种格式**：

- **SeaFS**（magic `0x12341234`）：项目自带的简易格式，用于 `disk.img` 冒烟测试。
- **EXT4**（magic `0xEF53`，位于超级块偏移 0x438）：评测镜像 `sdcard-la.img` 就是 ext4。

对外 API 统一（`la_fs_lookup` / `la_fs_read_file` / `la_fs_get_dentries` 等），内部按当前格式走不同实现：

**EXT4 读取一个文件的流程**（ SeaFS 类似但更简单）：

```
路径 "/musl/busybox"
  ↓ la_fs_lookup 逐级解析（'/' 分隔，每级查目录）
根目录 inode → 目录项里找 "musl" → 拿到 musl 的 inode
musl 目录项里找 "busybox" → 拿到 busybox 的 inode
  ↓ la_fs_read_file
读 busybox 的 inode → 通过 extent 树把"逻辑块号"映射成"物理块号"
  ↓ 逐块 la_virtio_blk_read 读出数据
```

EXT4 用 **extent 树**（而非老的间接块索引）记录文件块映射，支持多级索引 + 叶子 extent，比传统 inode 高效。本驱动实现了只读解析（含目录项按 `rec_len` 线性扫描）。

> 文件系统目前**只读**，且**无缓存**（每次都直接读盘）。可写（memfs）和块缓存是路线图 Step 15 / 20。

---

## 9. 第一个用户进程：`initcode_la.h`

内核启动后跑的第一个用户程序，不是磁盘上的文件，而是**内嵌在内核里的一段 C 源码**（编译成 ELF 后以头文件形式链进内核，见 `initcode_la.h`）。它干的事：

```
initcode 的 main:
  1. 扫描文件系统，找形如 *_testcode.sh 的脚本
     （/musl/、/glibc/ 下的 oscomp 测试脚本）
  2. 对每个脚本：
     fork()  → 子进程 exec(脚本)   # exec 识别 shebang，交给 busybox sh 解释
     父进程 wait4() 回收子进程     # 拿到退出码
  3. 继续下一个脚本
```

它本质上是评测流程的"发动机"：评测机只负责启动内核 + 挂镜像，**剩下"找脚本、跑脚本、收结果"全靠 initcode 驱动**。脚本里被测程序用 `write(1, ...)` 把评测标记打到串口，评测机据此判分。

---

## 10. 全链路串联：一次测试脚本执行的完整旅程

把前面所有章节缝起来。以跑 `/musl/libcbench_testcode.sh` 为例：

```
① 上电
   entry.S(_entry @0x200000) → la_boot_main
   依次点亮 pmem/kvm/paging/tlb/timer/proc/中断/virtio/fs
   la_proc_make_first() 创建 initcode 进程（pid=1）
   la_scheduler() 进死循环

② 调度器选中 initcode
   设好它的内核栈、页表 → la_swtch 切过去
   → la_proc_user_bootstrap → la_proc_return
   → 切页表、清TLB、填TLB、DA=0/PG=1 → la_user_return → ertn
   → CPU 进入 PLV3，从 initcode 的 main 开始执行用户代码

③ initcode 扫到 libcbench_testcode.sh
   发起 fork() 系统调用：
     syscall 指令 → ecode=0xB 异常 → la_exception_entry
     → 换内核栈、存 trap frame → la_trap_dispatch
     → 分发到 sys_fork（深拷贝页表、造子进程）
     → 返回值写 tf->a0，era+=4 → ertn 回用户态

④ 子进程 exec(脚本)
   exec 系统调用 → la_do_exec_syscall
   → 识别 #! → 改成加载 /musl/busybox，argv=[busybox, sh, 脚本]
   → 建新页表、加载 busybox 的 PT_LOAD 段、清 BSS
   → 搭栈（argc/argv/auxv）→ 切地址空间 → 跳 busybox 入口

⑤ busybox 解释执行脚本
   脚本里 ./busybox echo "#### OS COMP TEST GROUP START ... ####"
   → write(1, 标记) 系统调用
   → 内核 sys_write：fd=1 是 CONSOLE → la_uart_putc 逐字节打串口
   → 评测机抓到这条标记

⑥ 脚本继续 exec ./libc-bench（相对路径从 cwd 解析）
   → 遇到阻塞点：
     - libc-bench 要 ~80KB 栈，但 exec 只预分配 8 页(32KB)
       → 栈区缺页 → TLB 未命中 → la_uvm_grow_stack 自动扩栈 ✓（已实现）
     - libc-bench 调 socket 族 syscall → "syscall: UNKNOWN #N" ✗（未实现，阻塞）

⑦ 子进程结束
   exit() → la_proc_exit（清 ISTLBR、标 ZOMBIE、唤醒父进程）
   initcode 的 wait4() 回收它，继续下一个脚本
```

这条链路里**任何一环断了**，测试就跑不通。当前已通到 ⑥ 的扩栈，阻塞在 socket 族 syscall。

---

## 11. 关键设计决策与踩过的坑（精华）

> 这一章是"为什么代码写成这样"的答案集合，建议反复读。

### 11.1 TLB 全局无 ASID → 每次切地址空间必须全清

**现象**：exec 后，旧程序（如 initcode）残留的 TLB 表项还在，新程序（busybox）复用了同样的虚拟地址，CPU 可能命中旧表项 → store 落到错误物理页 → musl malloc 元数据损坏 → busybox 启动即 abort（`a_crash`）。

**根因**：所有叶 PTE 带 **G（全局）位、无 ASID**，TLB 表项**不按进程区分**。又因 LA 的 TLB 按 VPPN（even/odd 对）索引，跨地址空间必然别名。

**修复**（`proc.c` / `uvm_la.c`）：每次进入用户地址空间前，`la_tlb_inval_all()` 全清，再 `la_tlb_fill_all()` 按新页表重填；每次新映射后 `la_tlb_inval_page(va)` 清对应 even/odd 对。

### 11.2 ISTLBR 位必须正确处理

`ISTLBR`（`TLBRERA` bit0）标记"当前是不是在 TLB 未命中路径里"。它只能被 `ertn` 硬件清除。如果内核在 refill 路径里**切走进程却没 ertn**，`ISTLBR` 残留 → 下一个进程一进异常被误当 TLB 未命中 → 雪崩。

因此 `la_proc_exit` 里有 `la_csr_write(... & ~1ULL, TLBRERA)` 手动清位；refill 成功后**故意不清** `ISTLBR`（让 `ertn` 走 TLB 返回路径自动重开分页）。

### 11.3 目录项无标志位

HPTW 不会剥标志位。目录项带标志 → 地址被 OR 脏 → 翻译出错。约定：目录项只存裸物理地址。

### 11.4 HPTW 是性能关键

不开 HPTW，busybox 这种大程序会产生几百万次 TLB refill 异常，慢到不可用。`paging_init` 开 `PWCH.bit24` 是必须的。

### 11.5 errno 返回 -(errno)，不是 -1

见 §7.2。返回 -1 会让 musl 误报 EPERM，掩盖真因。

### 11.6 brk 堆与 mmap 区必须分离

见 §3.4。混在一起会踩 musl malloc 元数据。

### 11.7 exec 的 argv 数组必须 static

见 §6.5。内核栈 4KB 撑不住大局部数组。

### 11.8 BSS 必须清零

见 §6.2。残留数据被 musl 当 malloc 块头 → 崩。

### 11.9 VirtIO 读取要 dbar + 周期重写 QUEUE_NOTIFY

见 §8.1。否则随机超时死锁。

### 11.10 栈自动增长

exec 只预分配 8 页栈，重栈程序（libc-bench ~80KB）会缺页。`la_uvm_grow_stack` 在 trap 里按需向下扩页（上限 `LA_MAX_STACK_PAGES=512` 页），匹配 Linux 语义。

---

## 12. 当前状态、阻塞点与后续路线（速览）

- **已跑通**：启动→用户态→syscall→返回全链路；VirtIO 稳定读；ext4 只读挂载；initcode 找到并 exec 脚本；busybox sh 解释执行、echo 打标记、wait4 回收、相对路径 exec；栈自动增长。
- **当前阻塞点**：socket 族 syscall（libc-bench 调 `connect` 等）未实现 → `UNKNOWN #N`。
- **后续路线**（详见 `CLAUDE.md` §7）：Step 12 定时器抢占 → Step 14 管道 → Step 15 文件写入 → Step 16 补全 syscall → Step 13 动态链接 → Step 18~21 堆增强/资源回收/缓存/集成验证。

---

## 13. 术语小词典

| 术语 | 解释 |
| --- | --- |
| **PLV0 / PLV3** | LoongArch 特权级：0=内核态，3=用户态 |
| **CSR** | 控制状态寄存器，CPU 的"开关面板" |
| **PA / VA** | 物理地址 / 虚拟地址 |
| **页表 / PTE** | VA→PA 翻译表 / 表项 |
| **TLB** | 地址翻译的硬件缓存 |
| **HPTW** | 硬件页表遍历器，自动走页表填 TLB |
| **DMW0** | 直接映射窗口，让内核低地址=物理地址 |
| **STLB / MTLB** | 大 TLB(2048项) / 小 TLB(64项) |
| **trap frame** | 异常时用户寄存器的完整快照 |
| **ecode** | 异常原因码（ESTAT 里） |
| **ISTLBR** | "当前在 TLB 未命中路径"标志位 |
| **ELF** | 可执行文件格式 |
| **PT_LOAD / shebang** | ELF 可加载段 / 脚本首行 `#!` 解释器声明 |
| **auxv / AT_RANDOM** | 内核给程序的启动情报 / 其中一项随机数 |
| **extent 树** | EXT4 记录文件块映射的数据结构 |
| **fd** | 文件描述符，进程打开文件的句柄 |
| **inode** | 文件系统里文件的编号/元数据 |

---

## 14. 给小白的上手建议

1. **先跑起来再说**：按 `CLAUDE.md` §3 在 Docker 容器里 `make build-la`，再用 `sdcard-la.img` 启动，亲眼看到串口输出。
2. **加一句自己的打印**：在 `boot.c::la_boot_main` 里加一句 `la_uart_puts("hello from me\n");`，重新构建运行，看到它出现——这一刻你就"摸到"内核了。
3. **顺着串口日志读代码**：日志里 `[init] xxx` 的顺序就是 `la_boot_main` 的执行顺序，对照着读最直观。
4. **挑一个小 syscall 实现**：比如 `getpid` 已经有了，照着它看 `syscall.c` 的分发结构，再尝试补一个简单的（如 `gettimeofday`）。
5. **遇到崩溃先看串口**：`badv=... pc=...` 告诉你出错地址，配合 `CLAUDE.md` §9 的反汇编技巧定位。

> 记住：内核开发就是"和硬件细节死磕 + 被自己的 bug 折磨 + 修好的瞬间极度爽快"的循环。这份文档是你的地图，但路得自己走一遍才真懂。祝顺利。
