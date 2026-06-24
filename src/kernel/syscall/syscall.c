#include "mod.h"

// 跳转表: 系统调用号 -> 系统调用服务函数
static uint64 (*syscalls[])(void) = {
    [SYS_brk] sys_brk,
    [SYS_mmap] sys_mmap,
    [SYS_munmap] sys_munmap,
    [SYS_mremap] sys_mremap,
    [SYS_fork] sys_fork,
    [SYS_wait] sys_wait,
    [SYS_exit] sys_exit,
    [SYS_sleep] sys_sleep,
    [SYS_getpid] sys_getpid,
    [SYS_set_tid_address] sys_set_tid_address,
    [SYS_exec] sys_exec,
    [SYS_open] sys_open,
    [SYS_close] sys_close,
    [SYS_read] sys_read,
    [SYS_write] sys_write,
    [SYS_readv] sys_readv,
    [SYS_pread64] sys_pread64,
    [SYS_pwrite64] sys_pwrite64,
    [SYS_lseek] sys_lseek,
    [SYS_dup] sys_dup,
    [SYS_fstat] sys_fstat,
    [SYS_sync] sys_sync,
    [SYS_fsync] sys_fsync,
    [SYS_fdatasync] sys_fdatasync,
    [SYS_get_dentries] sys_get_dentries,
    [SYS_mkdir] sys_mkdir,
    [SYS_chdir] sys_chdir,
    [SYS_getcwd] sys_getcwd,
    [SYS_mount] sys_mount,
    [SYS_umount2] sys_umount2,
    [SYS_symlinkat] sys_symlinkat,
    [SYS_link] sys_link,
    [SYS_renameat] sys_renameat,
    [SYS_statfs] sys_statfs,
    [SYS_fstatfs] sys_fstatfs,
    [SYS_ftruncate] sys_ftruncate,
    [SYS_faccessat] sys_faccessat,
    [SYS_fchmodat] sys_fchmodat,
    [SYS_fchownat] sys_fchownat,
    [SYS_unlink] sys_unlink,
    [SYS_newfstatat] sys_newfstatat,
    [SYS_rt_sigsuspend] sys_rt_sigsuspend,
    [SYS_rt_sigprocmask] sys_rt_sigprocmask,
    [SYS_rt_sigtimedwait] sys_rt_sigtimedwait,
    [SYS_rt_sigreturn] sys_rt_sigreturn,
    [SYS_setregid] sys_setregid,
    [SYS_setgid] sys_setgid,
    [SYS_setreuid] sys_setreuid,
    [SYS_setuid] sys_setuid,
    [SYS_setresuid] sys_setresuid,
    [SYS_setresgid] sys_setresgid,
    [SYS_setpgid] sys_setpgid,
    [SYS_setsid] sys_setsid,
    [SYS_times] sys_times,
    [SYS_getuid] sys_getuid,
    [SYS_getgid] sys_getgid,
    [SYS_writev] sys_writev,
    [SYS_exit_group] sys_exit_group,
    [SYS_fcntl] sys_fcntl,
    [SYS_rt_sigaction] sys_rt_sigaction,
    [SYS_uname] sys_uname,
    [SYS_getrlimit] sys_getrlimit,
    [SYS_setrlimit] sys_setrlimit,
    [SYS_getppid] sys_getppid,
    [SYS_gettid] sys_gettid,
    [SYS_clone] sys_clone,
    [SYS_clock_gettime] sys_clock_gettime,
    [SYS_mlock] sys_mlock,
    [SYS_getrusage] sys_getrusage,
    [SYS_gettimeofday] sys_gettimeofday,
    [SYS_pipe2] sys_pipe2,
    [SYS_umask] sys_umask,
    [SYS_dup3] sys_dup3,
    [SYS_mprotect] sys_mprotect,
    [SYS_msync] sys_msync,
    [SYS_ioctl] sys_ioctl,
    [SYS_set_robust_list] sys_set_robust_list,
    [SYS_get_robust_list] sys_get_robust_list,
    [SYS_futex] sys_futex,
    [SYS_getitimer] sys_getitimer,
    [SYS_setitimer] sys_setitimer,
    [SYS_clock_getres] sys_clock_getres,
    [SYS_clock_nanosleep] sys_clock_nanosleep,
    [SYS_syslog] sys_syslog,
    [SYS_kill] sys_kill,
    [SYS_tkill] sys_tkill,
    [SYS_tgkill] sys_tgkill,
    [SYS_sysinfo] sys_sysinfo,
    [SYS_shmget] sys_shmget,
    [SYS_shmctl] sys_shmctl,
    [SYS_shmat] sys_shmat,
    [SYS_shmdt] sys_shmdt,
    [SYS_acct] sys_acct,
    [SYS_adjtimex] sys_adjtimex,
    [SYS_add_key] sys_add_key,
    [SYS_keyctl] sys_keyctl,
    [SYS_madvise] sys_madvise,
    [SYS_utimensat] sys_utimensat,
    [SYS_readlinkat] sys_readlinkat,
    [SYS_sched_yield] sys_sched_yield,
    [SYS_sched_setparam] sys_sched_setparam,
    [SYS_sched_getscheduler] sys_sched_getscheduler,
    [SYS_sched_getparam] sys_sched_getparam,
    [SYS_sched_setaffinity] sys_sched_setaffinity,
    [SYS_sched_getaffinity] sys_sched_getaffinity,
    [SYS_getegid] sys_getegid,
    [SYS_geteuid] sys_geteuid,
    [SYS_pselect6] sys_pselect6,
    [SYS_ppoll] sys_ppoll,
    [SYS_sendfile] sys_sendfile,
    [SYS_renameat2] sys_renameat2,
    [SYS_sched_setscheduler] sys_sched_setscheduler,
    [SYS_socket] sys_socket,
    [SYS_socketpair] sys_socketpair,
    [SYS_bind] sys_bind,
    [SYS_listen] sys_listen,
    [SYS_accept] sys_accept,
    [SYS_connect] sys_connect,
    [SYS_getsockname] sys_getsockname,
    [SYS_getpeername] sys_getpeername,
    [SYS_sendto] sys_sendto,
    [SYS_recvfrom] sys_recvfrom,
    [SYS_setsockopt] sys_setsockopt,
    [SYS_getsockopt] sys_getsockopt,
    [SYS_shutdown_sock] sys_shutdown_sock,
    [SYS_sendmsg] sys_sendmsg,
    [SYS_recvmsg] sys_recvmsg,
    [SYS_accept4] sys_accept4,
    [SYS_get_mempolicy] sys_get_mempolicy,
    [SYS_prlimit64] sys_prlimit64,
    [SYS_getrandom] sys_getrandom,
    [SYS_membarrier] sys_membarrier,
    [SYS_schedstat] sys_schedstat,
    [SYS_spawn] sys_spawn,
    [SYS_shutdown] sys_shutdown,
};

