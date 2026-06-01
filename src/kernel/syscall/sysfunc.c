#include "mod.h"

/*
    用户堆空间伸缩
    uint64 new_heap_top (如果是0, 代表查询当前堆顶位置)
    成功返回new_heap_top, 失败返回-1
*/
uint64 sys_brk()
{
    // push_off(); // 上锁防止时钟中断干扰页表打印（不加好像也行）

    uint64 new_top;
    arg_uint64(0, &new_top);

    proc_t *p = myproc();
    if (!p) return (uint64)-1;

    uint64 cur = p->heap_top;

    if (new_top == 0) { // look

    }else if (new_top > cur) { // grow
        uint32 len = (uint32)(new_top - cur);
        uint64 ret = uvm_heap_grow(p->pgtbl, cur, len, PTE_R | PTE_W | PTE_U);
        if (ret == (uint64)-1) return (uint64)-1;
        p->heap_top = ret;

    }else { // ungrow & stay
        uint32 len = (uint32)(cur - new_top);
        uint64 ret = uvm_heap_ungrow(p->pgtbl, cur, len);
        if (ret == (uint64)-1) return (uint64)-1;
        p->heap_top = ret;

    }
    // pop_off();
    return p->heap_top;
}

/*
    增加一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节,需检查是否是page-aligned)
    成功返回映射空间的起始地址, 失败返回-1
*/
uint64 sys_mmap()
{
    uint64 start; // 起始地址
    uint32 len;   // 地址范围
    arg_uint64(0, &start);
    arg_uint32(1, &len);

    // 检查长度是否有效
    if (len == 0) {
        printf("sys_mmap: len == 0\n");
        return (uint64)-1;
    }
    // 检查地址是否页对齐
    if (start % PGSIZE != 0) {
        printf("sys_mmap: start not page-aligned\n");
        return (uint64)-1;
    }
    if (len % PGSIZE != 0) {
        printf("sys_mmap: len not page-aligned\n");
        return (uint64)-1;
    }

    uint32 npages = len / PGSIZE;
    int perm = PTE_R | PTE_W | PTE_U; 

    uint64 ret_addr = uvm_mmap(start, npages, perm);

    // // 调试
    // proc_t *p = myproc();
    // printf("sys_mmap: start = %p, len = 0x%x, ret_addr = %p\n", (void *)start, len, (void *)ret_addr);
    // uvm_show_mmaplist(p->mmap);
    // vm_print(p->pgtbl);
    // printf("\n");

    return ret_addr;
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    uint64 start; // 起始地址
    uint32 len;   // 地址范围
    arg_uint64(0, &start);
    arg_uint32(1, &len);

    if (len == 0) return (uint64)-1;
    if (start % PGSIZE != 0 || len % PGSIZE != 0) return (uint64)-1;

    uint32 npages = len / PGSIZE;
    uvm_munmap(start, npages);

    // // 调试
    // proc_t *p = myproc();
    // printf("sys_munmap: start = %p, len = 0x%x\n", (void *)start, len);
    // uvm_show_mmaplist(p->mmap);
    // vm_print(p->pgtbl);
    // printf("\n");

    return 0;
}

/*
    进程复制
    返回子进程的pid
*/
uint64 sys_fork()
{
    return proc_fork();
}

/*
    等待子进程退出
    uint64 addr_exit_state
*/
uint64 sys_wait()
{
    uint64 addr_exit_state = 0;
    if (arg_raw(1) != 0 || arg_raw(2) != 0 || arg_raw(3) != 0) {
        arg_uint64(1, &addr_exit_state); // Linux wait4(pid, status, options, rusage)
    } else {
        arg_uint64(0, &addr_exit_state); // SeaOS wait(status)
    }
    return proc_wait(addr_exit_state);
}

/*
    进程退出
    int exit_code
    不返回
*/
uint64 sys_exit()
{
    int exit_code;
    arg_uint32(0, (uint32 *)&exit_code); // 获取退出码
    proc_exit(exit_code);
    return 0; // 不会执行到这里
}

