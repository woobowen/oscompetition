/*
 * LoongArch syscall dispatch.
 *
 * Syscall numbers match the RISC-V / SeaOS ABI (same as initcode expects).
 * Arguments: a0–a5, syscall number in a7.
 * Return value in a0.
 *
 * Implemented syscalls:
 *   Process:  fork, wait, exit, getpid, exec
 *   File I/O: open, close, read, write, lseek, dup, fstat, get_dentries
 *   Dir:      chdir, mkdir
 *   Memory:   brk, mmap, munmap
 *   System:   shutdown
 */
#include "early_boot.h"
#include "trap.h"
#include "proc.h"

/* ---- Syscall numbers (same as RISC-V SeaOS ABI) ---- */
#define SYS_fork         4
#define SYS_mkdir       34
#define SYS_unlink      35
#define SYS_link        37
#define SYS_chdir       49
#define SYS_dup         23
#define SYS_lseek       62
#define SYS_get_dentries 61
#define SYS_read        63
#define SYS_write       64
#define SYS_open        56
#define SYS_close       57
#define SYS_exit        93
#define SYS_getpid     172
#define SYS_fstat       80
#define SYS_brk        214
#define SYS_munmap     215
#define SYS_mmap       222
#define SYS_exec       221
#define SYS_wait       260
#define SYS_shutdown   502

#define LA_ENOSYS 38

/* ---- Root inode numbers (set by fs_la.c after mount) ---- */
#define LA_ROOT_INO_SEA  0
#define LA_ROOT_INO_E4   2

/* ================================================================
 *  Syscall implementations
 * ================================================================ */

/* SYS_getpid: return current process ID */
static uint64_t sys_getpid(struct la_trap_frame *tf)
{
    (void)tf;
    struct la_proc *p = la_current_proc();
    return p ? (uint64_t)p->pid : 0;
}

/* SYS_write: write buf to fd.
 * fd 0-2 (console) → UART.
 * file fd → write to disk. */
static uint64_t sys_write(struct la_trap_frame *tf)
{
    int fd      = (int)tf->gpr[LA_GPR_A0];
    uint64_t buf = tf->gpr[LA_GPR_A1];
    uint32_t len = (uint32_t)tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p) return (uint64_t)-1;

    /* Console output (stdin/stdout/stderr) */
    if (fd >= 0 && fd <= 2) {
        /* Copy from user space to kernel temp buffer, then UART */
        uint32_t written = 0;
        while (written < len) {
            uint64_t va = buf + written;
            uint64_t page_off = va & 0xFFFUL;
            uint32_t chunk = LA_PGSIZE - page_off;
            if (chunk > len - written) chunk = len - written;

            /* Walk user page table */
            uint64_t pa = 0;
            if (p->pgtbl) {
                uint64_t idx0 = (va >> 30) & 0x1FF;
                uint64_t e0 = p->pgtbl[idx0];
                if (e0 & 1) {
                    uint64_t *mid = (uint64_t *)(e0 & ~0xFFFUL);
                    uint64_t idx1 = (va >> 21) & 0x1FF;
                    uint64_t e1 = mid[idx1];
                    if (e1 & 1) {
                        uint64_t *leaf = (uint64_t *)(e1 & ~0xFFFUL);
                        uint64_t idx2 = (va >> 12) & 0x1FF;
                        uint64_t e2 = leaf[idx2];
                        if (e2 & 1)
                            pa = (e2 & ~0xFFFUL) + page_off;
                    }
                }
            }
            if (!pa) break;

            const char *s = (const char *)pa;
            for (uint32_t i = 0; i < chunk; i++)
                la_uart_putc(s[i]);
            written += chunk;
        }
        return written;
    }

    /* File fd */
    if (fd < 0 || fd >= LA_NFD || p->fds[fd].type != LA_FD_FILE)
        return (uint64_t)-1;

    /* Read-only filesystem — return error for write */
    return (uint64_t)-1;
}

