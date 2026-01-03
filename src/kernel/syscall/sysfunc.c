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
        printf("look event: ret_heap_top = %p\n", cur);

    }else if (new_top > cur) { // grow
        uint32 len = (uint32)(new_top - cur);
        uint64 ret = uvm_heap_grow(p->pgtbl, cur, len, PTE_R | PTE_W | PTE_U);
        if (ret == (uint64)-1) return (uint64)-1;
        p->heap_top = ret;
        printf("grow event: ret_heap_top = %p old_heap_top = %p len = 0x%x\n", (void *)ret, (void *)cur, len);

    }else { // ungrow & stay
        uint32 len = (uint32)(cur - new_top);
        uint64 ret = uvm_heap_ungrow(p->pgtbl, cur, len);
        if (ret == (uint64)-1) return (uint64)-1;
        p->heap_top = ret;
        printf("ungrow event: ret_heap_top = %p old_heap_top = %p len = 0x%x\n", (void *)ret, (void *)cur, len);

    }
    printf("After the event: Current pgtbl:\n");
    vm_print(p->pgtbl);
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
    uint64 addr_exit_state;
    arg_uint64(0, &addr_exit_state); // 获取接收退出状态的用户地址
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
    arg_str(0, path, STR_MAXLEN);
    
    uint64 argv_addr;
    arg_uint64(1, &argv_addr);
    
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
    arg_str(0, path, STR_MAXLEN);
    
    uint32 open_mode;
    arg_uint32(1, &open_mode);
    
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
    
    uint32 len;
    arg_uint32(1, &len);
    
    uint64 addr;
    arg_uint64(2, &addr);
    
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
    
    uint32 len;
    arg_uint32(1, &len);
    
    uint64 addr;
    arg_uint64(2, &addr);
    
    return file_write(file, len, addr, true);
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
    if (arg_fd(0, NULL, &file) < 0) return -1;
    
    uint64 addr;
    arg_uint64(1, &addr);
    
    uint32 buffer_len;
    arg_uint32(2, &buffer_len);
    
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
    arg_str(0, path, STR_MAXLEN);
    
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
    
    path[STR_MAXLEN+1] = '\0'; 
    printf("current work directory:%s\n", path + offset);
    return 0;
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
    arg_str(0, old_path, STR_MAXLEN);
    arg_str(1, new_path, STR_MAXLEN);
    
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
    arg_str(0, path, STR_MAXLEN);
    
    return path_unlink(path);
}