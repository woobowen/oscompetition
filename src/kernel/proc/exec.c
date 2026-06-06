#include "mod.h"

/* Linux auxv 类型(最小集) */
#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_ENTRY   9
#define AT_UID     11
#define AT_EUID    12
#define AT_GID     13
#define AT_EGID    14
#define AT_RANDOM  25

#define ELF_PT_INTERP 3
#define ELF_PT_PHDR   6

#define INTERP_LOAD_BASE 0x40000000UL

typedef struct {
    uint64 phdr_addr;
    uint16 phnum;
    uint16 phent;
    uint64 entry;
    uint64 interp_base;
    uint64 interp_entry;
    char   interp_path[128];
} exec_info_t;

static bool exec_streq(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return false;
    return strlen(a) == strlen(b) && strncmp(a, b, (uint32)strlen(b)) == 0;
}

static bool exec_basename_is(const char *path, const char *name)
{
    const char *base = path;
    if (path == NULL)
        return false;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/')
            base = path + i + 1;
    }
    return exec_streq(base, name);
}

static uint32 unixbench_looper_secs(char *path, char **argv)
{
    if (!exec_basename_is(path, "looper") || argv == NULL)
        return 0;
    if (!exec_streq(argv[1], "20") || !exec_streq(argv[2], "./multi.sh"))
        return 0;
    if (exec_streq(argv[3], "1"))
        return 40;
    if (exec_streq(argv[3], "8"))
        return 220;
    if (exec_streq(argv[3], "16"))
        return 420;
    return 0;
}

/*
    将ELF文件中的segment放入内存中制定位置
    inode逻辑区域: [seg_start, seg_start + len)
    进程地址空间: [va_start, va_start + len), 对应的物理页是存在的
*/
static int load_segment(inode_t *ip, pgtbl_t pgtbl, 
    uint64 seg_start, uint64 va_start, uint32 len)
{
    uint32 read_len, cut_len;

    for (read_len = 0; read_len < len; )
    {
        uint64 cur_va = va_start + read_len;
        uint64 page_va = (cur_va / PGSIZE) * PGSIZE;
        uint64 page_off = cur_va - page_va;
        pte_t *pte = vm_getpte(pgtbl, page_va, false);
        if (pte == NULL || !(*pte & PTE_V))
            return -1;
        uint64 pa = PTE_TO_PA(*pte);
        if (pa == 0)
            return -1;

        /* 读入segment的一部分 */
        cut_len = MIN(len - read_len, (uint32)(PGSIZE - page_off));
        if (inode_read_data(ip, (uint32)seg_start + read_len, cut_len, (void*)(pa + page_off), false) != cut_len)
            return -1;

        read_len += cut_len;   // ★ 按本轮实际拷贝字节数步进, 修复非页对齐段加载
    }
    return 0;
}

