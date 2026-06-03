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
    mmap(addr, length, prot, flags, fd, offset)
    成功返回映射空间的起始地址, 失败返回(uint64)-1
*/
uint64 sys_mmap()
{
    uint64 start;
    uint64 len;
    uint64 prot;
    uint64 flags;
    arg_uint64(0, &start);
    arg_uint64(1, &len);
    arg_uint64(2, &prot);
    arg_uint64(3, &flags);
    // a4=fd, a5=offset 仅文件映射需要，匿名映射忽略

    if (len == 0)
        return (uint64)-1;
    if (start != 0 && start % PGSIZE != 0)
        return (uint64)-1;

    uint64 aligned_len = (len + PGSIZE - 1) & ~(PGSIZE - 1);
    uint32 npages = aligned_len / PGSIZE;

    // 根据 prot 设置 PTE 权限
    // PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4
    int perm = PTE_U;
    if (prot & 1) perm |= PTE_R;
    if (prot & 2) perm |= PTE_W;
    if (prot & 4) perm |= PTE_X;
    // 若 prot=PROT_NONE(0) 或未设置读权限，给最小读权限避免 musl 访问头部失败
    if (!(perm & (PTE_R | PTE_W | PTE_X)))
        perm |= PTE_R;

    uint64 ret_addr = uvm_mmap(start, npages, perm);
    printf("sys_mmap: start=%p len=0x%llx prot=%llu flags=%llu npages=%u ret=%p\n",
           (void*)start, (unsigned long long)len, (unsigned long long)prot,
           (unsigned long long)flags, (unsigned)npages, (void*)ret_addr);
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

uint64 sys_clone()
{
    // Linux/RISC-V clone(flags=a0, stack=a1, parent_tid=a2, tls=a3, child_tid=a4)
    // musl fork() => clone(SIGCHLD=0x11, 0, ...): 复制地址空间, 子返回0, 父返回子pid
    uint64 flags = arg_raw(0);
    uint64 stack = arg_raw(1);

    // 仅支持 fork 语义。CLONE_VM(0x100)=共享地址空间(线程)、或指定新栈, 暂不支持。
    if ((flags & 0x100) || stack != 0) {
        printf("sys_clone: unsupported flags=%p stack=%p -> -ENOSYS\n",
               (void *)flags, (void *)stack);
        return -ENOSYS;
    }

    return proc_fork();   // 子 a0 已置0、epc+4; 父返回子 pid (与 fork 完全一致)
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

uint64 sys_gettid()
{
    return myproc()->pid;   // 单线程: tid == pid
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
    dup3(oldfd, newfd, flags)
    复制 oldfd 到指定的 newfd, 若 newfd 已打开则先关闭
    flags 忽略 (O_CLOEXEC 暂不实现)
    成功返回 newfd, 失败返回 -EBADF/-EINVAL
*/
uint64 sys_dup3()
{
    uint32 oldfd = (uint32)arg_raw(0);
    uint32 newfd = (uint32)arg_raw(1);

    proc_t *p = myproc();
    if (oldfd >= N_OPEN_FILE_PER_PROC || p->open_file[oldfd] == NULL)
        return (uint64)(-EBADF);
    if (newfd >= N_OPEN_FILE_PER_PROC)
        return (uint64)(-EBADF);
    if (oldfd == newfd)
        return (uint64)(-EINVAL);

    if (p->open_file[newfd] != NULL) {
        file_close(p->open_file[newfd]);
        p->open_file[newfd] = NULL;
    }

    p->open_file[newfd] = file_dup(p->open_file[oldfd]);
    return newfd;
}

/*
    mprotect(addr, len, prot) — 桩实现, 总是成功
    动态链接器自重定位时调用, 因为解释器段以 RWX 加载所以安全
*/
uint64 sys_mprotect()
{
    return 0;
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

// 134 rt_sigaction(signum, act, oldact, sigsetsize): 注册信号处理器
uint64 sys_rt_sigaction()
{
    int signum = (int)arg_raw(0);
    uint64 act_addr = arg_raw(1);
    uint64 oldact_addr = arg_raw(2);

    if (signum < 1 || signum > NSIG)
        return (uint64)(-EINVAL);

    proc_t *p = myproc();

    if (oldact_addr != 0) {
        uint8 buf[152];
        memset(buf, 0, sizeof(buf));
        *(uint64 *)&buf[0] = p->sig_handler[signum];
        *(uint64 *)&buf[8] = 0;
        *(uint64 *)&buf[16] = p->sig_restorer;
        uvm_copyout(p->pgtbl, oldact_addr, (uint64)buf, sizeof(buf));
    }

    if (act_addr != 0) {
        uint8 buf[152];
        uvm_copyin(p->pgtbl, (uint64)buf, act_addr, sizeof(buf));
        p->sig_handler[signum] = *(uint64 *)&buf[0];
        uint64 flags = *(uint64 *)&buf[8];
        if (flags & SA_RESTORER)
            p->sig_restorer = *(uint64 *)&buf[16];
    }

    return 0;
}

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

// qemu virt 的 time CSR 频率: INTERVAL=1e6 cycle≈0.1s => 10MHz
#define TIMEBASE_HZ 10000000ull

// 113 clock_gettime：获取时钟时间（高精度）
uint64 sys_clock_gettime()
{
    // clock_gettime(clk_id=a0(忽略, 统一用单调10MHz计数), struct timespec *tp=a1)
    uint64 tp = arg_raw(1);
    if (tp == 0) return 0;
    uint64 t = r_time();
    uint64 ts[2];
    ts[0] = t / TIMEBASE_HZ;             // tv_sec
    ts[1] = (t % TIMEBASE_HZ) * 100;     // tv_nsec (1/10MHz = 100ns)
    uvm_copyout(myproc()->pgtbl, tp, (uint64)ts, sizeof(ts));
    return 0;
}

// 169 gettimeofday：获取当前时间（微秒精度）
uint64 sys_gettimeofday()
{
    // gettimeofday(struct timeval *tv=a0, struct timezone *tz=a1(忽略))
    uint64 tv = arg_raw(0);
    if (tv == 0) return 0;
    uint64 t = r_time();
    uint64 val[2];
    val[0] = t / TIMEBASE_HZ;            // tv_sec
    val[1] = (t % TIMEBASE_HZ) / 10;     // tv_usec (10MHz/10 = 1MHz)
    uvm_copyout(myproc()->pgtbl, tv, (uint64)val, sizeof(val));
    return 0;
}

// 165 getrusage：获取资源使用统计（最小桩：全零）
uint64 sys_getrusage()
{
    // getrusage(int who=a0, struct rusage *usage=a1) 最小桩: 全零(144字节)返回0
    uint64 usage = arg_raw(1);
    if (usage == 0) return 0;
    char buf[144];
    memset(buf, 0, sizeof(buf));
    uvm_copyout(myproc()->pgtbl, usage, (uint64)buf, sizeof(buf));
    return 0;
}

uint64 sys_pipe2()
{
    // pipe2(int pipefd[2]=a0, int flags=a1)。flags(O_CLOEXEC/O_NONBLOCK)暂忽略。
    uint64 fdarray = arg_raw(0);
    file_t *rf = NULL, *wf = NULL;
    if (pipe_alloc(&rf, &wf) < 0)
        return -1;
    uint32 fd0 = alloc_fd(rf);
    uint32 fd1 = alloc_fd(wf);
    if (fd0 == (uint32)-1 || fd1 == (uint32)-1) {
        if (fd0 != (uint32)-1) myproc()->open_file[fd0] = NULL;
        file_close(rf);
        file_close(wf);
        return -1;
    }
    int fds[2] = { (int)fd0, (int)fd1 };
    uvm_copyout(myproc()->pgtbl, fdarray, (uint64)fds, sizeof(fds));
    return 0;
}

uint64 sys_umask()
{
    return 0;
}

// 29 ioctl(fd, cmd, arg): 终端/设备控制。最小桩: ENOTTY。
uint64 sys_ioctl()
{
    return (uint64)(-ENOTTY);
}

// 99 set_robust_list(head, len): musl 线程初始化需要。桩返回 0。
uint64 sys_set_robust_list()
{
    return 0;
}

// 100 get_robust_list(pid, head_ptr, len_ptr): 桩返回 0。
uint64 sys_get_robust_list()
{
    return 0;
}

// 102 getitimer(which, curr_value): 获取间隔定时器。桩: 零填充返回。
uint64 sys_getitimer()
{
    uint64 curr = arg_raw(1);
    if (curr == 0) return 0;
    char buf[32];
    memset(buf, 0, sizeof(buf));
    uvm_copyout(myproc()->pgtbl, curr, (uint64)buf, sizeof(buf));
    return 0;
}

// 103 setitimer(which, new_value, old_value): 设置间隔定时器。
// dhry2reg 用它做 benchmark 计时(ITIMER_REAL=0, 超时发 SIGALRM)。
uint64 sys_setitimer()
{
    uint64 which = arg_raw(0);
    uint64 new_addr = arg_raw(1);
    uint64 old_addr = arg_raw(2);
    proc_t *p = myproc();

    if (which != 0)
        return (uint64)(-EINVAL);

    if (old_addr != 0) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        uvm_copyout(p->pgtbl, old_addr, (uint64)buf, sizeof(buf));
    }

    if (new_addr != 0) {
        uint64 buf[4];
        uvm_copyin(p->pgtbl, (uint64)buf, new_addr, sizeof(buf));
        uint64 interval_sec = buf[0];
        uint64 interval_usec = buf[1];
        uint64 value_sec = buf[2];
        uint64 value_usec = buf[3];

        if (value_sec == 0 && value_usec == 0) {
            p->itimer_expire = 0;
            p->itimer_interval = 0;
        } else {
            uint64 now = r_time();
            uint64 delay = value_sec * 10000000ull + value_usec * 10;
            p->itimer_expire = now + delay;
            p->itimer_interval = interval_sec * 10000000ull + interval_usec * 10;
        }
    }

    return 0;
}

// 115 clock_nanosleep(clockid, flags, request, remain): 高精度睡眠。
uint64 sys_clock_nanosleep()
{
    uint64 request = arg_raw(2);
    if (request == 0) return 0;
    uint64 ts[2] = {0, 0};
    uvm_copyin(myproc()->pgtbl, (uint64)ts, request, sizeof(ts));
    uint64 ntick = ts[0] * 10;
    if (ts[1] > 0)
        ntick += (ts[1] + 99999999ull) / 100000000ull;
    if (ntick == 0)
        ntick = 1;
    timer_wait(ntick);
    return 0;
}

// 179 sysinfo(struct sysinfo *info): 系统信息。最小桩: 零填充。
uint64 sys_sysinfo()
{
    uint64 info = arg_raw(0);
    if (info == 0) return (uint64)(-EFAULT);
    char buf[112];
    memset(buf, 0, sizeof(buf));
    uint64 *p = (uint64 *)buf;
    p[0] = r_time() / 10000000ull; // uptime in seconds
    uvm_copyout(myproc()->pgtbl, info, (uint64)buf, sizeof(buf));
    return 0;
}

// 233 madvise(addr, length, advice): 内存建议。桩返回 0。
uint64 sys_madvise()
{
    return 0;
}

// 78 readlinkat(dirfd, pathname, buf, bufsiz): 读取符号链接。
// 特殊处理 /proc/self/exe 返回进程路径。其余返回 EINVAL。
uint64 sys_readlinkat()
{
    char path[128];
    arg_str(1, path, sizeof(path));
    if (path[0] == 0) return (uint64)(-EINVAL);
    return (uint64)(-EINVAL);
}

// 124 sched_yield(): 让出 CPU。
uint64 sys_sched_yield()
{
    proc_yield();
    return 0;
}

// 123 sched_getaffinity(pid, cpusetsize, mask): CPU 亲和性掩码。
// 单核: 返回 mask bit0=1。
uint64 sys_sched_getaffinity()
{
    uint64 cpusetsize = arg_raw(1);
    uint64 mask_addr = arg_raw(2);
    if (mask_addr == 0) return (uint64)(-EFAULT);
    char buf[128];
    uint64 len = cpusetsize < sizeof(buf) ? cpusetsize : sizeof(buf);
    memset(buf, 0, len);
    buf[0] = 1;
    uvm_copyout(myproc()->pgtbl, mask_addr, (uint64)buf, len);
    return 0;
}

// 177 getegid: 返回有效 GID (root=0)
uint64 sys_getegid()
{
    return 0;
}

// 175 geteuid: 返回有效 UID (root=0)
uint64 sys_geteuid()
{
    return 0;
}

// 139 rt_sigreturn: 从信号处理器返回，恢复被中断的执行上下文
uint64 sys_rt_sigreturn()
{
    proc_t *p = myproc();
    trapframe_t *tf = p->tf;

    uint64 frame[32];
    uvm_copyin(p->pgtbl, (uint64)frame, tf->sp, sizeof(frame));

    tf->user_to_kern_epc = frame[0];
    tf->ra   = frame[1];
    tf->sp   = frame[2];
    tf->gp   = frame[3];
    tf->tp   = frame[4];
    tf->t0   = frame[5];
    tf->t1   = frame[6];
    tf->t2   = frame[7];
    tf->s0   = frame[8];
    tf->s1   = frame[9];
    tf->a0   = frame[10];
    tf->a1   = frame[11];
    tf->a2   = frame[12];
    tf->a3   = frame[13];
    tf->a4   = frame[14];
    tf->a5   = frame[15];
    tf->a6   = frame[16];
    tf->a7   = frame[17];
    tf->s2   = frame[18];
    tf->s3   = frame[19];
    tf->s4   = frame[20];
    tf->s5   = frame[21];
    tf->s6   = frame[22];
    tf->s7   = frame[23];
    tf->s8   = frame[24];
    tf->s9   = frame[25];
    tf->s10  = frame[26];
    tf->s11  = frame[27];
    tf->t3   = frame[28];
    tf->t4   = frame[29];
    tf->t5   = frame[30];
    tf->t6   = frame[31];

    p->sig_delivering = 0;

    // 补偿 trap_user_handler 中 syscall 后的 epc += 4
    tf->user_to_kern_epc -= 4;

    return tf->a0;
}