/*
    让进程睡眠一段时间
    uint32 ntick (1个tick大约0.1秒)
    成功返回0
*/
uint64 sys_sleep()
{
    uint64 req = 0;
    uint64 rem = 0;
    if (arg_raw(1) != 0 || arg_raw(2) != 0) {
        arg_uint64(0, &req);
        arg_uint64(1, &rem);
        (void)rem;
        if (req == 0)
            return 0;
        uint64 ts[2] = {0, 0};
        uvm_copyin(myproc()->pgtbl, (uint64)ts, req, sizeof(ts));
        uint64 ntick = ts[0] * 10;
        if (ts[1] > 0)
            ntick += (ts[1] + 99999999ull) / 100000000ull;
        if (ntick == 0)
            ntick = 1;
        timer_wait(ntick);
        return 0;
    }

    uint32 ntick;
    arg_uint32(0, &ntick); // 获取睡眠的tick数
    timer_wait((uint64)ntick);
    return 0;
}

/*
    返回当前进程的pid
*/
uint64 sys_getpid()
{
    return (uint64)(myproc()->pid);
}

/*
    set_tid_address(int *tidptr)
    Linux 语义: 设置调用线程 clear_child_tid = tidptr, 返回调用者 TID。
    SeaOS 单线程/进程模型下 TID == PID。
    最小实现(docs/DECISIONS.md D3): 暂不存储 tidptr、不做退出时 clear_child_tid 清零+futex 唤醒,
    仅返回 pid 满足 musl 启动期。
*/
uint64 sys_set_tid_address()
{
    return (uint64)(myproc()->pid);
}

/*
    拉取调度统计快照
    uint64 buf_user (sched_stat_t*)
    uint32 max_entries
    返回实际写入条目数
*/
uint64 sys_schedstat()
{
    uint64 buf_user;
    uint32 max_entries;
    arg_uint64(0, &buf_user);
    arg_uint32(1, &max_entries);
    return (uint64)proc_schedstat(buf_user, max_entries);
}

/*
    执行ELF文件以替换当前进程的内容
    char *path
    char **argv
    成功返回argc, 失败返回-1
*/
uint64 sys_exec()
{
    char path[STR_MAXLEN + 1];
    uint64 argv_addr;
    uint64 envp_addr = 0;

    if (arg_raw(2) != 0) {
        arg_str(0, path, STR_MAXLEN);
        arg_uint64(1, &argv_addr);
        arg_uint64(2, &envp_addr);
        (void)envp_addr;
    } else {
        arg_str(0, path, STR_MAXLEN);
        arg_uint64(1, &argv_addr);
    }

    // 读取argv数组
    char *argv[32];
    int argc = 0;
    uint64 addr;
    proc_t *p = myproc();

    while (argc < 32) {
        uvm_copyin(p->pgtbl, (uint64)&addr, argv_addr + argc * sizeof(uint64), sizeof(uint64));
        if (addr == 0) break;
        argv[argc] = (char*)addr;
        argc++;
    }
    argv[argc] = NULL;

    // 复制argv到内核
    char *kargv[32];
    for (int i = 0; i < argc; i++) {
        kargv[i] = (char*)pmem_alloc(false);
        if (!kargv[i]) {
            for (int j = 0; j < i; j++) pmem_free((uint64)kargv[j], false);
            return -1;
        }
        uvm_copyin_str(p->pgtbl, (uint64)kargv[i], (uint64)argv[i], STR_MAXLEN);
    }
    kargv[argc] = NULL;

    int ret = proc_exec(path, kargv);

    // 释放内核argv
    for (int i = 0; i < argc; i++) {
        pmem_free((uint64)kargv[i], false);
    }

    return ret;
}

/* 构建fd->file的映射, 返回fd */
static uint32 alloc_fd(file_t *file)
{
    proc_t *p = myproc();
    for (uint32 i = 0; i < N_OPEN_FILE_PER_PROC; i++)
    {
        if (p->open_file[i] == NULL) {
            p->open_file[i] = file;
            return i;
        }
    }
    return -1;
}