/* SYS_read: read from fd into buf */
static uint64_t sys_read(struct la_trap_frame *tf)
{
    int fd       = (int)tf->gpr[LA_GPR_A0];
    uint64_t buf = tf->gpr[LA_GPR_A1];
    uint32_t len = (uint32_t)tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p) return (uint64_t)-1;

    if (fd < 0 || fd >= LA_NFD)
        return (uint64_t)-1;

    /* Console stdin — no input available */
    if (p->fds[fd].type == LA_FD_CONSOLE)
        return 0;

    /* File fd */
    if (p->fds[fd].type != LA_FD_FILE)
        return (uint64_t)-1;

    uint32_t ino = p->fds[fd].ino;
    uint32_t offset = p->fds[fd].offset;

    /* Read into a kernel buffer, then copy to user */
    static __attribute__((aligned(8))) char tmpbuf[4096];
    uint32_t done = 0;
    while (done < len) {
        uint32_t chunk = 4096;
        if (chunk > len - done) chunk = len - done;
        uint32_t n = la_fs_read_file(ino, offset + done, tmpbuf, chunk);
        if (n == 0) break;
        la_copy_to_user(buf + done, tmpbuf, n);
        done += n;
        if (n < chunk) break;
    }
    p->fds[fd].offset = offset + done;
    return done;
}

/* SYS_open: open a file, return fd */
static uint64_t sys_open(struct la_trap_frame *tf)
{
    uint64_t upath = tf->gpr[LA_GPR_A0];
    uint32_t flags = (uint32_t)tf->gpr[LA_GPR_A1];
    struct la_proc *p = la_current_proc();
    (void)flags;

    if (!p) return (uint64_t)-1;

    /* Copy path from user */
    char path[256];
    if (la_copy_str_from_user(path, upath, sizeof(path) - 1) < 0)
        return (uint64_t)-1;

    /* Resolve path to inode */
    uint32_t ino;
    if (la_fs_lookup(path, &ino) < 0)
        return (uint64_t)-1;

    /* Allocate fd */
    int fd = -1;
    for (int i = 0; i < LA_NFD; i++) {
        if (p->fds[i].type == LA_FD_UNUSED) {
            fd = i;
            break;
        }
    }
    if (fd < 0) return (uint64_t)-1;

    p->fds[fd].ino = ino;
    p->fds[fd].offset = 0;
    p->fds[fd].type = LA_FD_FILE;
    p->fds[fd].writable = (flags & 0x04) ? 1 : 0;
    return (uint64_t)fd;
}

/* SYS_close: close a file descriptor */
static uint64_t sys_close(struct la_trap_frame *tf)
{
    int fd = (int)tf->gpr[LA_GPR_A0];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD)
        return (uint64_t)-1;
    if (p->fds[fd].type == LA_FD_UNUSED)
        return (uint64_t)-1;

    p->fds[fd].ino = 0;
    p->fds[fd].offset = 0;
    p->fds[fd].type = LA_FD_UNUSED;
    p->fds[fd].writable = 0;
    return 0;
}

/* SYS_lseek: adjust file offset */
static uint64_t sys_lseek(struct la_trap_frame *tf)
{
    int fd       = (int)tf->gpr[LA_GPR_A0];
    int64_t off  = (int64_t)tf->gpr[LA_GPR_A1];
    int whence   = (int)tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD || p->fds[fd].type != LA_FD_FILE)
        return (uint64_t)-1;

    uint32_t new_off;
    if (whence == 0)        /* SET */
        new_off = (uint32_t)off;
    else if (whence == 1)   /* ADD */
        new_off = p->fds[fd].offset + (uint32_t)off;
    else if (whence == 2)   /* SUB */
        new_off = p->fds[fd].offset - (uint32_t)off;
    else
        return (uint64_t)-1;

    p->fds[fd].offset = new_off;
    return new_off;
}

