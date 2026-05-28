#include "mod.h"

/*
    将ELF文件中的segment放入内存中制定位置
    inode逻辑区域: [seg_start, seg_start + len)
    进程地址空间: [va_start, va_start + len), 对应的物理页是存在的
*/
static void load_segment(inode_t *ip, pgtbl_t pgtbl, 
    uint64 seg_start, uint64 va_start, uint32 len)
{
    uint32 read_len, cut_len;

    for (read_len = 0; read_len < len; read_len += PGSIZE)
    {
        uint64 cur_va = va_start + read_len;
        uint64 page_va = (cur_va / PGSIZE) * PGSIZE;
        uint64 page_off = cur_va - page_va;
        pte_t *pte = vm_getpte(pgtbl, page_va, false);
        uint64 pa = PTE_TO_PA(*pte);
        assert(pa != 0, "load_segment: invalid pa!");

        /* 读入segment的一部分 */
        cut_len = MIN(len - read_len, (uint32)(PGSIZE - page_off));
        if (inode_read_data(ip, (uint32)seg_start + read_len, cut_len, (void*)(pa + page_off), false) != cut_len)
            panic("load_segment: read fail!");
    }
}

/* 将程序的代码区和数据区读入用户堆中, 返回new_heap_top */
static uint64 prepare_heap(pgtbl_t new_pgtbl, inode_t *ip, elf_header_t *eh)
{
    program_header_t ph;
    uint64 new_heap_top = USER_BASE, old_heap_top = USER_BASE;
    
    for (uint32 off = eh->ph_off; off < eh->ph_off + eh->ph_ent_num * sizeof(ph); off += sizeof(ph))
    {
        // 读入一个program header
        if (inode_read_data(ip, off, sizeof(ph), &ph, false) != sizeof(ph))
            return -1;
         // debug: 打印 program header 信息
         printf("proc_prepare_heap: ph_idx=%d type=%d va=%p off=%p file_size=%p mem_size=%p flags=0x%x\n",
             (int)((off - eh->ph_off) / sizeof(ph)), ph.type,
             (void*)ph.va, (void*)ph.off,
             (void*)ph.file_size, (void*)ph.mem_size, ph.flags);
        
        // 判断是否有必要载入
        if (ph.type != ELF_PROG_LOAD)
            continue;
        
        // program header参数的合法性检查
        if (ph.mem_size < ph.file_size)
            return -1;
        if (ph.va + ph.mem_size < ph.va)
            return -1;
        // Ensure the segment lies in user address space and not overlapping kernel/trampoline
        if (ph.va < USER_BASE || ph.va + ph.mem_size > MMAP_BEGIN) {
            printf("proc_prepare_heap: ph_idx=%d va %p mem_size %p out of user range\n",
                   (int)((off - eh->ph_off) / sizeof(ph)), (void*)ph.va, (void*)ph.mem_size);
            return -1;
        }
        
        //! 用户堆生长
        uint32 perm = PTE_U;
        if(ph.flags & ELF_PROG_FLAG_READ) perm |= PTE_R;
        if(ph.flags & ELF_PROG_FLAG_WRITE) perm |= PTE_W;
        if(ph.flags & ELF_PROG_FLAG_EXEC) perm |= PTE_X;

        new_heap_top = uvm_heap_grow(new_pgtbl, old_heap_top,
                        ph.va + ph.mem_size - old_heap_top, perm);//! 更灵活的权限设置
        if (new_heap_top != ph.va + ph.mem_size)
            return -1;
        old_heap_top = new_heap_top;

        // segment读入
        load_segment(ip, new_pgtbl, ph.off, ph.va, ph.file_size);
    }

    return new_heap_top;
}

/* 准备栈空间用于存储输入参数(4KB), 设置arg_count, 返回sp */
static uint64 prepare_stack(pgtbl_t new_pgtbl, char **argv, int *arg_count)
{
    uint64 ustack_page;
    uint64 sp = TRAPFRAME, sp_base = TRAPFRAME - PGSIZE;
    uint64 sp_list[ELF_MAXARGS + 1];
    uint32 argc, arg_len;

    ustack_page = (uint64)pmem_alloc(false);
    vm_mappages(new_pgtbl, sp_base, ustack_page, PGSIZE, PTE_R | PTE_W | PTE_U);
    
    for (argc = 0; argv[argc] != NULL; argc++)
    {
        if (argc >= ELF_MAXARGS)
            return -1;
        
        arg_len = strlen(argv[argc]) + 1;
        sp -= ALIGN_UP(arg_len, 16);
        if (sp < sp_base)
            return -1;
        
        uvm_copyout(new_pgtbl, sp, (uint64)argv[argc], arg_len);

        sp_list[argc] = sp;
    }
    sp_list[argc] = 0;

    arg_len = (argc + 1) * sizeof(uint64);
    sp -= ALIGN_UP(arg_len, 16);
    if (sp < sp_base)
        return -1;

    uvm_copyout(new_pgtbl, sp, (uint64)sp_list, arg_len);

    *arg_count = argc;

    return sp;
}