/*
    打开或创建文件
    char *path
    uint32 open_mode
    成功返回fd, 失败返回-1
*/
uint64 sys_open()
{
    char path[STR_MAXLEN + 1];
    uint64 arg0 = arg_raw(0);
    uint64 arg1 = arg_raw(1);
    uint64 arg2 = arg_raw(2);
    uint64 arg3 = arg_raw(3);

    uint32 open_mode = 0;
    bool looks_like_openat = false;

    // 兼容两种调用形态：
    // 1) SeaOS 旧接口: open(path, mode)
    // 2) Linux openat: openat(dirfd, path, flags, mode)
    // 只有当参数形态明显像 openat 时才进入 openat 分支，避免被残留寄存器误导。
    if (((int64)arg0) <= 4096 && arg0 != 0) {
        looks_like_openat = true;
    }
    if (arg1 >= PGSIZE) {
        looks_like_openat = true;
    }
    if (looks_like_openat && (arg2 != 0 || arg3 != 0)) {
        uint64 dirfd;
        uint32 flags;
        uint32 mode;
        arg_uint64(0, &dirfd);
        arg_str(1, path, STR_MAXLEN);
        arg_uint32(2, &flags);
        arg_uint32(3, &mode);
        (void)dirfd;
        (void)mode;
        if ((flags & 3) == 0)
            open_mode |= FILE_OPEN_READ;
        if ((flags & 3) == 1)
            open_mode |= FILE_OPEN_WRITE;
        if ((flags & 3) == 2)
            open_mode |= FILE_OPEN_READ | FILE_OPEN_WRITE;
        if (flags & 64)
            open_mode |= FILE_OPEN_CREATE;
    } else {
        arg_str(0, path, STR_MAXLEN);
        arg_uint32(1, &open_mode);
    }

    file_t *file = file_open(path, open_mode);
    if (!file) return -1;

    uint32 fd = alloc_fd(file);
    if (fd == (uint32)-1) {
        file_close(file);
        return -1;
    }

    return fd;
}

/*
    关闭文件
    uint32 fd
    成功返回0, 失败返回-1
*/
uint64 sys_close()
{
    file_t *file;
    uint32 fd;
    if (arg_fd(0, &fd, &file) < 0) return -1;
    
    file_close(file);
    myproc()->open_file[fd] = NULL;
    
    return 0;
}

/*
    读取文件内容
    uint32 fd
    uint32 len
    uint64 addr
    成功返回读到的字节数, 失败返回0
*/
uint64 sys_read()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return 0;

    // Linux/RISC-V ABI: read(fd=a0, buf=a1, count=a2)
    uint64 addr;
    arg_uint64(1, &addr);   // a1 = buf
    uint32 len;
    arg_uint32(2, &len);    // a2 = count

    return file_read(file, len, addr, true);
}

/*
    写入文件内容
    uint32 fd
    uint32 len
    uint64 addr
    成功返回写入的字节数, 失败返回0
*/
uint64 sys_write()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return 0;

    // Linux/RISC-V ABI: write(fd=a0, buf=a1, count=a2)
    uint64 addr;
    arg_uint64(1, &addr);   // a1 = buf
    uint32 len;
    arg_uint32(2, &len);    // a2 = count

    return file_write(file, len, addr, true);
}

// 66 writev(fd, iovec*, iovcnt)：按 Linux ABI 逐段写出
uint64 sys_writev()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return (uint64)(-EBADF);
    uint64 iov    = arg_raw(1);          // 用户态 struct iovec[] 指针
    int    iovcnt = (int)arg_raw(2);
    if (iovcnt <= 0) return 0;

    uint64 total = 0;
    for (int i = 0; i < iovcnt; i++) {
        uint64 vec[2];                   // struct iovec { void* base; size_t len; } = 16B
        uvm_copyin(myproc()->pgtbl, (uint64)vec, iov + (uint64)i * 16, 16);
        uint64 base = vec[0];
        uint64 len  = vec[1];
        if (len == 0) continue;
        uint32 w = file_write(file, (uint32)len, base, true);
        total += w;
        if (w < len) break;              // 短写, 停止
    }
    return total;
}