/* SYS_dup: duplicate fd */
static uint64_t sys_dup(struct la_trap_frame *tf)
{
    int fd = (int)tf->gpr[LA_GPR_A0];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD || p->fds[fd].type == LA_FD_UNUSED)
        return (uint64_t)-1;

    int newfd = -1;
    for (int i = 0; i < LA_NFD; i++) {
        if (p->fds[i].type == LA_FD_UNUSED) { newfd = i; break; }
    }
    if (newfd < 0) return (uint64_t)-1;

    p->fds[newfd] = p->fds[fd];
    return (uint64_t)newfd;
}

/* SYS_fstat: get file status */
static uint64_t sys_fstat(struct la_trap_frame *tf)
{
    int fd        = (int)tf->gpr[LA_GPR_A0];
    uint64_t udst = tf->gpr[LA_GPR_A1];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD || p->fds[fd].type != LA_FD_FILE)
        return (uint64_t)-1;

    /* Build file_stat_t (matching help.h layout):
     *   uint16 type, nlink; uint32 size, inode_num, offset; */
    struct {
        uint16_t type;
        uint16_t nlink;
        uint32_t size;
        uint32_t inode_num;
        uint32_t offset;
    } stat;

    int ft = la_fs_inode_type(p->fds[fd].ino);
    stat.type = (uint16_t)ft;
    stat.nlink = 1;
    stat.size = la_fs_inode_size(p->fds[fd].ino);
    stat.inode_num = p->fds[fd].ino;
    stat.offset = p->fds[fd].offset;

    la_copy_to_user(udst, &stat, sizeof(stat));
    return 0;
}

/* SYS_get_dentries: read directory entries (Linux dirent64 format) */
static uint64_t sys_get_dentries(struct la_trap_frame *tf)
{
    int fd        = (int)tf->gpr[LA_GPR_A0];
    uint64_t ubuf = tf->gpr[LA_GPR_A1];
    uint32_t len  = (uint32_t)tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD || p->fds[fd].type != LA_FD_FILE)
        return (uint64_t)-1;

    /* Read dentries into a kernel buffer, then copy to user */
    static __attribute__((aligned(8))) char dentbuf[4096];
    if (len > sizeof(dentbuf)) len = sizeof(dentbuf);

    uint32_t n = la_fs_get_dentries(p->fds[fd].ino, dentbuf, len);
    if (n == 0) return 0;

    la_copy_to_user(ubuf, dentbuf, n);
    return n;
}

/* SYS_chdir: change current working directory */
static uint64_t sys_chdir(struct la_trap_frame *tf)
{
    uint64_t upath = tf->gpr[LA_GPR_A0];
    struct la_proc *p = la_current_proc();

    if (!p) return (uint64_t)-1;

    char path[256];
    if (la_copy_str_from_user(path, upath, sizeof(path) - 1) < 0)
        return (uint64_t)-1;

    uint32_t ino;
    if (la_fs_lookup(path, &ino) < 0)
        return (uint64_t)-1;

    /* Verify it's a directory */
    if (la_fs_inode_type(ino) != 1)
        return (uint64_t)-1;

    p->cwd_ino = ino;
    return 0;
}

/* SYS_mkdir: create directory (not supported on read-only FS) */
static uint64_t sys_mkdir(struct la_trap_frame *tf)
{
    (void)tf;
    return (uint64_t)-1;
}

