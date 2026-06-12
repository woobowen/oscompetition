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

/* ---- Syscall numbers (LoongArch asm-generic ABI) ---- */
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
/* Additional LoongArch syscalls needed by busybox */
#define SYS_set_tid_address  96
#define SYS_set_robust_list  99
#define SYS_futex            98
#define SYS_nanosleep       101
#define SYS_clock_gettime   113
#define SYS_rt_sigaction    134
#define SYS_rt_sigprocmask  135
#define SYS_msync           144
#define SYS_uname           160
#define SYS_getuid          174
#define SYS_geteuid         175
#define SYS_getgid          176
#define SYS_getegid         177
#define SYS_gettid          178
#define SYS_mprotect        226
#define SYS_statx           291
#define SYS_exit_group       94
#define SYS_pipe2            59
#define SYS_dup3             24
#define SYS_waitid           95
#define SYS_clone           220
#define SYS_ioctl            29
#define SYS_getcwd           17
#define SYS_getppid         173
#define SYS_newfstatat       79
#define SYS_faccessat        48
#define SYS_readlinkat       78
#define SYS_fcntl            25
#define SYS_sched_yield     124
#define SYS_prlimit64       261
#define SYS_setrlimit       139
#define SYS_getrlimit       140

#define LA_ENOSYS 38

/* POSIX errno values.  Syscall failures return (uint64_t)(-errno) so that
 * userland (musl) sees a value in [-4096,-1] and decodes the errno.
 * Returning a bare (uint64_t)-1 is read by musl as errno 1 = EPERM
 * ("Operation not permitted"), which masks the real failure. */