// 94 exit_group：当前单线程, 等价于 exit
uint64 sys_exit_group()
{
    int exit_code;
    arg_uint32(0, (uint32 *)&exit_code);
    proc_exit(exit_code);
    return 0; // 不会执行到这
}

static void sbi_system_shutdown()
{
    register uint64 a0 asm("a0") = 0;           // reset_type: shutdown
    register uint64 a1 asm("a1") = 0;           // reset_reason: no reason
    register uint64 a6 asm("a6") = 0;           // fid
    register uint64 a7 asm("a7") = 0x53525354;  // EID "SRST"
    asm volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a6), "r"(a7) : "memory");

    while (1)
        asm volatile("wfi");
}

uint64 sys_shutdown()
{
    printf("sys_shutdown: powering off via SBI SRST\n");
    sbi_system_shutdown();
    return 0;
}

static int sys_spawn_and_wait(char *path, char **argv)
{
    printf("sys_spawn_and_wait: spawn request path=%s\n", path);
    int pid = proc_fork();
    printf("sys_spawn_and_wait: fork returned pid=%d\n", pid);
    if (pid < 0) {
        printf("sys_spawn_and_wait: fork FAILED\n");
        return -1;
    }

    /* 为子进程执行exec（在父进程上下文中替换子进程的地址空间） */
    if (pid > 0) {
        int eret = proc_exec_target(pid, path, argv);
        if (eret < 0) {
            printf("sys_spawn_and_wait: proc_exec_target failed for pid=%d\n", pid);
            /* 如果失败，proc_exec_target 已将子进程置为ZOMBIE并唤醒父进程 */
        }
    }

    printf("sys_spawn_and_wait: parent waiting for child pid=%d\n", pid);
    if (proc_wait(0) < 0) {
        printf("sys_spawn_and_wait: wait FAILED\n");
        return -1;
    }
    printf("sys_spawn_and_wait: child exited\n");
    return 0;
}

/*
    调整读写指针位置
    uint32 fd
    uint32 offset
    uint32 flag
    成功返回新的偏移量, 失败返回-1
*/
uint64 sys_lseek()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return -1;
    
    uint32 offset, flag;
    arg_uint32(1, &offset);
    arg_uint32(2, &flag);
    
    return file_lseek(file, offset, flag);
}

/*
    复制文件控制权
    uinr32 fd
    成功返回new_fd, 失败返回-1
*/
uint64 sys_dup()
{
    file_t *file;
    uint32 fd;
    if (arg_fd(0, &fd, &file) < 0) return -1;
    
    file_t *new_file = file_dup(file);
    if (!new_file) return -1;
    
    uint32 new_fd = alloc_fd(new_file);
    if (new_fd == (uint32)-1) {
        file_close(new_file);
        return -1;
    }
    
    return new_fd;
}

/*
    获取文件信息
    uint32 fd
    uint64 addr
    成功返回0, 失败返回-1
*/
uint64 sys_fstat()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return -1;

    uint64 addr;
    arg_uint64(1, &addr);

    return file_get_stat(file, addr);
}

/*
    获取目录中的所有目录项信息
    uint32 fd
    uint64 addr
    uint32 buffer_len
    成功返回读到的字节数, 失败返回-1
*/
uint64 sys_get_dentries()
{
    file_t *file;
    uint32 fd;
    if (arg_fd(0, &fd, &file) < 0) {
        return -1;
    }

    uint64 addr;
    arg_uint64(1, &addr);

    uint32 buffer_len;
    arg_uint32(2, &buffer_len);

    if (file == NULL) {
        return -1;
    }

    return file_read(file, buffer_len, addr, true);
}