/* SYS_fork: create a copy of the current process */
static uint64_t sys_fork(struct la_trap_frame *tf)
{
    struct la_proc *parent = la_current_proc();
    if (!parent) return (uint64_t)-1;

    /* Allocate new process */
    struct la_proc *child = la_proc_create_user("child");
    if (!child) return (uint64_t)-1;

    /* Copy user page table (deep copy) */
    if (parent->pgtbl) {
        uint64_t *new_pgtbl = la_uvm_create();
        if (!new_pgtbl) {
            child->state = LA_PROC_UNUSED;
            return (uint64_t)-1;
        }
        if (la_uvm_copy_pgtbl(parent->pgtbl, new_pgtbl) < 0) {
            child->state = LA_PROC_UNUSED;
            return (uint64_t)-1;
        }
        child->pgtbl = new_pgtbl;
    }

    /* Copy trap frame to a separate page (same as first proc) */
    struct la_trap_frame *ctf = (struct la_trap_frame *)la_pmem_alloc();
    if (!ctf) {
        child->state = LA_PROC_UNUSED;
        return (uint64_t)-1;
    }
    /* Copy parent's trap frame */
    uint64_t *s = (uint64_t *)tf;
    uint64_t *d = (uint64_t *)ctf;
    uint64_t *e = (uint64_t *)(ctf + 1);
    while (d < e) *d++ = *s++;
    ctf->gpr[LA_GPR_A0] = 0;  /* child returns 0 from fork */
    child->tf = ctf;

    /* Copy file descriptors */
    for (int i = 0; i < LA_NFD; i++)
        child->fds[i] = parent->fds[i];

    /* Inherit other state */
    child->parent_pid = parent->pid;
    child->heap_top   = parent->heap_top;
    child->cwd_ino    = parent->cwd_ino;

    return (uint64_t)child->pid;
}

/* Forward declaration */
uint64_t la_do_exec_syscall(struct la_trap_frame *tf, const char *path,
                            uint64_t uargv);

/* SYS_exec: replace process image with new program */
static uint64_t sys_exec(struct la_trap_frame *tf)
{
    uint64_t upath = tf->gpr[LA_GPR_A0];
    uint64_t uargv = tf->gpr[LA_GPR_A1];

    /* Copy path from user */
    char path[256];
    if (la_copy_str_from_user(path, upath, sizeof(path) - 1) < 0)
        return (uint64_t)-1;

    return la_do_exec_syscall(tf, path, uargv);
}

/* SYS_wait: wait for a child process to exit */
static uint64_t sys_wait(struct la_trap_frame *tf)
{
    int wait_pid     = (int)tf->gpr[LA_GPR_A0];
    uint64_t ustatus = tf->gpr[LA_GPR_A1];
    struct la_proc *parent = la_current_proc();

    if (!parent) return (uint64_t)-1;

    for (;;) {
        /* Scan for children */
        int has_children = 0;
        struct la_proc *child = 0;

        struct la_proc *procs = la_proc_table();
        for (int i = 0; i < LA_NPROC; i++) {
            struct la_proc *p = &procs[i];
            if (p->parent_pid != parent->pid || p->state == LA_PROC_UNUSED)
                continue;
            has_children = 1;

            /* Check PID filter */
            if (wait_pid > 0 && p->pid != wait_pid)
                continue;

            if (p->state == LA_PROC_ZOMBIE) {
                child = p;
                break;
            }
        }

        if (child) {
            /* Copy exit status to user */
            if (ustatus) {
                int wstatus = (child->exit_code & 0xff) << 8;
                la_copy_to_user(ustatus, &wstatus, sizeof(int));
            }
            int cpid = child->pid;
            /* Reclaim child — just mark unused */
            child->state = LA_PROC_UNUSED;
            return (uint64_t)cpid;
        }

        if (!has_children)
            return (uint64_t)-1;  /* no children */

        /* No zombie child yet — sleep and retry */
        la_proc_sleep();
    }
}

/* SYS_exit: terminate current process */
static uint64_t __attribute__((noreturn)) sys_exit(struct la_trap_frame *tf)
{
    uint32_t exit_code = (uint32_t)tf->gpr[LA_GPR_A0];
    struct la_proc *me = la_current_proc();

    if (me) {
        me->exit_code = (int)exit_code;
        me->state = LA_PROC_ZOMBIE;

        /* Wake up parent so it can collect our exit status */
        if (me->parent_pid > 0)
            la_proc_wakeup_pid(me->parent_pid);
    }

    /* Switch back to scheduler — never returns */
    la_sched_switch(&me->ctx);

    for (;;) {}
}