#define LA_EPERM    1
#define LA_ENOENT   2
#define LA_EINTR    4
#define LA_EIO      5
#define LA_EBADF    9
#define LA_ECHILD  10
#define LA_EAGAIN  11
#define LA_ENOMEM  12
#define LA_EACCES  13
#define LA_EFAULT  14
#define LA_EEXIST  17
#define LA_ENOTDIR 20
#define LA_EISDIR  21
#define LA_EINVAL  22
#define LA_EMFILE  24
#define LA_ENOSPC  28
#define LA_ESPIPE  29
#define LA_ENAMETOOLONG 36

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

            /* Walk user page table (dir entries are raw PAs, leaf has flags) */
            uint64_t pa = 0;
            if (p->pgtbl) {
                uint64_t idx0 = (va >> 30) & 0x1FF;
                uint64_t e0 = p->pgtbl[idx0];
                if (e0) {
                    uint64_t *mid = (uint64_t *)e0;
                    uint64_t idx1 = (va >> 21) & 0x1FF;
                    uint64_t e1 = mid[idx1];
                    if (e1) {
                        uint64_t *leaf = (uint64_t *)e1;
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

/* SYS_open (56) = openat(dirfd, pathname, flags, mode) in the asm-generic ABI.
 *
 *   a0 = dirfd   (AT_FDCWD or a directory fd)
 *   a1 = pathname
 *   a2 = flags
 *   a3 = mode
 *
 * Our read-only FS is flat (root-relative): we ignore dirfd and resolve
 * pathname the same way regardless of AT_FDCWD vs a real dir fd.  Reading
 * the path from a1 (not a0) is the fix for musl/busybox, which issues
 * openat(AT_FDCWD, path, …).  The legacy initcode blob was rebuilt to the
 * same convention (see src/user/initcode_la.c). */
static uint64_t sys_open(struct la_trap_frame *tf)
{
    uint64_t upath = tf->gpr[LA_GPR_A1];
    uint32_t flags = (uint32_t)tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();
    (void)flags;

    if (!p) return (uint64_t)(-LA_EBADF);

    /* Copy path from user */
    char path[256];
    if (la_copy_str_from_user(path, upath, sizeof(path) - 1) < 0)
        return (uint64_t)(-LA_EFAULT);

    /* Resolve path to inode */
    uint32_t ino;
    if (la_fs_lookup(path, &ino) < 0) {
        la_uart_puts("  open: lookup '");
        la_uart_puts(path);
        la_uart_puts("' FAILED\n");
        return (uint64_t)(-LA_ENOENT);
    }
    la_uart_puts("  open: '");
    la_uart_puts(path);
    la_uart_puts("' -> ino=");
    la_uart_put_hex(ino);
    la_uart_puts("\n");

    /* Allocate fd */
    int fd = -1;
    for (int i = 0; i < LA_NFD; i++) {
        if (p->fds[i].type == LA_FD_UNUSED) {
            fd = i;
            break;
        }
    }
    if (fd < 0) return (uint64_t)(-LA_EMFILE);

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

    la_uart_puts("  getdents: ino=");
    la_uart_put_hex(p->fds[fd].ino);
    la_uart_puts("\n");

    uint32_t n = la_fs_get_dentries(p->fds[fd].ino, dentbuf, len);
    if (n == 0) {
        la_uart_puts("  getdents: 0 entries\n");
        return 0;
    }

    la_uart_puts("  getdents: n=");
    la_uart_put_hex(n);
    la_uart_puts("\n");
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
    ctf->era += LA_SYSCALL_INSN_SIZE; /* skip syscall insn (parent does this later, child must do it now) */
    child->tf = ctf;

    /* Copy file descriptors */
    for (int i = 0; i < LA_NFD; i++)
        child->fds[i] = parent->fds[i];

    /* Inherit other state */
    child->parent_pid = parent->pid;
    child->heap_top   = parent->heap_top;
    child->mmap_top   = parent->mmap_top;
    child->stack_bottom = parent->stack_bottom;   /* so forked children keep
                                                   * the grown stack floor */
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
        return (uint64_t)(-LA_EFAULT);

    /* la_do_exec_syscall returns (uint64_t)-1 on any failure (file not
     * found, bad ELF, no memory).  Map that to -ENOENT so callers see a
     * sensible errno instead of -1, which musl reads as EPERM and busybox
     * prints as "Operation not permitted". */
    uint64_t rc = la_do_exec_syscall(tf, path, uargv);
    if (rc == (uint64_t)-1)
        return (uint64_t)(-LA_ENOENT);
    return rc;
}

/* SYS_wait (260) = wait4(pid, wstatus, options, rusage).
 * options bit 0 = WNOHANG (return 0 if no child has exited). */
static uint64_t sys_wait(struct la_trap_frame *tf)
{
    int wait_pid     = (int)tf->gpr[LA_GPR_A0];
    uint64_t ustatus = tf->gpr[LA_GPR_A1];
    int options      = (int)tf->gpr[LA_GPR_A2];
    struct la_proc *parent = la_current_proc();

    if (!parent) return (uint64_t)(-LA_ECHILD);

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
            /* Reclaim the child's resources (user page table + kernel stack).
             * Previously this only set state=UNUSED, leaking BOTH — every
             * fork/exec/exit cycle permanently lost a page table + kstack,
             * exhausting physical memory after a few tests.  Capture cpid
             * and exit status first; la_proc_free zeroes pid/pgtbl. */
            la_proc_free(child);
            return (uint64_t)cpid;
        }

        if (!has_children)
            return (uint64_t)(-LA_ECHILD);  /* ECHILD, not EPERM */

        /* WNOHANG: children exist but none exited yet — return 0. */
        if (options & 1)
            return 0;

        /* No zombie child yet — sleep and retry */
        la_proc_sleep();
    }
}

/* SYS_exit: terminate current process */
static uint64_t __attribute__((noreturn)) sys_exit(struct la_trap_frame *tf)
{
    uint32_t exit_code = (uint32_t)tf->gpr[LA_GPR_A0];
    struct la_proc *me = la_current_proc();

    /* A normal exit must come from a user process.  If there is no current
     * user process we cannot exit (la_proc_exit would dereference NULL) —
     * halt as a kernel panic rather than corrupt state. */
    if (!me || !me->is_user) {
        la_uart_puts("  exit: no current user proc — HALT\n");
        for (;;) {}
    }

    la_uart_puts("  exit: code=");
    la_uart_put_hex(exit_code);
    la_uart_puts(" pid=");
    la_uart_put_hex(me->pid);
    la_uart_puts("\n");

    /* la_proc_exit clears ISTLBR, marks us ZOMBIE, wakes the parent, and
     * switches to the scheduler.  It never returns. */
    la_proc_exit((int)exit_code);
}

/* SYS_brk: adjust program break.
 * When extending, allocate and map pages for the new heap area.
 * When shrinking, just update the pointer (don't free pages). */
static uint64_t sys_brk(struct la_trap_frame *tf)
{
    uint64_t addr = tf->gpr[LA_GPR_A0];
    struct la_proc *p = la_current_proc();

    if (!p) return (uint64_t)-1;

    if (addr == 0) {
        return p->heap_top;
    }

    if (addr < LA_USER_BASE)
        return (uint64_t)-1;

    /* Allocate pages for any new heap area.
     * CRITICAL: Linux brk returns ZEROED pages. musl's malloc depends
     * on this — it interprets non-zero bytes in fresh heap as malloc
     * chunk headers, causing corruption and crashes. */
    if (addr > p->heap_top && p->pgtbl) {
        uint64_t old_page = (p->heap_top + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
        uint64_t new_page = (addr + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
        for (uint64_t va = old_page; va < new_page; va += LA_PGSIZE) {
            uint64_t pa = la_uvm_alloc_page(p->pgtbl, va, 0x19FUL);
            if (pa == 0)
                return p->heap_top;  /* return current break on failure */
            /* la_uvm_alloc_page already zeroes (via la_pmem_alloc). */
        }
    }

    p->heap_top = addr;
    return addr;
}

/* SYS_mmap: map anonymous memory (simplified).
 * Handles MAP_ANONYMOUS mappings for malloc/TLS.
 * If addr hint is non-zero and page is already mapped, skip it. */
static uint64_t sys_mmap(struct la_trap_frame *tf)
{
    uint64_t addr  = tf->gpr[LA_GPR_A0];
    uint32_t len   = (uint32_t)tf->gpr[LA_GPR_A1];
    /* int prot     = (int)tf->gpr[LA_GPR_A2]; */
    /* int flags    = (int)tf->gpr[LA_GPR_A3]; */
    /* int fd       = (int)tf->gpr[LA_GPR_A4]; */
    /* uint64_t off = tf->gpr[LA_GPR_A5]; */
    struct la_proc *p = la_current_proc();

    if (!p || !p->pgtbl)
        return (uint64_t)-1;

    /* Linux returns -EINVAL for len==0 */
    if (len == 0)
        return (uint64_t)-1;

    /* Round up to page size */
    uint32_t npages = (len + LA_PGSIZE - 1) / LA_PGSIZE;

    /* Pick the mapping address.
     * Linux keeps the brk heap and the mmap region in DISJOINT parts of the
     * address space.  We previously allocated mmap from heap_top, which made
     * anonymous mmap pages collide with brk growth and corrupt musl's malloc
     * metadata.  Now mmap always comes from a separate high region.
     *
     * A non-zero addr is treated as a hint: honour it only if the page is
     * free, otherwise fall back to the mmap region. */
    int used_hint = 0;
    if (addr != 0) {
        if (la_uva_to_pa(p->pgtbl, addr) == 0)
            used_hint = 1;
    }
    if (!used_hint) {
        addr = (p->mmap_top + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
        p->mmap_top = addr + (uint64_t)npages * LA_PGSIZE;
    }

    /* Allocate and map pages (skip already-mapped pages).
     * CRITICAL: Linux mmap(MAP_ANONYMOUS) returns ZEROED pages.
     * musl's malloc depends on this — it interprets non-zero bytes
     * in fresh mmap'd memory as malloc chunk headers, causing
     * corruption and crashes. */
    for (uint32_t i = 0; i < npages; i++) {
        uint64_t va = addr + (uint64_t)i * LA_PGSIZE;
        /* Check if already mapped using VA-to-PA translation */
        if (la_uva_to_pa(p->pgtbl, va) != 0) {
            continue;  /* page already mapped, skip */
        }
        /* Allocate new page */
        uint64_t pa = la_uvm_alloc_page(p->pgtbl, va, 0x19FUL);
        if (pa == 0)
            return (uint64_t)-1;  /* 0x19F = V|D|PLV3|P|W = user RWX */
        /* Diagnostic: log VA→PA to detect physical-page aliasing. */
        la_uart_puts("  mmap va=");
        la_uart_put_hex(va);
        la_uart_puts("->pa=");
        la_uart_put_hex(pa);
        la_uart_puts("\n");
        /* Zero the page — Linux guarantees anonymous mmap pages are zeroed */
        uint8_t *px = (uint8_t *)pa;
        for (uint32_t z = 0; z < LA_PGSIZE; z++)
            px[z] = 0;
    }

    return addr;
}

/* SYS_munmap: unmap memory (stub — just return success) */
static uint64_t sys_munmap(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* ================================================================
 *  Syscall stubs needed by busybox / musl libc startup
 *  These return success (0) or sensible defaults so busybox
 *  doesn't crash on unimplemented syscalls.
 * ================================================================ */

/* SYS_set_tid_address: set clear-child-tid pointer.
 * Just return our pid (Linux returns tid). */
static uint64_t sys_set_tid_address(struct la_trap_frame *tf)
{
    (void)tf;
    struct la_proc *p = la_current_proc();
    return p ? (uint64_t)p->pid : 1;
}

/* SYS_set_robust_list: register robust futex list (stub) */
static uint64_t sys_set_robust_list(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_rt_sigaction: set signal handler (stub) */
static uint64_t sys_rt_sigaction(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_rt_sigprocmask: change signal mask (stub) */
static uint64_t sys_rt_sigprocmask(struct la_trap_frame *tf)
{
    /* If oset pointer is non-NULL, write empty signal mask */
    uint64_t oset = tf->gpr[LA_GPR_A1];
    if (oset) {
        uint64_t empty_mask = 0;
        la_copy_to_user(oset, &empty_mask, 8);
    }
    return 0;
}

/* SYS_msync: synchronize file mapping (stub) */
static uint64_t sys_msync(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_uname: return system information */
static uint64_t sys_uname(struct la_trap_frame *tf)
{
    uint64_t ubuf = tf->gpr[LA_GPR_A0];
    if (!ubuf) return (uint64_t)-1;

    /* struct utsname: 6 fields × 65 bytes each */
    char uname_buf[6 * 65];
    for (int i = 0; i < (int)sizeof(uname_buf); i++)
        uname_buf[i] = 0;

    /* sysname */
    const char *sysname = "SeaOS";
    for (int i = 0; sysname[i]; i++) uname_buf[i] = sysname[i];
    /* nodename (offset 65) */
    const char *node = "seaos";
    for (int i = 0; node[i]; i++) uname_buf[65 + i] = node[i];
    /* release (offset 130) */
    const char *release = "0.1.0";
    for (int i = 0; release[i]; i++) uname_buf[130 + i] = release[i];
    /* version (offset 195) */
    const char *version = "SeaOS 0.1.0 loongarch64";
    for (int i = 0; version[i]; i++) uname_buf[195 + i] = version[i];
    /* machine (offset 260) */
    const char *machine = "loongarch64";
    for (int i = 0; machine[i]; i++) uname_buf[260 + i] = machine[i];

    la_copy_to_user(ubuf, uname_buf, sizeof(uname_buf));
    return 0;
}

/* SYS_getuid: return real user ID (root = 0) */
static uint64_t sys_getuid(struct la_trap_frame *tf) { (void)tf; return 0; }

/* SYS_geteuid: return effective user ID (root = 0) */
static uint64_t sys_geteuid(struct la_trap_frame *tf) { (void)tf; return 0; }

/* SYS_getgid: return real group ID (root = 0) */
static uint64_t sys_getgid(struct la_trap_frame *tf) { (void)tf; return 0; }

/* SYS_getegid: return effective group ID (root = 0) */
static uint64_t sys_getegid(struct la_trap_frame *tf) { (void)tf; return 0; }

/* SYS_gettid: return thread ID (= pid for single-threaded) */
static uint64_t sys_gettid(struct la_trap_frame *tf)
{
    (void)tf;
    struct la_proc *p = la_current_proc();
    return p ? (uint64_t)p->pid : 1;
}

/* SYS_getppid: return parent process ID */
static uint64_t sys_getppid(struct la_trap_frame *tf)
{
    (void)tf;
    struct la_proc *p = la_current_proc();
    return p ? (uint64_t)p->parent_pid : 0;
}

/* SYS_getcwd: get current working directory */
static uint64_t sys_getcwd(struct la_trap_frame *tf)
{
    uint64_t ubuf = tf->gpr[LA_GPR_A0];
    uint32_t size = (uint32_t)tf->gpr[LA_GPR_A1];
    struct la_proc *p = la_current_proc();

    if (!p || !ubuf || size < 2) return (uint64_t)-1;

    /* Resolve cwd inode to path — simplified: just return "/" */
    const char *cwd = "/";
    uint32_t len = 0;
    while (cwd[len]) len++;
    len++; /* include NUL */
    if (len > size) return (uint64_t)-1;

    la_copy_to_user(ubuf, cwd, len);
    return ubuf;
}

/* SYS_mprotect: set memory protection (stub — all pages are RWX) */
static uint64_t sys_mprotect(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_statx: get file status (modern stat interface) */
static uint64_t sys_statx(struct la_trap_frame *tf)
{
    int dirfd     = (int)tf->gpr[LA_GPR_A0];
    uint64_t upath = tf->gpr[LA_GPR_A1];
    /* uint32_t flags = (uint32_t)tf->gpr[LA_GPR_A2]; */
    /* uint32_t mask  = (uint32_t)tf->gpr[LA_GPR_A3]; */
    uint64_t ustat = tf->gpr[LA_GPR_A4];
    struct la_proc *p = la_current_proc();

    if (!p || !ustat) return (uint64_t)-1;

    /* Copy path from user */
    char path[256];
    if (la_copy_str_from_user(path, upath, sizeof(path) - 1) < 0)
        return (uint64_t)-1;

    /* Handle AT_FDCWD */
    (void)dirfd;

    /* Resolve path to inode */
    uint32_t ino;
    if (la_fs_lookup(path, &ino) < 0) {
        la_uart_puts("  statx: '");
        la_uart_puts(path);
        la_uart_puts("' not found\n");
        return (uint64_t)-1;
    }

    /* Build a minimal statx struct matching Linux UAPI layout */
    int ftype = la_fs_inode_type(ino);

    {
        char sbuf[256];
        for (int i = 0; i < 256; i++) sbuf[i] = 0;

        struct __attribute__((packed)) statx_s {
            uint32_t mask;        /* 0 */
            uint32_t blksize;     /* 4 */
            uint64_t attributes;  /* 8 */
            uint32_t nlink;       /* 16 */
            uint32_t uid;         /* 20 */
            uint32_t gid;         /* 24 */
            uint16_t mode;        /* 28 */
            uint16_t _spare0;     /* 30 */
            uint32_t ino_lo;      /* 32 */
            uint32_t ino_hi;      /* 36 */
            uint64_t size;        /* 40 */
            uint64_t blocks;      /* 48 */
            uint64_t atime_s;     /* 56 */
            uint32_t atime_ns;    /* 64 */
            uint32_t _spare1;     /* 68 */
            uint64_t btime_s;     /* 72 */
            uint32_t btime_ns;    /* 80 */
            uint32_t _spare2;     /* 84 */
            uint64_t ctime_s;     /* 88 */
            uint32_t ctime_ns;    /* 96 */
            uint32_t _spare3;     /* 100 */
            uint64_t mtime_s;     /* 104 */
            uint32_t mtime_ns;    /* 112 */
            uint32_t _spare4;     /* 116 */
            uint64_t rdev;        /* 120 */
            uint64_t dev;         /* 128 */
        } *psx = (struct statx_s *)sbuf;

        psx->mask = 0x07FF; /* STATX_BASIC_STATS */
        psx->blksize = 4096;
        psx->nlink = 1;
        psx->uid = 0;
        psx->gid = 0;
        if (ftype == 1) psx->mode = 0040755;  /* directory */
        else psx->mode = 0100755;             /* regular file */
        psx->ino_lo = ino;
        psx->size = la_fs_inode_size(ino);
        psx->blocks = (psx->size + 511) / 512;
        psx->dev = 1;

        la_copy_to_user(ustat, sbuf, 160);
    }
    return 0;
}

/* SYS_exit_group: exit all threads (same as exit for single-threaded) */
static uint64_t __attribute__((noreturn)) sys_exit_group(struct la_trap_frame *tf)
{
    sys_exit(tf);
    __builtin_unreachable();
}

/* SYS_pipe2: create pipe (stub — return -ENOSYS) */
static uint64_t sys_pipe2(struct la_trap_frame *tf)
{
    (void)tf;
    return (uint64_t)(-LA_ENOSYS);
}

/* SYS_dup3: duplicate fd with flags */
static uint64_t sys_dup3(struct la_trap_frame *tf)
{
    int oldfd = (int)tf->gpr[LA_GPR_A0];
    int newfd = (int)tf->gpr[LA_GPR_A1];
    /* int flags = (int)tf->gpr[LA_GPR_A2]; */
    struct la_proc *p = la_current_proc();

    if (!p || oldfd < 0 || oldfd >= LA_NFD || newfd < 0 || newfd >= LA_NFD)
        return (uint64_t)-1;
    if (p->fds[oldfd].type == LA_FD_UNUSED)
        return (uint64_t)-1;

    /* Close newfd if it was open */
    if (p->fds[newfd].type != LA_FD_UNUSED)
        p->fds[newfd].type = LA_FD_UNUSED;

    p->fds[newfd] = p->fds[oldfd];
    return (uint64_t)newfd;
}

/* SYS_waitid: wait for child (similar to wait) */
static uint64_t sys_waitid(struct la_trap_frame *tf)
{
    /* idtype = a0, id = a1, infop = a2, options = a3 */
    /* For now, delegate to sys_wait */
    return sys_wait(tf);
}

/* SYS_clone: create a new process (simplified — act like fork) */
static uint64_t sys_clone(struct la_trap_frame *tf)
{
    /* For simple cases, clone with SIGCHLD signal acts like fork */
    /* flags = a0, stack = a1, parent_tid = a2, tls = a3, child_tid = a4 */
    uint64_t flags = tf->gpr[LA_GPR_A0];
    uint64_t new_stack = tf->gpr[LA_GPR_A1];

    /* If CLONE_VM is set, that's threading — we don't support it */
    if (flags & 0x00000100UL) {
        la_uart_puts("  clone: CLONE_VM not supported\n");
        return (uint64_t)-1;
    }

    /* Treat as fork */
    uint64_t ret = sys_fork(tf);
    if (ret == 0) {
        /* Child: set new stack pointer if provided */
        struct la_proc *child = la_current_proc();
        if (child && child->tf && new_stack)
            child->tf->gpr[LA_GPR_SP] = new_stack;
    }
    return ret;
}

/* SYS_ioctl: I/O control (stub — return -ENOTTY) */
static uint64_t sys_ioctl(struct la_trap_frame *tf)
{
    (void)tf;
    /* ENOTTY = 25, inappropriate ioctl for device */
    return (uint64_t)(-25);
}

/* SYS_newfstatat: stat a file relative to dirfd */
static uint64_t sys_newfstatat(struct la_trap_frame *tf)
{
    int dirfd     = (int)tf->gpr[LA_GPR_A0];
    uint64_t upath = tf->gpr[LA_GPR_A1];
    uint64_t ustat = tf->gpr[LA_GPR_A2];
    /* int flags    = (int)tf->gpr[LA_GPR_A3]; */
    struct la_proc *p = la_current_proc();

    if (!p || !ustat) return (uint64_t)-1;
    (void)dirfd;

    char path[256];
    if (la_copy_str_from_user(path, upath, sizeof(path) - 1) < 0)
        return (uint64_t)-1;

    uint32_t ino;
    if (la_fs_lookup(path, &ino) < 0)
        return (uint64_t)-1;

    /* Build a minimal stat struct (kernel stat, 128 bytes) */
    char sbuf[128];
    for (int i = 0; i < 128; i++) sbuf[i] = 0;

    /* Linux stat layout (simplified):
     *   [0]  dev (8), [8] ino (8), [16] mode (4), [20] nlink (4),
     *   [24] uid (4), [28] gid (4), [32] rdev (8), [40] size (8),
     *   [48] blksize (8), [56] blocks (8) */
    int ftype = la_fs_inode_type(ino);
    uint16_t mode = ftype == 1 ? 0040755 : 0100755;
    *(uint64_t *)&sbuf[0]  = 1;     /* dev */
    *(uint64_t *)&sbuf[8]  = ino;   /* ino */
    *(uint32_t *)&sbuf[16] = mode;  /* mode */
    *(uint32_t *)&sbuf[20] = 1;     /* nlink */
    *(uint32_t *)&sbuf[48] = 4096;  /* blksize */
    *(uint64_t *)&sbuf[40] = la_fs_inode_size(ino); /* size */

    la_copy_to_user(ustat, sbuf, 128);
    return 0;
}

/* SYS_faccessat: check file accessibility (stub — return 0) */
static uint64_t sys_faccessat(struct la_trap_frame *tf)
{
    /* Just return 0 (accessible) */
    (void)tf;
    return 0;
}

/* SYS_readlinkat: read symbolic link (stub — return -EINVAL) */
static uint64_t sys_readlinkat(struct la_trap_frame *tf)
{
    (void)tf;
    return (uint64_t)(-22); /* -EINVAL */
}

/* SYS_fcntl: file control (stub) */
static uint64_t sys_fcntl(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_sched_yield: yield CPU (stub) */
static uint64_t sys_sched_yield(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_prlimit64: get/set resource limits (stub) */
static uint64_t sys_prlimit64(struct la_trap_frame *tf)
{
    uint64_t uold = tf->gpr[LA_GPR_A2];
    /* Return empty old limit if pointer provided */
    if (uold) {
        uint64_t zero_pair[2] = {0, 0};
        la_copy_to_user(uold, zero_pair, 16);
    }
    return 0;
}

/* SYS_setrlimit / SYS_getrlimit: resource limits (stub) */
static uint64_t sys_setrlimit(struct la_trap_frame *tf) { (void)tf; return 0; }
static uint64_t sys_getrlimit(struct la_trap_frame *tf)
{
    uint64_t urlim = tf->gpr[LA_GPR_A1];
    if (urlim) {
        uint64_t lim[2] = {(uint64_t)-1, (uint64_t)-1}; /* RLIM_INFINITY */
        la_copy_to_user(urlim, lim, 16);
    }
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

/* ---- Syscall trace counter (first N only) ---- */
static int la_syscall_trace_count = 0;
#define LA_SYSCALL_TRACE_MAX 200

/* ---- Main syscall dispatcher ---- */
uint64_t la_syscall_dispatch(struct la_trap_frame *tf)
{
    uint64_t sysno = tf->gpr[LA_GPR_A7];

    /* Publish the current process's cwd so path lookups (open/exec/chdir/
     * statx/...) resolve relative paths like "./cyclictest" correctly. */
    {
        struct la_proc *cp = la_current_proc();
        la_fs_cwd_ino = cp ? cp->cwd_ino : 0;
    }

    /* Trace first N syscalls for diagnostics */
    if (la_syscall_trace_count < LA_SYSCALL_TRACE_MAX) {
        la_uart_puts("  sys#");
        la_uart_put_hex(sysno);
        la_uart_puts(" a0=");
        la_uart_put_hex(tf->gpr[LA_GPR_A0]);
        la_uart_puts(" a1=");
        la_uart_put_hex(tf->gpr[LA_GPR_A1]);
        la_uart_puts("\n");
        la_syscall_trace_count++;
    }

    switch (sysno) {
    /* Process management */
    case SYS_fork:       return sys_fork(tf);
    case SYS_wait:       return sys_wait(tf);
    case SYS_waitid:     return sys_waitid(tf);
    case SYS_exit:       sys_exit(tf); __builtin_unreachable();
    case SYS_exit_group: sys_exit_group(tf); __builtin_unreachable();
    case SYS_getpid:     return sys_getpid(tf);
    case SYS_gettid:     return sys_gettid(tf);
    case SYS_getppid:    return sys_getppid(tf);
    case SYS_getcwd:     return sys_getcwd(tf);
    case SYS_exec:       return sys_exec(tf);
    case SYS_clone:      return sys_clone(tf);

    /* File I/O */
    case SYS_open:       return sys_open(tf);
    case SYS_close:      return sys_close(tf);
    case SYS_read:       return sys_read(tf);
    case SYS_write:      return sys_write(tf);
    case SYS_lseek:      return sys_lseek(tf);
    case SYS_dup:        return sys_dup(tf);
    case SYS_dup3:       return sys_dup3(tf);
    case SYS_fstat:      return sys_fstat(tf);
    case SYS_get_dentries: return sys_get_dentries(tf);
    case SYS_ioctl:      return sys_ioctl(tf);
    case SYS_newfstatat: return sys_newfstatat(tf);
    case SYS_faccessat:  return sys_faccessat(tf);
    case SYS_readlinkat: return sys_readlinkat(tf);
    case SYS_fcntl:      return sys_fcntl(tf);

    /* Directory */
    case SYS_chdir:      return sys_chdir(tf);
    case SYS_mkdir:      return sys_mkdir(tf);

    /* Memory */
    case SYS_brk:        return sys_brk(tf);
    case SYS_mmap:       return sys_mmap(tf);
    case SYS_munmap:     return sys_munmap(tf);
    case SYS_mprotect:   return sys_mprotect(tf);

    /* Signal (stubs) */
    case SYS_rt_sigaction:   return sys_rt_sigaction(tf);
    case SYS_rt_sigprocmask: return sys_rt_sigprocmask(tf);

    /* Identity */
    case SYS_getuid:     return sys_getuid(tf);
    case SYS_geteuid:    return sys_geteuid(tf);
    case SYS_getgid:     return sys_getgid(tf);
    case SYS_getegid:    return sys_getegid(tf);

    /* Threading / futex */
    case SYS_set_tid_address: return sys_set_tid_address(tf);
    case SYS_set_robust_list: return sys_set_robust_list(tf);

    /* File info */
    case SYS_statx:      return sys_statx(tf);
    case SYS_uname:      return sys_uname(tf);

    /* Other stubs */
    case SYS_msync:      return sys_msync(tf);
    case SYS_pipe2:      return sys_pipe2(tf);
    case SYS_sched_yield: return sys_sched_yield(tf);
    case SYS_setrlimit:  return sys_setrlimit(tf);
    case SYS_getrlimit:  return sys_getrlimit(tf);
    case SYS_prlimit64:  return sys_prlimit64(tf);

    /* System */
    case SYS_shutdown:   return sys_shutdown();

    default:
        break;
    }

    /* Unimplemented syscall — log for diagnostics */
    la_uart_puts("  syscall: UNKNOWN #");
    la_uart_put_hex(sysno);
    la_uart_puts(" a0=");
    la_uart_put_hex(tf->gpr[LA_GPR_A0]);
    la_uart_puts("\n");
    return (uint64_t)(-LA_ENOSYS);
}