/*
    创建目录
    char *path
    成功返回0, 失败返回-1
*/
uint64 sys_mkdir()
{
    char path[STR_MAXLEN + 1];
    if (arg_raw(1) != 0 || arg_raw(2) != 0) {
        uint64 dirfd;
        uint32 mode;
        arg_uint64(0, &dirfd);
        arg_str(1, path, STR_MAXLEN);
        arg_uint32(2, &mode);
        (void)dirfd;
        (void)mode;
    } else {
        arg_str(0, path, STR_MAXLEN);
    }

    inode_t *ip = path_create_inode(path,INODE_TYPE_DIR, 0, 0);
    if (!ip) return -1;

    inode_put(ip);
    return 0;
}

/*
    修改当前工作目录
    char *new_path
    成功返回0, 失败返回-1
*/
uint64 sys_chdir()
{
    char path[STR_MAXLEN + 1];
    arg_str(0, path, STR_MAXLEN);
    
    inode_t *ip = path_to_inode(path);
    if (!ip || ip->disk_info.type != INODE_TYPE_DIR) {
        if (ip) inode_put(ip);
        return -1;
    }
    
    inode_dup(ip);
    proc_t *p = myproc();
    if (p->cwd) inode_put(p->cwd);
    p->cwd = ip;
    
    return 0;
}

/*
    打印当前工作目录的绝对路径
    成功返回0, 失败返回-1
*/
uint64 sys_print_cwd()
{
    proc_t *p = myproc();
    if (!p->cwd) return -1;

    char path[STR_MAXLEN + 1];
    uint32 offset = inode_to_path(p->cwd, path, STR_MAXLEN + 1);
    if (offset == (uint32)-1) return -1;

    path[STR_MAXLEN] = '\0';
    printf("current work directory:%s\n", path + offset);
    return 0;
}

uint64 sys_spawn()
{
    char path[STR_MAXLEN + 1];
    arg_str(0, path, STR_MAXLEN);

    uint64 argv_addr;
    arg_uint64(1, &argv_addr);

    char *argv[32];
    int argc = 0;
    uint64 addr;
    proc_t *p = myproc();

    while (argc < 32) {
        uvm_copyin(p->pgtbl, (uint64)&addr, argv_addr + argc * sizeof(uint64), sizeof(uint64));
        if (addr == 0) break;
        argv[argc] = (char*)addr;
        argc++;
    }
    argv[argc] = NULL;

    char *kargv[32];
    for (int i = 0; i < argc; i++) {
        kargv[i] = (char*)pmem_alloc(false);
        if (!kargv[i]) {
            for (int j = 0; j < i; j++) pmem_free((uint64)kargv[j], false);
            return -1;
        }
        uvm_copyin_str(p->pgtbl, (uint64)kargv[i], (uint64)argv[i], STR_MAXLEN);
    }
    kargv[argc] = NULL;

    int ret = sys_spawn_and_wait(path, kargv);

    for (int i = 0; i < argc; i++) {
        pmem_free((uint64)kargv[i], false);
    }

    return ret;
}

/*
    新建链接
    char *old_path
    char *new_path
    成功返回0, 失败返回-1
*/
uint64 sys_link()
{
    char old_path[STR_MAXLEN + 1], new_path[STR_MAXLEN + 1];
    if (arg_raw(2) != 0 || arg_raw(3) != 0 || arg_raw(4) != 0) {
        uint64 olddirfd, newdirfd;
        uint32 flags;
        arg_uint64(0, &olddirfd);
        arg_str(1, old_path, STR_MAXLEN);
        arg_uint64(2, &newdirfd);
        arg_str(3, new_path, STR_MAXLEN);
        arg_uint32(4, &flags);
        (void)olddirfd;
        (void)newdirfd;
        (void)flags;
    } else {
        arg_str(0, old_path, STR_MAXLEN);
        arg_str(1, new_path, STR_MAXLEN);
    }

    return path_link(old_path, new_path);
}