/* SYS_brk: adjust program break */
static uint64_t sys_brk(struct la_trap_frame *tf)
{
    uint64_t addr = tf->gpr[LA_GPR_A0];
    struct la_proc *p = la_current_proc();

    if (!p) return (uint64_t)-1;

    if (addr == 0)
        return p->heap_top;

    if (addr < LA_USER_BASE)
        return (uint64_t)-1;

    p->heap_top = addr;
    return addr;
}

/* SYS_mmap: map memory (simplified — just allocate pages) */
static uint64_t sys_mmap(struct la_trap_frame *tf)
{
    uint64_t addr  = tf->gpr[LA_GPR_A0];
    uint32_t len   = (uint32_t)tf->gpr[LA_GPR_A1];
    /* int prot     = (int)tf->gpr[LA_GPR_A2]; */
    /* int flags    = (int)tf->gpr[LA_GPR_A3]; */
    /* int fd       = (int)tf->gpr[LA_GPR_A4]; */
    /* uint64_t off = tf->gpr[LA_GPR_A5]; */
    struct la_proc *p = la_current_proc();

    if (!p || !p->pgtbl || len == 0)
        return (uint64_t)-1;

    /* Round up to page size */
    uint32_t npages = (len + LA_PGSIZE - 1) / LA_PGSIZE;

    /* If addr is 0, use heap_top */
    if (addr == 0) {
        addr = (p->heap_top + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
        p->heap_top = addr + (uint64_t)npages * LA_PGSIZE;
    }

    /* Allocate and map pages */
    for (uint32_t i = 0; i < npages; i++) {
        uint64_t va = addr + (uint64_t)i * LA_PGSIZE;
        if (la_uvm_alloc_page(p->pgtbl, va, 0x8FUL) == 0)
            return (uint64_t)-1;  /* 0x8F = user RWX */
    }

    return addr;
}

/* SYS_munmap: unmap memory (stub — just return success) */
static uint64_t sys_munmap(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_shutdown: halt the system */
static uint64_t sys_shutdown(void)
{
    la_uart_puts("shutdown: system halting\n");
    for (;;) {
        asm volatile("idle 0" ::: "memory");
    }
    return 0;
}

/* ---- Main syscall dispatcher ---- */
uint64_t la_syscall_dispatch(struct la_trap_frame *tf)
{
    uint64_t sysno = tf->gpr[LA_GPR_A7];

    switch (sysno) {
    /* Process management */
    case SYS_fork:       return sys_fork(tf);
    case SYS_wait:       return sys_wait(tf);
    case SYS_exit:       sys_exit(tf); __builtin_unreachable();
    case SYS_getpid:     return sys_getpid(tf);
    case SYS_exec:       return sys_exec(tf);

    /* File I/O */
    case SYS_open:       return sys_open(tf);
    case SYS_close:      return sys_close(tf);
    case SYS_read:       return sys_read(tf);
    case SYS_write:      return sys_write(tf);
    case SYS_lseek:      return sys_lseek(tf);
    case SYS_dup:        return sys_dup(tf);
    case SYS_fstat:      return sys_fstat(tf);
    case SYS_get_dentries: return sys_get_dentries(tf);

    /* Directory */
    case SYS_chdir:      return sys_chdir(tf);
    case SYS_mkdir:      return sys_mkdir(tf);

    /* Memory */
    case SYS_brk:        return sys_brk(tf);
    case SYS_mmap:       return sys_mmap(tf);
    case SYS_munmap:     return sys_munmap(tf);

    /* System */
    case SYS_shutdown:   return sys_shutdown();

    default:
        break;
    }

    /* Unimplemented syscall */
    return (uint64_t)(-LA_ENOSYS);
}