static int is_script_path(const char *path)
{
    int len = strlen(path);
    return len >= 3 && path[len - 3] == '.' && path[len - 2] == 's' && path[len - 1] == 'h';
}

static int build_script_argv(char **argv, char *script_path, char *interp_path, char *interp_arg,
                             char **out_argv)
{
    int argc = 0;
    out_argv[argc++] = interp_path;
    if (interp_arg && interp_arg[0] != '\0')
        out_argv[argc++] = interp_arg;
    out_argv[argc++] = script_path;

    for (int i = 0; argv && argv[i] != NULL; i++) {
        if (argc >= ELF_MAXARGS)
            return -1;
        out_argv[argc++] = argv[i];
    }
    out_argv[argc] = NULL;
    return argc;
}

static int parse_shebang(inode_t *ip, char *interp_path, char *interp_arg)
{
    char buf[128];
    uint32 n = inode_read_data(ip, 0, sizeof(buf) - 1, buf, false);
    if (n < 2)
        return -1;
    buf[n] = '\0';

    if (buf[0] != '#' || buf[1] != '!')
        return -1;

    int i = 2;
    while (buf[i] == ' ' || buf[i] == '\t')
        i++;

    int j = 0;
    while (buf[i] != '\0' && buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\n' && j < STR_MAXLEN) {
        interp_path[j++] = buf[i++];
    }
    interp_path[j] = '\0';
    if (j == 0)
        return -1;

    while (buf[i] == ' ' || buf[i] == '\t')
        i++;

    j = 0;
    while (buf[i] != '\0' && buf[i] != '\n' && j < STR_MAXLEN) {
        interp_arg[j++] = buf[i++];
    }
    interp_arg[j] = '\0';
    return 0;
}

static int pick_script_interpreter(const char *requested, char *resolved_path, char *resolved_arg)
{
    const char *path_candidates[8];
    const char *arg_candidates[8];
    int candidate_count = 0;

    if (requested && requested[0] != '\0') {
        if (candidate_count >= 8)
            return -1;
        path_candidates[candidate_count] = requested;
        arg_candidates[candidate_count] = "";
        candidate_count++;
    }

    if (candidate_count >= 8)
        return -1;
    path_candidates[candidate_count] = "/bin/sh";
    arg_candidates[candidate_count] = "";
    candidate_count++;

    if (candidate_count >= 8)
        return -1;
    path_candidates[candidate_count] = "busybox";
    arg_candidates[candidate_count] = "sh";
    candidate_count++;

    if (candidate_count >= 8)
        return -1;
    path_candidates[candidate_count] = "/busybox";
    arg_candidates[candidate_count] = "sh";
    candidate_count++;

    if (candidate_count >= 8)
        return -1;
    path_candidates[candidate_count] = "/busybox";
    arg_candidates[candidate_count] = "ash";
    candidate_count++;

    if (candidate_count >= 8)
        return -1;
    path_candidates[candidate_count] = "/bin/busybox";
    arg_candidates[candidate_count] = "sh";
    candidate_count++;

    if (candidate_count >= 8)
        return -1;
    path_candidates[candidate_count] = "/musl/busybox";
    arg_candidates[candidate_count] = "sh";
    candidate_count++;

    if (candidate_count >= 8)
        return -1;
    path_candidates[candidate_count] = "/glibc/busybox";
    arg_candidates[candidate_count] = "sh";
    candidate_count++;

    for (int i = 0; i < candidate_count; i++) {
        inode_t *ip = path_to_inode((char*)path_candidates[i]);
        if (!ip)
            continue;

        elf_header_t eh;
        uint32 data_size = inode_read_data(ip, 0, sizeof(eh), &eh, false);
        inode_put(ip);
        if (data_size != sizeof(eh) || eh.magic != ELF_MAGIC)
            continue;

        int j = 0;
        while (path_candidates[i][j] != '\0') {
            resolved_path[j] = path_candidates[i][j];
            j++;
        }
        resolved_path[j] = '\0';

        j = 0;
        while (arg_candidates[i][j] != '\0') {
            resolved_arg[j] = arg_candidates[i][j];
            j++;
        }
        resolved_arg[j] = '\0';
        return 0;
    }

    return -1;
}

