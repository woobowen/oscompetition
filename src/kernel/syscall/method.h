#pragma once

typedef struct file file_t;

void syscall(void);

void arg_uint32(int n, uint32 *ip);
void arg_uint64(int n, uint64 *ip);
void arg_str(int n, char *buf, int maxlen);
int arg_fd(int n, uint32 *pfd, file_t **pfile);
uint64 arg_raw(int n);

uint64 sys_brk();
uint64 sys_mmap();
uint64 sys_munmap();
uint64 sys_fork();
uint64 sys_wait();
uint64 sys_exit();
uint64 sys_sleep();
uint64 sys_getpid();
uint64 sys_set_tid_address();
uint64 sys_exec();
uint64 sys_open();
uint64 sys_close();
uint64 sys_read();
uint64 sys_write();
uint64 sys_lseek();
uint64 sys_dup();
uint64 sys_fstat();
uint64 sys_get_dentries();
uint64 sys_mkdir();
uint64 sys_chdir();
uint64 sys_print_cwd();
uint64 sys_link();
uint64 sys_unlink();
uint64 sys_newfstatat();
uint64 sys_rt_sigprocmask();
uint64 sys_setgid();
uint64 sys_setuid();
uint64 sys_getuid();
uint64 sys_getgid();
uint64 sys_schedstat();
uint64 sys_spawn();
uint64 sys_shutdown();
