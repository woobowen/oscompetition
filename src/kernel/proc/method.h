#pragma once

// proc.c: 进程管理相关

void proc_init();                                   // 进程模块初始化
proc_t *proc_alloc();                               // 进程申请
void proc_free(proc_t *p);                          // 进程释放

pgtbl_t proc_pgtbl_init(uint64 trapframe);          // 页表初始化
void proc_make_first();                             // 创建第一个用户进程
int proc_fork();                                    // 复制子进程
int proc_wait4(int64 wait_pid, uint64 user_addr, int wnohang); // 等待子进程退出
void proc_exit(int exit_state);                     // 进程退出
void proc_exit_group(int exit_state);               // exit all CLONE_VM siblings in this VM group
void proc_exit_group_if_requested();              // current thread handles pending group exit
void proc_yield();                                  // 进程放弃CPU
void proc_sleep(void *sleep_space, spinlock_t *lk); // 进程睡眠
void proc_wakeup(void *sleep_space);                // 进程唤醒
void proc_wakeup_force(void *sleep_space);          // futex/clear_child_tid paths must not miss pre-hint wakeups
void proc_shared_vm_sync_mmap(mmap_region_t *old_head, mmap_region_t *new_head);
void proc_shared_vm_sync_heap_grow(uint64 old_top, uint64 new_top);
int proc_shared_vm_lookup_page(uint64 va, uint64 *pa_out, int *flags_out);
int proc_shared_vm_map_page(uint64 va, uint64 pa, int flags);
uint64 proc_shared_vm_unmap_page(uint64 va);
void proc_check_itimers(uint64 now);                // ITIMER_REAL wall-clock expiry
void proc_sched();                                  // 进程切换到调度器
void proc_scheduler();                              // 调度器选择合适的进程执行

// 调度统计: copyout 一份 proc 列表快照到用户态
uint32 proc_schedstat(uint64 user_dst, uint32 max_entries);

// MLFQ: 多级反馈队列调度
void mlfq_init(void);
void mlfq_on_new(proc_t *p);                         // 新创建/初次就绪: 放入高优先级队列
void mlfq_on_wakeup(proc_t *p);                      // 睡眠唤醒: 提升到高优先级队列
void mlfq_on_yield(proc_t *p, int reason);           // 让出CPU: 根据原因降级/保持
proc_t *mlfq_pick_next(void);                        // 选择下一个RUNNABLE进程(不加p->lk)
bool mlfq_has_higher(int level);                     // 是否存在更高优先级的就绪进程
void mlfq_age_tick(void);                            // aging: 更新等待tick并按阈值提升

// Lab-11: 运行状态提示(用于 wakeup 的负载评估)
void mlfq_set_cpu_running(int cpu, int running);

// MLFQ 内部锁(用于保证全局锁顺序: 先 mlfq, 后 p->lk)
void mlfq_lock(void);
void mlfq_unlock(void);
void mlfq_on_yield_locked(proc_t *p, int reason);    // 需要已持有 mlfq_lock
int proc_on_tick(void);                              // 用户态时钟中断: 更新时间片并判断是否需要yield

// exec.c: 重置进程以执行ELF文件

int proc_exec(char *path, char **argv);             // 准备新进程
int proc_exec_env(char *path, char **argv, char **envp);
/* 查找pid对应的进程并返回（返回时持有该进程锁），找不到返回NULL */
proc_t *proc_get_by_pid(int pid);
/* 在当前上下文对指定pid的进程执行exec（替换其地址空间）。
	返回0表示成功，返回-1表示失败（失败时会将子进程置为ZOMBIE并唤醒父进程）。 */
int proc_exec_target(int pid, char *path, char **argv);
