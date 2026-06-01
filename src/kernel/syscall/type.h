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
#define SYS_lseek 62             // Linux/RISC-V lseek
#define SYS_dup 23               // Linux/RISC-V dup
#define SYS_fstat 80             // Linux/RISC-V fstat
#define SYS_get_dentries 61      // Linux/RISC-V getdents64
#define SYS_mkdir 34             // Linux/RISC-V mkdirat
#define SYS_chdir 49             // Linux/RISC-V chdir
#define SYS_print_cwd 17         // SeaOS print_cwd
#define SYS_link 37              // Linux/RISC-V linkat
#define SYS_unlink 35            // Linux/RISC-V unlinkat
#define SYS_newfstatat 79        // Linux/RISC-V newfstatat (按路径 stat)
#define SYS_rt_sigprocmask 135   // 信号屏蔽 (暂桩)
#define SYS_setgid 144           // 暂桩返回 0
#define SYS_setuid 146           // 暂桩返回 0
#define SYS_getuid 174           // 返回 0 (root)
#define SYS_getgid 176           // 返回 0
#define SYS_writev 66            // Linux/RISC-V writev
#define SYS_exit_group 94        // exit_group (单线程下等价 exit)
#define SYS_fcntl 25             // fcntl(fd,cmd,arg)
#define SYS_rt_sigaction 134     // 装信号处理器 (暂桩)
#define SYS_uname 160            // 系统信息
#define SYS_getppid 173          // 父进程 pid
#define SYS_clone 220            // Linux/RISC-V clone (musl fork 依赖)

#define SYS_schedstat 500        // SeaOS 私有: 拉取调度统计快照
#define SYS_spawn     501        // SeaOS 私有: fork+exec+wait 串行执行程序
#define SYS_shutdown  502        // SeaOS 私有: 请求系统关机

#define SYS_MAX_NUM 502

/* 可以传入的最大字符串长度 */
#define STR_MAXLEN 127