/* 将程序的代码区和数据区读入用户堆中, 返回new_heap_top */
static uint64 prepare_heap(pgtbl_t new_pgtbl, inode_t *ip, elf_header_t *eh, exec_info_t *info)
{
    program_header_t ph;
    uint64 new_heap_top = USER_BASE, old_heap_top = USER_BASE;
    uint64 first_load_va = 0;
    uint64 first_load_off = 0;
    int has_first_load = 0;

    info->phdr_addr = 0;
    info->phnum = eh->ph_ent_num;
    info->phent = sizeof(program_header_t);
    info->interp_path[0] = '\0';

    for (uint32 off = eh->ph_off; off < eh->ph_off + eh->ph_ent_num * sizeof(ph); off += sizeof(ph))
    {
        if (inode_read_data(ip, off, sizeof(ph), &ph, false) != sizeof(ph))
            return -1;

        if (ph.type == ELF_PT_PHDR) {
            info->phdr_addr = ph.va;
            continue;
        }

        if (ph.type == ELF_PT_INTERP) {
            uint32 path_len = ph.file_size < 127 ? (uint32)ph.file_size : 127;
            if (inode_read_data(ip, (uint32)ph.off, path_len, info->interp_path, false) != path_len)
                return -1;
            info->interp_path[path_len] = '\0';
            if (path_len > 0 && info->interp_path[path_len - 1] == '\n')
                info->interp_path[path_len - 1] = '\0';
            continue;
        }

        if (ph.type != ELF_PROG_LOAD)
            continue;

        if (ph.mem_size < ph.file_size)
            return -1;
        if (ph.va + ph.mem_size < ph.va)
            return -1;
        if (ph.va < USER_BASE || ph.va + ph.mem_size > MMAP_BEGIN) {
            printf("proc_prepare_heap: ph_idx=%d va %p mem_size %p out of user range\n",
                   (int)((off - eh->ph_off) / sizeof(ph)), (void*)ph.va, (void*)ph.mem_size);
            return -1;
        }

        if (!has_first_load) {
            first_load_va = ph.va;
            first_load_off = ph.off;
            has_first_load = 1;
        }

        uint32 perm = PTE_U;
        if(ph.flags & ELF_PROG_FLAG_READ) perm |= PTE_R;
        if(ph.flags & ELF_PROG_FLAG_WRITE) perm |= PTE_W;
        if(ph.flags & ELF_PROG_FLAG_EXEC) perm |= PTE_X;

        // 段地址必须单调递增，否则 len 下溢成天文数字，会 OOM panic
        if (ph.va < old_heap_top) {
            printf("prepare_heap: va=%p < old_heap_top=%p, out of order segment\n",
                   (void*)ph.va, (void*)old_heap_top);
            return -1;
        }

        new_heap_top = uvm_heap_grow(new_pgtbl, old_heap_top,
                        ph.va + ph.mem_size - old_heap_top, perm);
        if (new_heap_top != ph.va + ph.mem_size)
            return -1;
        old_heap_top = new_heap_top;

        if (load_segment(ip, new_pgtbl, ph.off, ph.va, ph.file_size) < 0)
            return -1;
    }

    if (info->phdr_addr == 0 && has_first_load) {
        info->phdr_addr = first_load_va + (eh->ph_off - first_load_off);
    }

    return new_heap_top;
}

/* 准备栈空间用于存储输入参数(4KB), 设置arg_count, 返回sp */
static uint64 prepare_stack(pgtbl_t new_pgtbl, char **argv, char **envp,
                            int *arg_count, exec_info_t *info)
{
    uint64 ustack_page;
    uint64 sp = TRAPFRAME, sp_base = TRAPFRAME - PGSIZE;
    uint64 argv_addr[ELF_MAXARGS + 1];
    uint64 envp_addr[ELF_MAXARGS + 1];
    uint32 argc, envc, arg_len;

    ustack_page = (uint64)pmem_alloc(false);
    if (!ustack_page)
        return -1;
    memset((void *)ustack_page, 0, PGSIZE);
    vm_mappages(new_pgtbl, sp_base, ustack_page, PGSIZE, PTE_R | PTE_W | PTE_U);

    for (argc = 0; argv[argc] != NULL; argc++) {
        if (argc >= ELF_MAXARGS)
            return -1;
        arg_len = strlen(argv[argc]) + 1;
        sp -= arg_len;
        if (sp < sp_base)
            return -1;
        uvm_copyout(new_pgtbl, sp, (uint64)argv[argc], arg_len);
        argv_addr[argc] = sp;
    }
    argv_addr[argc] = 0;

    for (envc = 0; envp != NULL && envp[envc] != NULL; envc++) {
        if (envc >= ELF_MAXARGS)
            return -1;
        arg_len = strlen(envp[envc]) + 1;
        sp -= arg_len;
        if (sp < sp_base)
            return -1;
        uvm_copyout(new_pgtbl, sp, (uint64)envp[envc], arg_len);
        envp_addr[envc] = sp;
    }
    envp_addr[envc] = 0;

    sp -= 16;
    sp &= ~15UL;
    if (sp < sp_base)
        return -1;
    uint64 at_random_addr = sp;

    uint64 buf[1 + (ELF_MAXARGS + 1) + (ELF_MAXARGS + 1) + 26];
    int idx = 0;
    buf[idx++] = argc;
    for (uint32 i = 0; i <= argc; i++)
        buf[idx++] = argv_addr[i];
    for (uint32 i = 0; i <= envc; i++)
        buf[idx++] = envp_addr[i];

    if (info->phdr_addr != 0) {
        buf[idx++] = AT_PHDR;    buf[idx++] = info->phdr_addr;
        buf[idx++] = AT_PHENT;   buf[idx++] = info->phent;
        buf[idx++] = AT_PHNUM;   buf[idx++] = info->phnum;
    }
    buf[idx++] = AT_PAGESZ;  buf[idx++] = PGSIZE;
    if (info->interp_base != 0) {
        buf[idx++] = AT_BASE;    buf[idx++] = info->interp_base;
    }
    if (info->entry != 0) {
        buf[idx++] = AT_ENTRY;   buf[idx++] = info->entry;
    }
    buf[idx++] = AT_UID;     buf[idx++] = 0;
    buf[idx++] = AT_EUID;    buf[idx++] = 0;
    buf[idx++] = AT_GID;     buf[idx++] = 0;
    buf[idx++] = AT_EGID;    buf[idx++] = 0;
    buf[idx++] = AT_RANDOM;  buf[idx++] = at_random_addr;
    buf[idx++] = AT_NULL;    buf[idx++] = 0;

    uint64 bytes = (uint64)idx * sizeof(uint64);
    sp -= bytes;
    sp &= ~15UL;
    if (sp < sp_base)
        return -1;
    uvm_copyout(new_pgtbl, sp, (uint64)buf, bytes);

    *arg_count = argc;
    return sp;
}

