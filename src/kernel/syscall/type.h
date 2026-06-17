#pragma once
#include "../arch/type.h"

/* 系统调用号 */

#define SYS_brk 214              // Linux/RISC-V brk
#define SYS_mmap 222             // Linux/RISC-V mmap
#define SYS_munmap 215           // Linux/RISC-V munmap
#define SYS_fork 4               // SeaOS fork
#define SYS_wait 260             // Linux/RISC-V wait4
#define SYS_exit 93              // Linux/RISC-V exit
#define SYS_sleep 101            // SeaOS nanosleep兼容入口
#define SYS_getpid 172           // Linux/RISC-V getpid
#define SYS_set_tid_address 96   // Linux/RISC-V set_tid_address
#define SYS_exec 221             // Linux/RISC-V execve
#define SYS_open 56              // Linux/RISC-V openat
#define SYS_close 57             // Linux/RISC-V close
#define SYS_read 63              // Linux/RISC-V read
#define SYS_write 64             // Linux/RISC-V write
#define SYS_readv 65             // Linux/RISC-V readv
#define SYS_lseek 62             // Linux/RISC-V lseek
#define SYS_dup 23               // Linux/RISC-V dup
#define SYS_fstat 80             // Linux/RISC-V fstat
#define SYS_sync 81              // Linux/RISC-V sync
#define SYS_fsync 82             // Linux/RISC-V fsync
#define SYS_fdatasync 83         // Linux/RISC-V fdatasync
#define SYS_get_dentries 61      // Linux/RISC-V getdents64
#define SYS_mkdir 34             // Linux/RISC-V mkdirat
#define SYS_chdir 49             // Linux/RISC-V chdir
#define SYS_getcwd 17            // Linux/RISC-V getcwd
#define SYS_link 37              // Linux/RISC-V linkat
#define SYS_renameat 38          // Linux/RISC-V renameat
#define SYS_statfs 43            // Linux/RISC-V statfs
#define SYS_fstatfs 44           // Linux/RISC-V fstatfs
#define SYS_ftruncate 46         // Linux/RISC-V ftruncate
#define SYS_faccessat 48         // Linux/RISC-V faccessat
#define SYS_unlink 35            // Linux/RISC-V unlinkat
#define SYS_newfstatat 79        // Linux/RISC-V newfstatat (按路径 stat)
#define SYS_utimensat 88         // Linux/RISC-V utimensat
#define SYS_rt_sigsuspend 133    // Linux/RISC-V rt_sigsuspend
#define SYS_rt_sigprocmask 135   // 信号屏蔽 (暂桩)
#define SYS_rt_sigreturn 139     // Linux/RISC-V rt_sigreturn
#define SYS_setgid 144           // 暂桩返回 0
#define SYS_setuid 146           // 暂桩返回 0
#define SYS_setsid 157           // Linux/RISC-V setsid
#define SYS_getuid 174           // 返回 0 (root)
#define SYS_getgid 176           // 返回 0
#define SYS_writev 66            // Linux/RISC-V writev
#define SYS_exit_group 94        // exit_group (单线程下等价 exit)
#define SYS_fcntl 25             // fcntl(fd,cmd,arg)
#define SYS_rt_sigaction 134     // 装信号处理器 (暂桩)
#define SYS_uname 160            // 系统信息
#define SYS_getrlimit 163        // Linux/RISC-V getrlimit
#define SYS_setrlimit 164        // Linux/RISC-V setrlimit
#define SYS_getppid 173          // 父进程 pid
#define SYS_gettid 178           // Linux/RISC-V gettid (单线程 = pid)
#define SYS_clone 220            // Linux/RISC-V clone (musl fork 依赖)
#define SYS_clock_gettime 113    // Linux/RISC-V clock_gettime
#define SYS_mlock 228            // Linux/RISC-V mlock
#define SYS_getrusage 165        // Linux/RISC-V getrusage (零填充桩)
#define SYS_gettimeofday 169     // Linux/RISC-V gettimeofday
#define SYS_pipe2 59             // Linux/RISC-V pipe2
#define SYS_umask 166            // Linux/RISC-V umask (桩, 返回0)
#define SYS_dup3 24              // Linux/RISC-V dup3
#define SYS_mprotect 226         // Linux/RISC-V mprotect
#define SYS_msync 227            // Linux/RISC-V msync
#define SYS_ioctl 29             // Linux/RISC-V ioctl
#define SYS_set_robust_list 99   // Linux/RISC-V set_robust_list
#define SYS_get_robust_list 100  // Linux/RISC-V get_robust_list
#define SYS_futex 98             // Linux/RISC-V futex
#define SYS_getitimer 102        // Linux/RISC-V getitimer
#define SYS_setitimer 103        // Linux/RISC-V setitimer
#define SYS_clock_getres 114     // Linux/RISC-V clock_getres
#define SYS_clock_nanosleep 115  // Linux/RISC-V clock_nanosleep
#define SYS_syslog 116           // Linux/RISC-V syslog/klogctl
#define SYS_kill 129             // Linux/RISC-V kill
#define SYS_tkill 130            // Linux/RISC-V tkill
#define SYS_tgkill 131           // Linux/RISC-V tgkill
#define SYS_sysinfo 179          // Linux/RISC-V sysinfo
#define SYS_madvise 233          // Linux/RISC-V madvise
#define SYS_readlinkat 78        // Linux/RISC-V readlinkat
#define SYS_sched_yield 124      // Linux/RISC-V sched_yield
#define SYS_sched_setparam 118   // Linux/RISC-V sched_setparam
#define SYS_sched_getscheduler 120 // Linux/RISC-V sched_getscheduler
#define SYS_sched_getparam 121   // Linux/RISC-V sched_getparam
#define SYS_sched_setaffinity 122 // Linux/RISC-V sched_setaffinity
#define SYS_sched_getaffinity 123 // Linux/RISC-V sched_getaffinity
#define SYS_getegid 177          // Linux/RISC-V getegid
#define SYS_geteuid 175          // Linux/RISC-V geteuid
#define SYS_pselect6 72          // Linux/RISC-V pselect6
#define SYS_ppoll 73             // Linux/RISC-V ppoll
#define SYS_sendfile 71          // Linux/RISC-V sendfile64
#define SYS_renameat2 276        // Linux/RISC-V renameat2
#define SYS_sched_setscheduler 119  // Linux/RISC-V sched_setscheduler
#define SYS_socket 198           // Linux/RISC-V socket
#define SYS_socketpair 199       // Linux/RISC-V socketpair
#define SYS_bind 200             // Linux/RISC-V bind
#define SYS_listen 201           // Linux/RISC-V listen
#define SYS_accept 202           // Linux/RISC-V accept
#define SYS_connect 203          // Linux/RISC-V connect
#define SYS_getsockname 204      // Linux/RISC-V getsockname
#define SYS_getpeername 205      // Linux/RISC-V getpeername
#define SYS_sendto 206           // Linux/RISC-V sendto
#define SYS_recvfrom 207         // Linux/RISC-V recvfrom
#define SYS_setsockopt 208       // Linux/RISC-V setsockopt
#define SYS_getsockopt 209       // Linux/RISC-V getsockopt
#define SYS_shutdown_sock 210    // Linux/RISC-V shutdown
#define SYS_sendmsg 211          // Linux/RISC-V sendmsg
#define SYS_recvmsg 212          // Linux/RISC-V recvmsg
#define SYS_accept4 242          // Linux/RISC-V accept4
#define SYS_get_mempolicy 236    // Linux/RISC-V get_mempolicy
#define SYS_prlimit64 261        // Linux/RISC-V prlimit64
#define SYS_getrandom 278        // Linux/RISC-V getrandom

#define SYS_schedstat 500        // SeaOS 私有: 拉取调度统计快照
#define SYS_spawn     501        // SeaOS 私有: fork+exec+wait 串行执行程序
#define SYS_shutdown  502        // SeaOS 私有: 请求系统关机

#define SYS_MAX_NUM 502

/* 可以传入的最大字符串长度 */
#define STR_MAXLEN 127