/*
    删除链接 (可能触发删除文件)
    char *path
    成功返回0, 失败返回-1
*/
uint64 sys_unlink()
{
    char path[STR_MAXLEN + 1];
    if (arg_raw(1) != 0 || arg_raw(2) != 0) {
        uint64 dirfd;
        uint32 flags;
        arg_uint64(0, &dirfd);
        arg_str(1, path, STR_MAXLEN);
        arg_uint32(2, &flags);
        (void)dirfd;
        (void)flags;
    } else {
        arg_str(0, path, STR_MAXLEN);
    }

    return path_unlink(path);
}

// 174 getuid / 176 getgid：当前无多用户, 一律 root
uint64 sys_getuid() { return 0; }
uint64 sys_getgid() { return 0; }

// 135 rt_sigprocmask(how,set,oldset,sigsetsize)：暂不做信号, 返回成功
uint64 sys_rt_sigprocmask() { return 0; }

// 144 setgid / 146 setuid：单用户环境, 视作成功 no-op
uint64 sys_setgid() { return 0; }
uint64 sys_setuid() { return 0; }

// 79 newfstatat(dirfd, path, statbuf, flags)
uint64 sys_newfstatat()
{
    char path[STR_MAXLEN + 1];
    uint64 flags   = arg_raw(3);
    uint64 statbuf = arg_raw(2);
    arg_str(1, path, STR_MAXLEN);

    // AT_EMPTY_PATH(0x1000): 空路径 → 直接 stat dirfd 指向的文件(musl 的 fstat 走这条)
    if (path[0] == '\0' && (flags & 0x1000)) {
        file_t *file;
        if (arg_fd(0, NULL, &file) < 0) return (uint64)(-EBADF);
        return file_get_stat_linux(file, statbuf);
    }
    // 否则按路径打开后 stat(dirfd 暂按 cwd/绝对路径处理, 与现有 openat 一致)
    file_t *file = file_open(path, FILE_OPEN_READ);
    if (!file) return (uint64)(-ENOENT);
    uint64 r = file_get_stat_linux(file, statbuf);
    file_close(file);
    return r;
}

// 25 fcntl(fd, cmd, arg)：实现 F_DUPFD + 文件描述符/状态标志的常见命令
uint64 sys_fcntl()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return (uint64)(-EBADF);
    int cmd = (int)arg_raw(1);
    switch (cmd) {
        case 0:      // F_DUPFD
        case 1030: { // F_DUPFD_CLOEXEC：暂不区分 cloexec, 直接复制 fd
            file_t *nf = file_dup(file);
            if (!nf) return (uint64)-1;
            uint32 nfd = alloc_fd(nf);
            if (nfd == (uint32)-1) { file_close(nf); return (uint64)-1; }
            return nfd;
        }
        case 1: return 0;     // F_GETFD：无 cloexec 跟踪, 返回 0
        case 2: return 0;     // F_SETFD：忽略, 成功
        case 3: return 2;     // F_GETFL：返回 O_RDWR(2)
        case 4: return 0;     // F_SETFL：忽略, 成功
        default: return 0;    // 其它命令暂作成功处理
    }
}

// 134 rt_sigaction：暂不实现真实信号, 一律成功(避免 sh 报 Function not implemented)
uint64 sys_rt_sigaction() { return 0; }

// 160 uname：填 struct utsname(6 × 65 字节字段)
uint64 sys_uname()
{
    uint64 addr = arg_raw(0);
    struct {
        char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
    } u;
    memset(&u, 0, sizeof(u));
    memmove(u.sysname,  "SeaOS",   6);
    memmove(u.nodename, "seaos",   6);
    memmove(u.release,  "6.1.0",   6);   // 给个较新的内核版本号, 规避部分版本检查
    memmove(u.version,  "SeaOS",   6);
    memmove(u.machine,  "riscv64", 8);
    uvm_copyout(myproc()->pgtbl, addr, (uint64)&u, sizeof(u));
    return 0;
}

// 173 getppid：返回父进程 pid(无父则 1)
uint64 sys_getppid()
{
    proc_t *p = myproc();
    if (p && p->parent) return (uint64)p->parent->pid;
    return 1;
}