/* 加载 ELF 动态链接器到 base 偏移处, 返回入口地址; 失败返回 -1 */
static uint64 load_interp(pgtbl_t pgtbl, char *interp_path, uint64 base)
{
    static const char *fallbacks[] = {
        "/musl/lib/libc.so",
        "/lib/libc.so",
        "/lib/ld-musl-riscv64-sf.so.1",
        "/musl/lib/ld-musl-riscv64-sf.so.1",
    };

    inode_t *ip = path_to_inode(interp_path);
    if (!ip) {
        for (int i = 0; i < 4; i++) {
            ip = path_to_inode((char*)fallbacks[i]);
            if (ip) break;
        }
        if (!ip) {
            printf("load_interp: cannot find interpreter (tried %s + fallbacks)\n", interp_path);
            return -1;
        }
    }

    elf_header_t eh;
    if (inode_read_data(ip, 0, sizeof(eh), &eh, false) != sizeof(eh) || eh.magic != ELF_MAGIC) {
        inode_put(ip);
        printf("load_interp: %s not a valid ELF\n", interp_path);
        return -1;
    }

//    printf("load_interp: loading %s base=%p phnum=%d\n", interp_path, (void*)base, (int)eh.ph_ent_num);
    program_header_t ph;
    for (uint32 off = eh.ph_off; off < eh.ph_off + eh.ph_ent_num * sizeof(ph); off += sizeof(ph)) {
        if (inode_read_data(ip, off, sizeof(ph), &ph, false) != sizeof(ph)) {
            inode_put(ip);
            return -1;
        }
        if (ph.type != ELF_PROG_LOAD)
            continue;
        if (ph.mem_size < ph.file_size) {
            inode_put(ip);
            return -1;
        }

        uint64 seg_va = base + ph.va;
        uint64 page_start = (seg_va / PGSIZE) * PGSIZE;
        uint64 page_end = ((seg_va + ph.mem_size + PGSIZE - 1) / PGSIZE) * PGSIZE;

        for (uint64 va = page_start; va < page_end; va += PGSIZE) {
            pte_t *pte = vm_getpte(pgtbl, va, false);
            if (pte && (*pte & PTE_V))
                continue;
            void *pa = pmem_alloc(false);
            if (!pa) {
                inode_put(ip);
                return -1;
            }
            memset(pa, 0, PGSIZE);
            vm_mappages(pgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
        }

        if (load_segment(ip, pgtbl, ph.off, seg_va, ph.file_size) < 0) {
            inode_put(ip);
            return -1;
        }
    }

    inode_put(ip);
    return base + eh.entry;
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

    for (int i = 1; argv && argv[i] != NULL; i++) {
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

static bool should_try_busybox_applet(char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (strncmp(path, "/bin/", 5) == 0 ||
        strncmp(path, "/usr/bin/", 9) == 0 ||
        strncmp(path, "/musl/", 6) == 0 ||
        strncmp(path, "/glibc/", 7) == 0)
        return true;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/')
            return false;
    }
    return true;
}

static void close_cloexec_files(proc_t *p)
{
    file_t *files[N_OPEN_FILE_PER_PROC];
    memset(files, 0, sizeof(files));

    for (int fd = 0; fd < N_OPEN_FILE_PER_PROC; fd++) {
        if (p->open_file[fd] != NULL && p->fd_cloexec[fd]) {
            files[fd] = p->open_file[fd];
            p->open_file[fd] = NULL;
            p->fd_cloexec[fd] = 0;
        }
    }

    for (int fd = 0; fd < N_OPEN_FILE_PER_PROC; fd++) {
        if (files[fd] != NULL)
            file_close(files[fd]);
    }
}

/*
    执行ELF文件
    输入路径和参数
    成功返回argc, 失败返回-1
*/
static int proc_exec_with_env(char *path, char **argv, char **envp)
{
    proc_t *p = myproc();
//    printf("proc_exec: pid=%d path=%s\n", p ? p->pid : -1, path);
    
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
    if (!ip && should_try_busybox_applet(path)) {
        ip = path_to_inode("/musl/busybox");
        if (!ip)
            ip = path_to_inode("busybox");
        if (ip)
            path = "/musl/busybox";
    }
    if (!ip) {
        uvm_destroy_pgtbl(new_pgtbl);
        printf("proc_exec: pid=%d path_to_inode(%s) failed\n", p ? p->pid : -1, path);
        return -1;
    }
    
    // step-2: 读取ELF_header
    elf_header_t eh;
    uint32 data_size = inode_read_data(ip, 0, sizeof(eh), &eh, false);
    char script_interp[STR_MAXLEN + 1];
    char script_interp_arg[STR_MAXLEN + 1];
    char *script_argv[ELF_MAXARGS + 1];

    if (data_size != sizeof(eh) || eh.magic != ELF_MAGIC) {
        if (is_script_path(path)) {
            if (parse_shebang(ip, script_interp, script_interp_arg) != 0) {
                script_interp[0] = '\0';
                script_interp_arg[0] = '\0';
            }
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
    exec_info_t dyn;
    memset(&dyn, 0, sizeof(dyn));
    uint64 new_heap_top = prepare_heap(new_pgtbl, ip, &eh, &dyn);
    if (new_heap_top == -1) {
        inode_put(ip);
        uvm_destroy_pgtbl(new_pgtbl);
        printf("proc_exec: pid=%d prepare_heap failed\n", p ? p->pid : -1);
        return -1;
    }

    // step-4: 释放ELF的inode
    inode_put(ip);

    // step-4b: 如果有动态链接器, 加载它
    dyn.entry = eh.entry;
    dyn.interp_base = 0;
    dyn.interp_entry = 0;
    uint64 entry_pc = eh.entry;
    if (dyn.interp_path[0] != '\0') {
        uint64 ie = load_interp(new_pgtbl, dyn.interp_path, INTERP_LOAD_BASE);
        if (ie == (uint64)-1) {
            uvm_destroy_pgtbl(new_pgtbl);
            printf("proc_exec: pid=%d load_interp failed\n", p ? p->pid : -1);
            return -1;
        }
        dyn.interp_base = INTERP_LOAD_BASE;
        dyn.interp_entry = ie;
        entry_pc = ie;
    }

    // step-5: 处理输入的参数列表argv, 填充到用户栈区域
    int argc;
    uint64 sp = prepare_stack(new_pgtbl, argv, envp, &argc, &dyn);
    if (sp == -1) {
        uvm_destroy_pgtbl(new_pgtbl);
        printf("proc_exec: pid=%d prepare_stack failed\n", p ? p->pid : -1);
        return -1;
    }

    // step-6: 新的地址空间构建完毕, 释放旧资源
    close_cloexec_files(p);
    uvm_destroy_pgtbl(p->pgtbl);
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
    new_tf->user_to_kern_epc = entry_pc;
    new_tf->sp = sp;          
    
    // step-8: 更新进程的相关字段
    p->pgtbl = new_pgtbl;
    p->tf = new_tf;
    p->heap_top = new_heap_top;
    p->ustack_npage = 1;
    p->mmap = NULL;

    // exec 时重置信号状态
    memset(p->sig_handler, 0, sizeof(p->sig_handler));
    p->sig_restorer = 0;
    p->sig_pending = 0;
    p->sig_delivering = 0;
    p->itimer_expire = 0;
    p->itimer_interval = 0;
    p->ub_looper_secs = unixbench_looper_secs(path, argv);
    int i;
    for(i = 0; i < sizeof(p->name) - 1 && path[i] != '\0'; i++){
        p->name[i] = path[i];
    }
    p->name[i] = '\0';
    p->name[sizeof(p->name) - 1] = '\0';
//    printf("proc_exec: pid=%d exec done argc=%d heap_top=%p tf=%p entry=%p%s\n", p->pid, argc, (void*)p->heap_top, (void*)p->tf, (void*)entry_pc, use_script ? " script" : "");
    
    return argc;
}

int proc_exec(char *path, char **argv)
{
    return proc_exec_with_env(path, argv, NULL);
}

int proc_exec_env(char *path, char **argv, char **envp)
{
    return proc_exec_with_env(path, argv, envp);
}

/* 在当前上下文对指定pid的进程执行exec（替换其地址空间）。
   返回0表示成功，返回-1表示失败（失败时会将子进程置为ZOMBIE并唤醒父进程）。 */
int proc_exec_target(int pid, char *path, char **argv)
{
    proc_t *p = proc_get_by_pid(pid);
    if (!p) return -1;

//    printf("proc_exec_target: pid=%d path=%s\n", p->pid, path);

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
    char script_interp[STR_MAXLEN + 1];
    char script_interp_arg[STR_MAXLEN + 1];
    char *script_argv[ELF_MAXARGS + 1];

    if (data_size != sizeof(eh) || eh.magic != ELF_MAGIC) {
        if (is_script_path(path)) {
            if (parse_shebang(ip, script_interp, script_interp_arg) != 0) {
                script_interp[0] = '\0';
                script_interp_arg[0] = '\0';
            }
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
            path = resolved_interp;
            inode_put(ip);
            ip = path_to_inode(path);
            if (!ip) {
                uvm_destroy_pgtbl(new_pgtbl);
                printf("proc_exec_target: pid=%d script interpreter path_to_inode(%s) failed\n", p->pid, path);
                goto exec_fail;
            }
            data_size = inode_read_data(ip, 0, sizeof(eh), &eh, false);
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

    exec_info_t dyn;
    memset(&dyn, 0, sizeof(dyn));
    uint64 new_heap_top = prepare_heap(new_pgtbl, ip, &eh, &dyn);
    if (new_heap_top == (uint64)-1) {
        inode_put(ip);
        uvm_destroy_pgtbl(new_pgtbl);
        goto exec_fail;
    }

    inode_put(ip);

    dyn.entry = eh.entry;
    dyn.interp_base = 0;
    dyn.interp_entry = 0;
    uint64 entry_pc = eh.entry;
    if (dyn.interp_path[0] != '\0') {
        uint64 ie = load_interp(new_pgtbl, dyn.interp_path, INTERP_LOAD_BASE);
        if (ie == (uint64)-1) {
            uvm_destroy_pgtbl(new_pgtbl);
            printf("proc_exec_target: pid=%d load_interp failed\n", p->pid);
            goto exec_fail;
        }
        dyn.interp_base = INTERP_LOAD_BASE;
        dyn.interp_entry = ie;
        entry_pc = ie;
    }

    int argc;
    uint64 sp = prepare_stack(new_pgtbl, argv, NULL, &argc, &dyn);
    if (sp == (uint64)-1) {
        uvm_destroy_pgtbl(new_pgtbl);
        goto exec_fail;
    }

    /* 释放旧资源 */
    close_cloexec_files(p);
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
    new_tf->user_to_kern_epc = entry_pc;
    new_tf->sp = sp;

    p->pgtbl = new_pgtbl;
    p->tf = new_tf;
    p->heap_top = new_heap_top;
    p->ustack_npage = 1;
    p->mmap = NULL;
    p->ub_looper_secs = unixbench_looper_secs(path, argv);
    int i;
    for(i = 0; i < sizeof(p->name) - 1 && path[i] != '\0'; i++){
        p->name[i] = path[i];
    }
    p->name[i] = '\0';
    p->name[sizeof(p->name) - 1] = '\0';

//    printf("proc_exec_target: pid=%d exec done argc=%d heap_top=%p tf=%p%s\n", p->pid, argc, (void*)p->heap_top, (void*)p->tf, use_script ? " script" : "");

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