// 每个未知 syscall 号只打印一次，避免 UART 洪水
static uint8 warned[SYS_MAX_NUM + 1];

static int syscall_is_restartable(int sys_num)
{
    return sys_num == SYS_wait;
}

static void syscall_save_restart_frame(proc_t *p, int sys_num)
{
    p->last_syscall_num = sys_num;
    p->last_syscall_args[0] = p->tf->a0;
    p->last_syscall_args[1] = p->tf->a1;
    p->last_syscall_args[2] = p->tf->a2;
    p->last_syscall_args[3] = p->tf->a3;
    p->last_syscall_args[4] = p->tf->a4;
    p->last_syscall_args[5] = p->tf->a5;
    p->last_syscall_restartable = syscall_is_restartable(sys_num);
}

// 基于系统调用表的请求跳转
void syscall()
{
    proc_t *p = myproc();

    int sys_num = p->tf->a7;
    syscall_save_restart_frame(p, sys_num);
    if (sys_num < 0 || sys_num > SYS_MAX_NUM || syscalls[sys_num] == NULL) {
        if (sys_num >= 0 && sys_num <= SYS_MAX_NUM && !warned[sys_num]) {
            warned[sys_num] = 1;
            printf("unknown syscall %d from pid = %d\n", sys_num, p->pid);
        }
        p->tf->a0 = (uint64)(-ENOSYS);
    } else {
        p->tf->a0 = syscalls[sys_num]();
    }
}

/*
    其他用于读取传入参数的函数
    参数分为两种,第一种是数据本身,第二种是指针
    第一种使用tf->ax传递
    第二种使用uvm_copyin 和 uvm_copyinstr 进行传递
*/

// 读取 n 号参数,它放在 an 寄存器中
uint64 arg_raw(int n)
{
    proc_t *proc = myproc();
    
    switch (n)
    {
    case 0:
        return proc->tf->a0;
    case 1:
        return proc->tf->a1;
    case 2:
        return proc->tf->a2;
    case 3:
        return proc->tf->a3;
    case 4:
        return proc->tf->a4;
    case 5:
        return proc->tf->a5;
    default:
        panic("arg_raw: illegal arg num");
        return 0;
    }
}

// 读取 n 号参数, 作为 uint32 存储
void arg_uint32(int n, uint32 *ip)
{
    *ip = arg_raw(n);
}

// 读取 n 号参数, 作为 uint64 存储
void arg_uint64(int n, uint64 *ip)
{
    *ip = arg_raw(n);
}

// 读取 n 号参数指向的字符串到 buf, 字符串最大长度是 maxlen
void arg_str(int n, char *buf, int maxlen)
{
    proc_t *p = myproc();
    uint64 addr;
    arg_uint64(n, &addr);

    uvm_copyin_str(p->pgtbl, (uint64)buf, addr, maxlen);
}

// 返回 n 号参数对应的文件
static proc_t *fd_table_proc(proc_t *p)
{
    if (p != NULL && p->thread_group && p->vm_owner != NULL)
        return p->vm_owner;
    return p;
}

int arg_fd(int n, uint32 *pfd, file_t **pfile)
{
    uint32 fd;
    file_t *file;
    proc_t *p = fd_table_proc(myproc());
    arg_uint32(n, &fd);

    // 越界fd
    if (fd >= N_OPEN_FILE_PER_PROC)
        return -EBADF;
    
    file = p->open_file[fd];
    
    // 无效fd
    if (file == NULL)
        return -EBADF;

    if (pfd) *pfd = fd;
    if (pfile) *pfile = file;

    return 0;
}