/*
    执行ELF文件
    输入路径和参数
    成功返回argc, 失败返回-1
*/
int proc_exec(char *path, char **argv)
{
    proc_t *p = myproc();
    printf("proc_exec: pid=%d path=%s\n", p ? p->pid : -1, path);
    
    // step-0: 准备全新的pagetable和trapframe
    // 分配一个新的物理页作为 Trapframe
    trapframe_t *new_tf = (trapframe_t*)pmem_alloc(false);
    if (!new_tf)  return -1;
    memset(new_tf, 0, sizeof(trapframe_t));

    if (!new_tf) {
        printf("proc_exec: pid=%d pmem_alloc tf failed\n", p ? p->pid : -1);
        return -1;
    }

    // 初始化新页表
    pgtbl_t new_pgtbl = proc_pgtbl_init((uint64)new_tf);
    if (new_pgtbl == NULL) {
        pmem_free((uint64)new_tf, false);
        printf("proc_exec: pid=%d proc_pgtbl_init failed\n", p ? p->pid : -1);
        return -1;
    }
    
    // step-1: 解析输入的文件路径, 获取ELF文件的inode
    inode_t *ip = path_to_inode(path);
    if (!ip) {
        uvm_destroy_pgtbl(new_pgtbl);
        printf("proc_exec: pid=%d path_to_inode(%s) failed\n", p ? p->pid : -1, path);
        return -1;
    }
    
    // step-2: 读取ELF_header
    elf_header_t eh;
    uint32 data_size = inode_read_data(ip, 0, sizeof(eh), &eh, false);
            printf("proc_exec: pid=%d read elf header size=%d entry=%p ph_off=%p ph_ent_num=%d\n",
                p ? p->pid : -1, (int)data_size, (void*)eh.entry, (void*)eh.ph_off, (int)eh.ph_ent_num);
    char script_interp[STR_MAXLEN + 1];
    char script_interp_arg[STR_MAXLEN + 1];
    char *script_argv[ELF_MAXARGS + 1];
    int use_script = 0;

    if (data_size != sizeof(eh) || eh.magic != ELF_MAGIC) {
        if (is_script_path(path) && parse_shebang(ip, script_interp, script_interp_arg) == 0) {
            char resolved_interp[STR_MAXLEN + 1];
            char resolved_interp_arg[STR_MAXLEN + 1];

            if (pick_script_interpreter(script_interp, resolved_interp, resolved_interp_arg) < 0) {
                inode_put(ip);
                uvm_destroy_pgtbl(new_pgtbl);
                printf("proc_exec: pid=%d no script interpreter found for %s\n", p ? p->pid : -1, path);
                return -1;
            }

            int script_argc = build_script_argv(argv, path, resolved_interp, resolved_interp_arg, script_argv);
            if (script_argc < 0) {
                inode_put(ip);
                uvm_destroy_pgtbl(new_pgtbl);
                printf("proc_exec: pid=%d build_script_argv failed\n", p ? p->pid : -1);
                return -1;
            }
            argv = script_argv;
            use_script = 1;
            path = resolved_interp;
            inode_put(ip);
            ip = path_to_inode(path);
            if (!ip) {
                uvm_destroy_pgtbl(new_pgtbl);
                printf("proc_exec: pid=%d script interpreter path_to_inode(%s) failed\n", p ? p->pid : -1, path);
                return -1;
            }
            data_size = inode_read_data(ip, 0, sizeof(eh), &eh, false);
            if (data_size != sizeof(eh) || eh.magic != ELF_MAGIC) {
                inode_put(ip);
                uvm_destroy_pgtbl(new_pgtbl);
                printf("proc_exec: pid=%d script interpreter is not ELF\n", p ? p->pid : -1);
                return -1;
            }
        } else {
            inode_put(ip);
            uvm_destroy_pgtbl(new_pgtbl);
            printf("proc_exec: pid=%d invalid ELF header or read fail\n", p ? p->pid : -1);
            return -1;
        }
    }
    /* Basic sanity check: entry must be in user space */
    if (eh.entry < USER_BASE || eh.entry >= TRAMPOLINE) {
        inode_put(ip);
        uvm_destroy_pgtbl(new_pgtbl);
        printf("proc_exec: pid=%d ELF entry %p out of user range\n", p ? p->pid : -1, (void*)eh.entry);
        return -1;
    }
    
    // step-3: 按照顺序读取需要载入内存的Segment, 填充到用户堆区域
    uint64 new_heap_top = prepare_heap(new_pgtbl, ip, &eh);
    if (new_heap_top == -1) {
        inode_put(ip);
        // pmem_free((uint64)new_tf, false); 【修复】
        uvm_destroy_pgtbl(new_pgtbl);
        printf("proc_exec: pid=%d prepare_heap failed\n", p ? p->pid : -1);
        return -1;
    }
    
    // step-4: 释放ELF的inode
    inode_put(ip);
    
    // step-5: 处理输入的参数列表argv, 填充到用户栈区域
    int argc;
    uint64 sp = prepare_stack(new_pgtbl, argv, &argc);
    if (sp == -1) {
        // pmem_free((uint64)new_tf, false); 【修复】
        uvm_destroy_pgtbl(new_pgtbl);
        printf("proc_exec: pid=%d prepare_stack failed\n", p ? p->pid : -1);
        return -1;
    }
    
    // step-6: 新的地址空间构建完毕, 释放旧资源
    uvm_destroy_pgtbl(p->pgtbl);
    // pmem_free((uint64)p->tf, false); 【修复】
    if (p->mmap) {
        mmap_region_t *mmap = p->mmap;
        while (mmap) {
            mmap_region_t *next = mmap->next;
            mmap_region_free(mmap);
            mmap = next;
        }
    }
    
    // step-7: 设置trapframe的相关字段
    new_tf->a0 = argc;       
    new_tf->a1 = sp;        
    new_tf->user_to_kern_epc = eh.entry;   
    new_tf->sp = sp;          
    
    // step-8: 更新进程的相关字段
    p->pgtbl = new_pgtbl;
    p->tf = new_tf;
    p->heap_top = new_heap_top;
    p->ustack_npage = 1;     
    p->mmap = NULL;
    int i;
    for(i = 0; i < sizeof(p->name) - 1 && path[i] != '\0'; i++){
        p->name[i] = path[i];
    }
    p->name[i] = '\0';
    p->name[sizeof(p->name) - 1] = '\0';
    printf("proc_exec: pid=%d exec done argc=%d heap_top=%p tf=%p%s\n", p->pid, argc, (void*)p->heap_top, (void*)p->tf, use_script ? " script" : "");
    
    return argc;
}

/* 在当前上下文对指定pid的进程执行exec（替换其地址空间）。
   返回0表示成功，返回-1表示失败（失败时会将子进程置为ZOMBIE并唤醒父进程）。 */
int proc_exec_target(int pid, char *path, char **argv)
{
    proc_t *p = proc_get_by_pid(pid);
    if (!p) return -1;

    printf("proc_exec_target: pid=%d path=%s\n", p->pid, path);

    /* 类似 proc_exec 的实现，但对指定进程p操作（p的锁已持有） */
    trapframe_t *new_tf = (trapframe_t*)pmem_alloc(false);
    if (!new_tf) {
        goto exec_fail;
    }
    memset(new_tf, 0, sizeof(trapframe_t));

    pgtbl_t new_pgtbl = proc_pgtbl_init((uint64)new_tf);
    if (!new_pgtbl) {
        pmem_free((uint64)new_tf, false);
        goto exec_fail;
    }

    inode_t *ip = path_to_inode(path);
    if (!ip) {
        uvm_destroy_pgtbl(new_pgtbl);
        goto exec_fail;
    }

    elf_header_t eh;
    uint32 data_size = inode_read_data(ip, 0, sizeof(eh), &eh, false);
          printf("proc_exec_target: pid=%d read elf header size=%d entry=%p ph_off=%p ph_ent_num=%d\n",
              p ? p->pid : -1, (int)data_size, (void*)eh.entry, (void*)eh.ph_off, (int)eh.ph_ent_num);
    char script_interp[STR_MAXLEN + 1];
    char script_interp_arg[STR_MAXLEN + 1];
    char *script_argv[ELF_MAXARGS + 1];
    int use_script = 0;

    if (data_size != sizeof(eh) || eh.magic != ELF_MAGIC) {
        if (is_script_path(path) && parse_shebang(ip, script_interp, script_interp_arg) == 0) {
            char resolved_interp[STR_MAXLEN + 1];
            char resolved_interp_arg[STR_MAXLEN + 1];

            if (pick_script_interpreter(script_interp, resolved_interp, resolved_interp_arg) < 0) {
                inode_put(ip);
                uvm_destroy_pgtbl(new_pgtbl);
                printf("proc_exec_target: pid=%d no script interpreter found for %s\n", p->pid, path);
                goto exec_fail;
            }

            int script_argc = build_script_argv(argv, path, resolved_interp, resolved_interp_arg, script_argv);
            if (script_argc < 0) {
                inode_put(ip);
                uvm_destroy_pgtbl(new_pgtbl);
                printf("proc_exec_target: pid=%d build_script_argv failed\n", p->pid);
                goto exec_fail;
            }
            argv = script_argv;
            use_script = 1;
            path = resolved_interp;
            inode_put(ip);
            ip = path_to_inode(path);
            if (!ip) {
                uvm_destroy_pgtbl(new_pgtbl);
                printf("proc_exec_target: pid=%d script interpreter path_to_inode(%s) failed\n", p->pid, path);
                goto exec_fail;
            }
                 data_size = inode_read_data(ip, 0, sizeof(eh), &eh, false);
                      printf("proc_exec_target: pid=%d interp elf entry=%p ph_off=%p ph_ent_num=%d\n",
                          p ? p->pid : -1, (void*)eh.entry, (void*)eh.ph_off, (int)eh.ph_ent_num);
                 if (data_size != sizeof(eh) || eh.magic != ELF_MAGIC) {
                inode_put(ip);
                uvm_destroy_pgtbl(new_pgtbl);
                printf("proc_exec_target: pid=%d script interpreter is not ELF\n", p->pid);
                goto exec_fail;
            }
        } else {
            inode_put(ip);
            uvm_destroy_pgtbl(new_pgtbl);
            printf("proc_exec_target: pid=%d invalid ELF header or read fail\n", p->pid);
            goto exec_fail;
        }
    }
    /* Basic sanity check: entry must be in user space */
    if (eh.entry < USER_BASE || eh.entry >= TRAMPOLINE) {
        inode_put(ip);
        uvm_destroy_pgtbl(new_pgtbl);
        printf("proc_exec_target: pid=%d ELF entry %p out of user range\n", p ? p->pid : -1, (void*)eh.entry);
        goto exec_fail;
    }

    uint64 new_heap_top = prepare_heap(new_pgtbl, ip, &eh);
    if (new_heap_top == (uint64)-1) {
        inode_put(ip);
        uvm_destroy_pgtbl(new_pgtbl);
        goto exec_fail;
    }

    inode_put(ip);

    int argc;
    uint64 sp = prepare_stack(new_pgtbl, argv, &argc);
    if (sp == (uint64)-1) {
        uvm_destroy_pgtbl(new_pgtbl);
        goto exec_fail;
    }

    /* 释放旧资源 */
    uvm_destroy_pgtbl(p->pgtbl);
    if (p->mmap) {
        mmap_region_t *mmap = p->mmap;
        while (mmap) {
            mmap_region_t *next = mmap->next;
            mmap_region_free(mmap);
            mmap = next;
        }
    }

    /* 设置trapframe与进程字段 */
    new_tf->a0 = argc;
    new_tf->a1 = sp;
    new_tf->user_to_kern_epc = eh.entry;
    new_tf->sp = sp;

    p->pgtbl = new_pgtbl;
    p->tf = new_tf;
    p->heap_top = new_heap_top;
    p->ustack_npage = 1;
    p->mmap = NULL;
    int i;
    for(i = 0; i < sizeof(p->name) - 1 && path[i] != '\0'; i++){
        p->name[i] = path[i];
    }
    p->name[i] = '\0';
    p->name[sizeof(p->name) - 1] = '\0';

    printf("proc_exec_target: pid=%d exec done argc=%d heap_top=%p tf=%p%s\n", p->pid, argc, (void*)p->heap_top, (void*)p->tf, use_script ? " script" : "");

    /* 完成，释放p->lk */
    spinlock_release(&p->lk);
    return 0;

exec_fail:
    /* 将子进程置为 ZOMBIE 并唤醒父进程 */
    p->exit_code = 1;
    p->state = ZOMBIE;
    proc_t *parent = p->parent;
    spinlock_release(&p->lk);

    if (parent) {
        spinlock_acquire(&parent->lk);
        if (parent->state == SLEEPING && parent->sleep_space == parent) {
            parent->state = RUNNABLE;
            parent->sleep_space = NULL;
            parent->sched_last_ready_tick = timer_get_ticks();
            parent->sched_ready_count++;
        }
        spinlock_release(&parent->lk);
        mlfq_on_wakeup(parent);
    }

    return -1;
}
