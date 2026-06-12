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
#define SYS_writev          66   /* scatter/gather write */
#define SYS_getcpu          168  /* get CPU number */
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

/* Signal syscalls (LoongArch generic ABI) */
#define SYS_kill            129
#define SYS_tgkill          131
#define SYS_rt_sigreturn    139  /* note: NOT setrlimit — LoongArch has no setrlimit */

/* Scheduling / time / info */
#define SYS_sched_setaffinity 122
#define SYS_sched_getaffinity 123
#define SYS_sched_setscheduler 119
#define SYS_times           153
#define SYS_gettimeofday    169

/* Select / poll */
#define SYS_pselect6         72
#define SYS_ppoll            73

/* Socket family (198–207, return -ENOSYS stubs for now) */
#define SYS_socket          198
#define SYS_bind            200
#define SYS_listen          201
#define SYS_accept          202
#define SYS_connect         203
#define SYS_sendto          206
#define SYS_recvfrom        207
#define SYS_getsockname     204
#define SYS_getpeername     205

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
#define LA_ESRCH    3
#define LA_EMFILE  24
#define LA_ENOSPC  28
#define LA_ESPIPE  29
#define LA_ENAMETOOLONG 36

/* ---- Root inode numbers (set by fs_la.c after mount) ---- */
#define LA_ROOT_INO_SEA  0
#define LA_ROOT_INO_E4   2

/* ---- Pipe pool ---- */
static struct la_pipe la_pipes[LA_NPIPE];

/* Allocate a free pipe slot from the static pool.  Returns NULL when
 * exhausted — caller should return -ENOMEM / -EMFILE. */
static struct la_pipe *la_pipe_alloc(void)
{
    for (int i = 0; i < LA_NPIPE; i++) {
        if (!la_pipes[i].used) {
            struct la_pipe *p = &la_pipes[i];
            p->used      = 1;
            p->nread     = 0;
            p->nwrite    = 0;
            p->readopen  = 0;   /* caller sets per-end ref counts */
            p->writeopen = 0;
            return p;
        }
    }
    return 0;
}

/* Release a pipe slot back to the pool.  Only call when both ends
 * are fully closed (readopen == 0 && writeopen == 0). */
static void la_pipe_close_end(struct la_proc *proc, int fd)
{
    struct la_pipe *pi = proc->fds[fd].pipe;
    if (!pi) return;

    if (proc->fds[fd].writable) {
        pi->writeopen--;
        if (pi->writeopen == 0)
            la_proc_wakeup_chan(&pi->nread);   /* EOF for blocked readers */
    } else {
        pi->readopen--;
        if (pi->readopen == 0)
            la_proc_wakeup_chan(&pi->nwrite);  /* broken pipe for writers */
    }

    /* Last end closed — free the pipe slot. */
    if (pi->readopen == 0 && pi->writeopen == 0)
        pi->used = 0;
}

/* After fork/clone copies fd table entries, bump pipe ref counts for
 * every inherited pipe fd so the pipe object knows each end is shared. */
static void la_pipe_dup_all(struct la_proc *child)
{
    for (int i = 0; i < LA_NFD; i++) {
        if (child->fds[i].type != LA_FD_PIPE || !child->fds[i].pipe)
            continue;
        if (child->fds[i].writable)
            child->fds[i].pipe->writeopen++;
        else
            child->fds[i].pipe->readopen++;
    }
}

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

    /* Pipe write */
    if (fd >= 0 && fd < LA_NFD && p->fds[fd].type == LA_FD_PIPE) {
        struct la_pipe *pi = p->fds[fd].pipe;
        if (!pi || !p->fds[fd].writable)
            return (uint64_t)-1;

        uint32_t done = 0;
        while (done < len) {
            /* If no reader left, return -1 (broken pipe).
             * Wake any blocked reader first so it can drain. */
            if (pi->readopen == 0) {
                la_proc_wakeup_chan(&pi->nread);
                return done > 0 ? (uint64_t)done : (uint64_t)-1;
            }

            /* Wait while buffer full, but only if a reader exists. */
            while (pi->nwrite == pi->nread + LA_PIPE_SIZE
                   && pi->readopen > 0)
                la_proc_sleep_chan(&pi->nwrite);
            if (pi->readopen == 0)
                continue;   /* re-check after wake (reader gone) */

            /* How much space is left in the pipe? */
            uint32_t space = LA_PIPE_SIZE
                           - (pi->nwrite - pi->nread);
            uint32_t chunk = len - done;
            if (chunk > space) chunk = space;

            /* Copy from user buffer into the circular pipe buffer,
             * wrapping at the end if needed. */
            uint32_t wi = pi->nwrite % LA_PIPE_SIZE;
            if (wi + chunk > LA_PIPE_SIZE) {
                /* Write wraps around buffer end */
                uint32_t first = LA_PIPE_SIZE - wi;
                la_copy_from_user(&pi->data[wi],
                                  buf + done, first);
                la_copy_from_user(&pi->data[0],
                                  buf + done + first,
                                  chunk - first);
            } else {
                la_copy_from_user(&pi->data[wi],
                                  buf + done, chunk);
            }
            pi->nwrite += chunk;
            done += chunk;

            /* Wake any blocked reader */
            la_proc_wakeup_chan(&pi->nread);
        }
        return done;
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

    /* Pipe read */
    if (p->fds[fd].type == LA_FD_PIPE) {
        struct la_pipe *pi = p->fds[fd].pipe;
        if (!pi || p->fds[fd].writable)
            return (uint64_t)-1;   /* read from write-end is invalid */

        /* Block until data is available or the write end closes. */
        while (pi->nread == pi->nwrite && pi->writeopen > 0)
            la_proc_sleep_chan(&pi->nread);

        uint32_t avail = pi->nwrite - pi->nread;
        if (avail == 0)
            return 0;   /* EOF — no writers left, buffer empty */

        uint32_t chunk = avail;
        if (chunk > len) chunk = len;

        /* Copy directly from pipe buffer to user, wrapping if needed. */
        uint32_t ri = pi->nread % LA_PIPE_SIZE;
        uint32_t done = 0;
        while (done < chunk) {
            uint32_t seg = chunk - done;
            if (ri + seg > LA_PIPE_SIZE)
                seg = LA_PIPE_SIZE - ri;   /* wrap at end */
            la_copy_to_user(buf + done, &pi->data[ri], seg);
            done += seg;
            ri = (ri + seg) % LA_PIPE_SIZE;
        }
        pi->nread += chunk;

        /* Wake any blocked writer (now has more buffer space). */
        la_proc_wakeup_chan(&pi->nwrite);

        return chunk;
    }

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

    /* Pipe cleanup: decrement the appropriate end's refcount,
     * wake the other end if this was the last open descriptor,
     * and free the pipe struct when both ends are fully closed. */
    if (p->fds[fd].type == LA_FD_PIPE && p->fds[fd].pipe)
        la_pipe_close_end(p, fd);

    p->fds[fd].ino = 0;
    p->fds[fd].offset = 0;
    p->fds[fd].type = LA_FD_UNUSED;
    p->fds[fd].writable = 0;
    p->fds[fd].pipe = 0;
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

    /* Bump pipe-end reference counts so the pipe object knows both
     * parent and child hold open ends (for correct EOF / refcounting). */
    la_pipe_dup_all(child);

    /* Inherit other state */
    child->parent_pid = parent->pid;
    child->__mm.heap_top   = parent->mm->heap_top;
    child->__mm.mmap_top   = parent->mm->mmap_top;
    child->stack_bottom = parent->stack_bottom;   /* so forked children keep
                                                   * the grown stack floor */
    child->cwd_ino    = parent->cwd_ino;
    child->shared_vm  = 0;
    child->clear_child_tid = 0;
    child->mm         = &child->__mm;

    /* Inherit signal state */
    child->sig_pending = parent->sig_pending;
    child->sig_mask    = parent->sig_mask;
    for (int s = 0; s < LA_NSIG; s++)
        child->sig_actions[s] = parent->sig_actions[s];

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
            /* CLONE_VM threads are never collected via wait4 — they are
             * reaped by the scheduler's parentless-zombie path when the
             * group leader dies.  Skipping them here keeps initcode's
             * wait4(leader_pid) clean: it only sees the leader, not the
             * worker threads whose parent_pid is the leader itself. */
            if (p->shared_vm)
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

/* SYS_exit: terminate current process (or thread if CLONE_VM).
 * For threads with a clear_child_tid, zero the word and wake the futex
 * channel so that pthread_join (which futex-waits on that address) returns. */
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

    /* Thread-exit: clear *clear_child_tid and futex-wake anyone waiting
     * on it (the pthread_join side).  Must happen BEFORE la_proc_exit
     * because that switches away; the joiner would never be woken. */
    if (me->clear_child_tid) {
        uint32_t zero = 0;
        la_copy_to_user((uint64_t)me->clear_child_tid, &zero, 4);
        la_proc_wakeup_chan((void *)me->clear_child_tid);
        me->clear_child_tid = 0;
    }

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
        return p->mm->heap_top;
    }

    if (addr < LA_USER_BASE)
        return (uint64_t)-1;

    /* Allocate pages for any new heap area.
     * CRITICAL: Linux brk returns ZEROED pages. musl's malloc depends
     * on this — it interprets non-zero bytes in fresh heap as malloc
     * chunk headers, causing corruption and crashes. */
    if (addr > p->mm->heap_top && p->pgtbl) {
        uint64_t old_page = (p->mm->heap_top + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
        uint64_t new_page = (addr + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
        for (uint64_t va = old_page; va < new_page; va += LA_PGSIZE) {
            uint64_t pa = la_uvm_alloc_page(p->pgtbl, va, 0x19FUL);
            if (pa == 0)
                return p->mm->heap_top;  /* return current break on failure */
            /* la_uvm_alloc_page already zeroes (via la_pmem_alloc). */
        }
    }

    p->mm->heap_top = addr;
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
        addr = (p->mm->mmap_top + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
        p->mm->mmap_top = addr + (uint64_t)npages * LA_PGSIZE;
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
 * Stores the user VA so the kernel can zero *tidptr and futex-wake it
 * when the calling thread exits — this is how pthread_join finds out
 * the thread terminated.  Returns the caller's tid (= pid in this model). */
static uint64_t sys_set_tid_address(struct la_trap_frame *tf)
{
    struct la_proc *p = la_current_proc();
    if (!p) return 1;
    p->clear_child_tid = tf->gpr[LA_GPR_A0];
    return (uint64_t)p->pid;
}

/* SYS_set_robust_list: register robust futex list (stub) */
static uint64_t sys_set_robust_list(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_futex (98): fast userspace mutual exclusion.
 *
 * Minimal private-futex implementation covering the two operations
 * musl/pthread needs: WAIT and WAKE.  The channel is the user VA of the
 * futex word (same VA → same channel).  Under the cooperative single-CPU
 * scheduler (no preemption, no timer-yield), the check-then-sleep in WAIT
 * is observationally atomic — no other process runs between the val-compare
 * and the swtch inside la_proc_sleep_chan, so there is no lost-wakeup race.
 *
 * Returns: WAKE → 1 (at least one waiter woken).  WAIT → 0 on wake,
 *          -EAGAIN if *uaddr != val, -EFAULT on bad uaddr. */
static uint64_t sys_futex(struct la_trap_frame *tf)
{
    uint64_t uaddr  = tf->gpr[LA_GPR_A0];
    int      op     = (int)tf->gpr[LA_GPR_A1] & 0x7f;   /* strip private/realtime flags */
    uint32_t val    = (uint32_t)tf->gpr[LA_GPR_A2];
    /* timeout, uaddr2, val3 are ignored in minimal implementation */

    if (uaddr == 0)
        return (uint64_t)(-LA_EFAULT);

    /* FUTEX_WAKE (op == 1): wake one or more waiters on this channel */
    if (op == 1) {
        la_proc_wakeup_chan((void *)uaddr);
        return 1;
    }

    /* FUTEX_WAIT (op == 0): sleep if *uaddr still equals val */
    if (op == 0) {
        uint32_t cur = 0;
        if (la_copy_from_user(&cur, uaddr, sizeof(cur)) != sizeof(cur))
            return (uint64_t)(-LA_EFAULT);
        if (cur != val)
            return (uint64_t)(-LA_EAGAIN);

        /* check-then-sleep is atomic under cooperative single-CPU scheduling:
         * la_proc_sleep_chan sets wait_chan THEN state=SLEEPING with no
         * intervening swtch, and only wakeup_chan flips futex sleepers back
         * to RUNNABLE.  The caller (musl) re-validates *uaddr after wake. */
        la_proc_sleep_chan((void *)uaddr);
        return 0;
    }

    /* Unknown futex op — silently succeed (most callers treat ENOSYS as fatal) */
    return 0;
}

/* SYS_rt_sigaction(134): register a signal handler.
 *   a0 = signum, a1 = *act (or NULL to query), a2 = *oldact (or NULL),
 *   a3 = sigsetsize (must be 8)
 * The sigaction struct is { handler(8), flags(8), restorer(8), mask(8) }.
 * SIGKILL(9) and SIGSTOP(19) cannot be caught or ignored. */
static uint64_t sys_rt_sigaction(struct la_trap_frame *tf)
{
    int signum       = (int)tf->gpr[LA_GPR_A0];
    uint64_t uact    = tf->gpr[LA_GPR_A1];
    uint64_t uoldact = tf->gpr[LA_GPR_A2];
    uint64_t sigsetsize = tf->gpr[LA_GPR_A3];
    struct la_proc *p = la_current_proc();

    if (!p || signum < 1 || signum >= LA_NSIG || sigsetsize != 8)
        return (uint64_t)(-LA_EINVAL);
    if (signum == LA_SIGKILL || signum == LA_SIGSTOP)
        return (uint64_t)(-LA_EINVAL);

    /* Read old action for query */
    if (uoldact) {
        struct la_sigaction old = p->sig_actions[signum];
        la_copy_to_user(uoldact, &old, sizeof(old));
    }

    /* Set new handler */
    if (uact) {
        struct la_sigaction new;
        if (la_copy_from_user(&new, uact, sizeof(new)) != sizeof(new))
            return (uint64_t)(-LA_EFAULT);
        p->sig_actions[signum] = new;
    }
    return 0;
}

/* SYS_rt_sigprocmask(135): examine or change the blocked signal mask.
 *   a0 = how (0=SIG_BLOCK, 1=SIG_UNBLOCK, 2=SIG_SETMASK)
 *   a1 = *set (or NULL), a2 = *oldset (or NULL), a3 = sigsetsize (must be 8) */
static uint64_t sys_rt_sigprocmask(struct la_trap_frame *tf)
{
    int how          = (int)tf->gpr[LA_GPR_A0];
    uint64_t uset    = tf->gpr[LA_GPR_A1];
    uint64_t uoldset = tf->gpr[LA_GPR_A2];
    uint64_t sigsetsize = tf->gpr[LA_GPR_A3];
    struct la_proc *p = la_current_proc();

    if (!p || sigsetsize != 8)
        return (uint64_t)(-LA_EINVAL);

    /* Read old mask */
    if (uoldset)
        la_copy_to_user(uoldset, &p->sig_mask, sizeof(p->sig_mask));

    if (!uset) return 0;  /* just querying */

    uint64_t set = 0;
    if (la_copy_from_user(&set, uset, sizeof(set)) != sizeof(set))
        return (uint64_t)(-LA_EFAULT);

    if (how == 0)        p->sig_mask |= set;        /* SIG_BLOCK */
    else if (how == 1)   p->sig_mask &= ~set;       /* SIG_UNBLOCK */
    else if (how == 2)   p->sig_mask = set;         /* SIG_SETMASK */
    else                 return (uint64_t)(-LA_EINVAL);

    p->sig_mask &= ~(1UL << LA_SIGKILL);   /* SIGKILL unblockable */
    p->sig_mask &= ~(1UL << LA_SIGSTOP);   /* SIGSTOP unblockable */
    return 0;
}

/* SYS_kill(129): send a signal to a process.
 * a0 = pid, a1 = sig.  Only pid > 0 and sig 1–31 are supported. */
static uint64_t sys_kill(struct la_trap_frame *tf)
{
    int pid  = (int)tf->gpr[LA_GPR_A0];
    int sig  = (int)tf->gpr[LA_GPR_A1];

    if (sig < 1 || sig >= LA_NSIG) return (uint64_t)(-LA_EINVAL);

    struct la_proc *target = la_proc_by_pid(pid);
    if (!target) return (uint64_t)(-LA_ESRCH);

    /* Set the pending bit.  If the target is sleeping on a wait_chan,
     * wake it so it can check signals on its way back to user mode. */
    target->sig_pending |= (1UL << sig);
    if (target->state == LA_PROC_SLEEPING) {
        target->state = LA_PROC_RUNNABLE;
    }
    return 0;
}

/* SYS_tgkill(131): send a signal to a specific thread.
 * a0 = tgid, a1 = tid, a2 = sig.  Simplified — same as kill(tid, sig). */
static uint64_t sys_tgkill(struct la_trap_frame *tf)
{
    /* int tgid = (int)tf->gpr[LA_GPR_A0]; */  /* ignored */
    int tid  = (int)tf->gpr[LA_GPR_A1];
    int sig  = (int)tf->gpr[LA_GPR_A2];

    if (sig < 1 || sig >= LA_NSIG) return (uint64_t)(-LA_EINVAL);

    struct la_proc *target = la_proc_by_pid(tid);
    if (!target) return (uint64_t)(-LA_ESRCH);

    target->sig_pending |= (1UL << sig);
    if (target->state == LA_PROC_SLEEPING)
        target->state = LA_PROC_RUNNABLE;
    return 0;
}

/* SYS_rt_sigreturn(139): restore context after a signal handler returns.
 * The signal frame was pushed onto the user stack by la_signal_deliver.
 * Reads it back, restores the original trap frame, and the dispatcher's
 * ertn resumes the original execution where it was interrupted.
 *
 * NOTE: we set tf->era = sf.era - 4 because trap.c's syscall path
 * unconditionally does tf->era += 4 after this function returns,
 * so the net effect is era = sf.era (the original interrupted PC). */
static uint64_t sys_rt_sigreturn(struct la_trap_frame *tf)
{
    struct la_proc *p = la_current_proc();
    if (!p || !p->pgtbl) return (uint64_t)-1;

    uint64_t frame_va = tf->gpr[LA_GPR_SP];

    struct la_sigframe sf;
    if (la_copy_from_user(&sf, frame_va, sizeof(sf)) != sizeof(sf))
        return (uint64_t)-1;

    /* Restore all 32 GPRs and ERA from the saved sigframe.
     * Subtract 4 from era because the dispatcher adds 4 unconditionally. */
    for (int i = 0; i < 32; i++)
        tf->gpr[i] = sf.gpr[i];
    tf->era = sf.era - LA_SYSCALL_INSN_SIZE;

    /* Return the original a0 so the dispatcher writes it back */
    return sf.gpr[LA_GPR_A0];
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

/* SYS_exit_group: exit the entire thread group.
 *
 * Before calling sys_exit on the leader, force-zombify every sibling that
 * shares the same page table (i.e. is in the same thread group) so that no
 * thread survives the leader.  Zombified threads stop running, their ctx
 * is stale but safe — their kstacks are freed later by la_proc_free (either
 * when the parent reaps the leader via wait4, or by the scheduler's
 * parentless-zombie reaping).  Group membership is determined by shared
 * pgtbl root (NOT parent_pid), which is the robust predicate.
 *
 * For each force-killed sibling, we also fire its cleartid to unblock any
 * pthread_join call that might be waiting on it (unusual in a bench but
 * correct). */
static uint64_t __attribute__((noreturn)) sys_exit_group(struct la_trap_frame *tf)
{
    struct la_proc *me = la_current_proc();
    uint32_t exit_code = (uint32_t)tf->gpr[LA_GPR_A0];

    if (!me || !me->is_user) {
        la_uart_puts("  exit_group: no current user proc — HALT\n");
        for (;;) {}
    }

    /* Group-kill: zombify all siblings that share our page table.
     * The predicate "pgtbl == me->pgtbl" catches every CLONE_VM thread
     * regardless of parent_pid, state (RUNNABLE/RUNNING/SLEEPING), or
     * whether they were created by the leader or by a sibling.  We do NOT
     * touch the current proc (me) — sys_exit handles it below. */
    if (me->pgtbl) {
        struct la_proc *procs = la_proc_table();
        for (int i = 0; i < LA_NPROC; i++) {
            struct la_proc *q = &procs[i];
            if (q == me) continue;
            if (q->state == LA_PROC_UNUSED) continue;
            if (q->state == LA_PROC_ZOMBIE) continue;
            if (q->pgtbl != me->pgtbl) continue;  /* different address space */

            /* This is a sibling thread — force-kill it. */
            if (q->clear_child_tid) {
                uint32_t zero = 0;
                la_copy_to_user((uint64_t)q->clear_child_tid, &zero, 4);
                la_proc_wakeup_chan((void *)q->clear_child_tid);
                q->clear_child_tid = 0;
            }
            q->state     = LA_PROC_ZOMBIE;
            q->exit_code = (int)(unsigned)exit_code;
            la_uart_puts("  exit_group: killed sibling pid=");
            la_uart_put_hex(q->pid);
            la_uart_puts("\n");
        }
    }

    sys_exit(tf);
    __builtin_unreachable();
}

/* SYS_pipe2: create a pipe (circular buffer, blocking read/write).
 *
 *   a0 = user-space int fds[2]
 *   a1 = flags (O_CLOEXEC=0x80000, O_NONBLOCK=0x800; we ignore both)
 *
 * Allocates one pipe object and two file descriptors (fd0=read,
 * fd1=write).  A reader blocks on empty buffer until a writer produces
 * data or closes the write end.  A writer blocks on full buffer until a
 * reader drains data or closes the read end — in which case write
 * returns -1 (broken pipe).  Fork and clone inherit pipe fds; the
 * kernel tracks per-end refcounts so both parent and child can use the
 * pipe independently. */
static uint64_t sys_pipe2(struct la_trap_frame *tf)
{
    uint64_t ufdarray = tf->gpr[LA_GPR_A0];
    /* int flags = (int)tf->gpr[LA_GPR_A1]; */
    struct la_proc *p = la_current_proc();

    if (!p || !ufdarray) return (uint64_t)-1;

    struct la_pipe *pi = la_pipe_alloc();
    if (!pi) return (uint64_t)-1;

    /* Find two free file descriptors */
    int fd0 = -1, fd1 = -1;
    for (int i = 0; i < LA_NFD; i++) {
        if (p->fds[i].type == LA_FD_UNUSED) {
            if (fd0 < 0)      fd0 = i;
            else if (fd1 < 0) { fd1 = i; break; }
        }
    }
    if (fd1 < 0) {
        pi->used = 0;
        return (uint64_t)-1;
    }

    /* Read end (fd0) */
    pi->readopen  = 1;
    pi->writeopen = 1;

    p->fds[fd0].type     = LA_FD_PIPE;
    p->fds[fd0].pipe     = pi;
    p->fds[fd0].writable = 0;
    p->fds[fd0].ino      = 0;
    p->fds[fd0].offset   = 0;

    /* Write end (fd1) */
    p->fds[fd1].type     = LA_FD_PIPE;
    p->fds[fd1].pipe     = pi;
    p->fds[fd1].writable = 1;
    p->fds[fd1].ino      = 0;
    p->fds[fd1].offset   = 0;

    /* Copy fds to user-space int[2] */
    int fdarray[2] = { fd0, fd1 };
    la_copy_to_user(ufdarray, fdarray, sizeof(fdarray));

    return 0;
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

/* SYS_clone: create a child process or CLONE_VM thread.
 *
 * LoongArch ABI: clone(flags=a0, stack=a1, ptid=a2, tls=a3, ctid=a4)
 * Flag bits:
 *   0x00000100  CLONE_VM      — share address space (pgtbl + mm)
 *   0x00080000  CLONE_SETTLS  — set tp register to tls
 *   0x00100000  CLONE_PARENT_SETTID — write child tid → *ptid
 *   0x00200000  CLONE_CHILD_CLEARTID — store ctid, zero+WAKE on child exit
 *   0x01000000  CLONE_CHILD_SETTID  — write child tid → *ctid
 *
 * CLONE_VM threads share the parent's pgtbl pointer (no deep copy) and mm
 * (heap/mmap cursors).  This is a true shared-address-space thread — the
 * child gets its own kernel stack, trap frame, pid, fds, cwd, and a new
 * user stack / TLS.  Threads are invisible to the original parent's wait4
 * (thread->parent_pid = caller's pid, and wait4 skips shared_vm procs).
 * They are reaped by the scheduler's parentless-zombie path when the group
 * leader dies. */
static uint64_t sys_clone(struct la_trap_frame *tf)
{
    /* LoongArch asm-generic clone ABI is NOT the same as RISC-V.
     * RV:  clone(flags, stack, ptid, tls, ctid)   a3=tls, a4=ctid
     * LA:  clone(flags, stack, ptid, ctid, tls)   a3=ctid, a4=tls
     * musl loongarch64 clone.s confirms this: it moves a6(ctid)→a3, a5(tls)→a4. */
    uint64_t flags    = tf->gpr[LA_GPR_A0];
    uint64_t new_stack = tf->gpr[LA_GPR_A1];
    uint64_t ptid     = tf->gpr[LA_GPR_A2];
    uint64_t ctid     = tf->gpr[LA_GPR_A3];   /* a3 = child_tid */
    uint64_t tls      = tf->gpr[LA_GPR_A4];   /* a4 = tls */

    /* ---- Non-CLONE_VM: act like fork ---- */
    if ((flags & 0x00000100UL) == 0) {
        if (new_stack == 0)
            return sys_fork(tf);
        /* Clone without VM sharing but with a new stack — fork semantics. */
        uint64_t ret = sys_fork(tf);
        if (ret == 0) {
            /* Child: set new stack pointer */
            struct la_proc *child = la_current_proc();
            if (child && child->tf)
                child->tf->gpr[LA_GPR_SP] = new_stack;
        }
        return ret;
    }

    /* ---- CLONE_VM: create a real thread (shared address space) ---- */
    struct la_proc *parent = la_current_proc();
    if (!parent) return (uint64_t)-1;

    struct la_proc *child = la_proc_create_user("thread");
    if (!child) return (uint64_t)(-LA_EAGAIN);

    /* Share the parent's address space — same pgtbl root and same mm
     * (heap/mmap cursors), so brk/mmap in any thread advances one cursor. */
    child->pgtbl = parent->pgtbl;       /* SHARED, not a deep copy */
    child->mm    = parent->mm;          /* SHARED cursor */
    child->shared_vm = 1;
    child->parent_pid = parent->pid;   /* invisible to original parent's wait4 */
    child->stack_bottom = 0;            /* worker stacks are user-managed */

    /* Own trap frame page (copy of parent's register state) */
    struct la_trap_frame *ctf = (struct la_trap_frame *)la_pmem_alloc();
    if (!ctf) {
        child->state = LA_PROC_UNUSED;
        return (uint64_t)(-LA_ENOMEM);
    }
    {
        uint64_t *s = (uint64_t *)tf;
        uint64_t *d = (uint64_t *)ctf;
        uint64_t *e = (uint64_t *)(ctf + 1);
        while (d < e) *d++ = *s++;
    }
    ctf->gpr[LA_GPR_A0] = 0;           /* child returns 0 */
    ctf->era += LA_SYSCALL_INSN_SIZE;   /* resume past the syscall instruction */

    /* Child stack pointer */
    if (new_stack != 0)
        ctf->gpr[LA_GPR_SP] = new_stack;

    /* TLS register — musl's __clone calls clone with tls=td->self */
    if ((flags & 0x00080000UL) && tls != 0)
        ctf->gpr[LA_GPR_TP] = tls;

    child->tf = ctf;

    /* Copy file descriptors (CLONE_FILES semantics — shared table) */
    for (int i = 0; i < LA_NFD; i++)
        child->fds[i] = parent->fds[i];

    /* Bump pipe-end reference counts for inherited pipe fds. */
    la_pipe_dup_all(child);

    child->cwd_ino = parent->cwd_ino;

    /* Inherit signal state */
    child->sig_pending = parent->sig_pending;
    child->sig_mask    = parent->sig_mask;
    for (int s = 0; s < LA_NSIG; s++)
        child->sig_actions[s] = parent->sig_actions[s];

    /* tid pointers */
    child->clear_child_tid = (flags & 0x00200000UL) ? ctid : 0;
    if ((flags & 0x00100000UL) && ptid != 0) {
        int cpid = child->pid;
        la_copy_to_user(ptid, &cpid, sizeof(cpid));
    }
    if ((flags & 0x01000000UL) && ctid != 0) {
        int cpid = child->pid;
        la_copy_to_user(ctid, &cpid, sizeof(cpid));
    }

    la_uart_puts("  clone: thread pid=");
    la_uart_put_hex(child->pid);
    la_uart_puts(" sp=");
    la_uart_put_hex(ctf->gpr[LA_GPR_SP]);
    la_uart_puts(" parent=");
    la_uart_put_hex(parent->pid);
    la_uart_puts("\n");

    return (uint64_t)child->pid;
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

/* SYS_writev(66): scatter/gather write — write multiple buffers at once.
 *   a0 = fd, a1 = iovec ptr, a2 = iov count
 * Each iovec entry is { void *iov_base; size_t iov_len; }.
 * Iterates all iovec entries and writes each buffer sequentially. */
static uint64_t sys_writev(struct la_trap_frame *tf)
{
    int fd        = (int)tf->gpr[LA_GPR_A0];
    uint64_t uiov  = tf->gpr[LA_GPR_A1];
    int iovcnt    = (int)tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p) return (uint64_t)-1;
    if (fd < 0 || fd >= LA_NFD) return (uint64_t)-1;
    if (iovcnt <= 0) return 0;

    uint64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        /* iovec = { iov_base (8 bytes), iov_len (8 bytes) } */
        uint8_t iov[16];
        if (la_copy_from_user(iov, uiov + i * 16UL, 16) != 16)
            break;
        uint64_t base = *(uint64_t *)&iov[0];
        uint64_t len  = *(uint64_t *)&iov[8];

        if (len == 0) continue;

        /* Re-use the existing write path per-buffer.
         * We build a mini trap frame so sys_write sees the right args. */
        struct la_trap_frame wtf = *tf;
        wtf.gpr[LA_GPR_A1] = base;
        wtf.gpr[LA_GPR_A2] = (uint64_t)(uint32_t)len;
        uint64_t n = sys_write(&wtf);
        if (n > (uint64_t)len) break;   /* error */
        total += n;
        if (n < len) break;             /* short write — stop */
    }
    return total;
}

/* SYS_clock_gettime(113): get clock time.
 *   a0 = clock_id, a1 = struct timespec *tp (16 bytes: tv_sec + tv_nsec)
 * Returns 0 on success.  Only CLOCK_MONOTONIC(1) / CLOCK_REALTIME(0)
 * are supported; returns time based on the kernel tick counter. */
static uint64_t sys_clock_gettime(struct la_trap_frame *tf)
{
    uint64_t clock_id = tf->gpr[LA_GPR_A0];
    uint64_t utp      = tf->gpr[LA_GPR_A1];
    struct la_proc *p  = la_current_proc();

    if (!p || !utp) return (uint64_t)-1;

    /* Our timer runs at 100 Hz; each tick = 10 ms.
     * tv_sec = ticks/100, tv_nsec = (ticks%100) * 10_000_000 */
    uint64_t ticks = la_timer_get_ticks();
    uint64_t sec   = ticks / 100;
    uint64_t nsec  = (ticks % 100) * 10000000UL;

    /* struct timespec: tv_sec (8 bytes) + tv_nsec (8 bytes) */
    uint64_t ts[2] = { sec, nsec };
    la_copy_to_user(utp, ts, sizeof(ts));
    (void)clock_id;
    return 0;
}

/* SYS_getcpu(169): get CPU and NUMA node.
 *   a0 = *cpu, a1 = *node, a2 = tcache (ignored)
 * Returns 0.  Single-CPU kernel — always cpu=0, node=0. */
static uint64_t sys_getcpu(struct la_trap_frame *tf)
{
    uint64_t ucpu = tf->gpr[LA_GPR_A0];
    uint64_t unode = tf->gpr[LA_GPR_A1];
    uint32_t zero = 0;

    if (ucpu)
        la_copy_to_user(ucpu, &zero, sizeof(zero));
    if (unode)
        la_copy_to_user(unode, &zero, sizeof(zero));
    return 0;
}

/* ---- Minimal stubs for socket / sched / select / times ----
 * These return -ENOSYS so programs degrade gracefully.  Real
 * implementations are planned for later steps (loopback socket
 * for iperf/netperf, select/poll for lmbench, scheduler for
 * cyclictest).  */

static uint64_t sys_stub_enosys(struct la_trap_frame *tf) { (void)tf; return (uint64_t)(-LA_ENOSYS); }
static uint64_t sys_gettimeofday(struct la_trap_frame *tf)
{
    /* Return time based on ticks (same as clock_gettime). */
    uint64_t utv = tf->gpr[LA_GPR_A0];
    /* uint64_t utz = tf->gpr[LA_GPR_A1]; */  /* timezone, ignored */
    if (!utv) return 0;
    uint64_t ticks = la_timer_get_ticks();
    uint64_t sec  = ticks / 100;
    uint64_t usec = (ticks % 100) * 10000UL;
    uint64_t tv[2] = { sec, usec };
    la_copy_to_user(utv, tv, sizeof(tv));
    return 0;
}
static uint64_t sys_times(struct la_trap_frame *tf)
{
    uint64_t ubuf = tf->gpr[LA_GPR_A0];
    if (ubuf) {
        /* Return 0 for all fields (simplified). */
        uint64_t tms[4] = { 0, 0, 0, 0 };
        la_copy_to_user(ubuf, tms, sizeof(tms));
    }
    return 0;
}
static uint64_t sys_sched_setaffinity(struct la_trap_frame *tf)
{
    /* Single CPU — always succeed. */
    (void)tf;
    return 0;
}
static uint64_t sys_sched_getaffinity(struct la_trap_frame *tf)
{
    uint64_t ubuf = tf->gpr[LA_GPR_A2];
    if (ubuf) {
        uint64_t mask = 1;  /* CPU 0 only */
        la_copy_to_user(ubuf, &mask, 8);
    }
    return 0;
}
static uint64_t sys_sched_setscheduler(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;   /* accept any policy */
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
    case SYS_futex:      return sys_futex(tf);

    /* Signal */
    case SYS_kill:       return sys_kill(tf);
    case SYS_tgkill:     return sys_tgkill(tf);
    case SYS_rt_sigreturn: return sys_rt_sigreturn(tf);

    /* File info */
    case SYS_statx:      return sys_statx(tf);
    case SYS_uname:      return sys_uname(tf);

    /* Other stubs */
    case SYS_msync:      return sys_msync(tf);
    case SYS_pipe2:      return sys_pipe2(tf);
    case SYS_writev:     return sys_writev(tf);
    case SYS_sched_yield: return sys_sched_yield(tf);
    case SYS_prlimit64:  return sys_prlimit64(tf);
    case SYS_clock_gettime: return sys_clock_gettime(tf);
    case SYS_getcpu:     return sys_getcpu(tf);
    case SYS_gettimeofday: return sys_gettimeofday(tf);
    case SYS_times:      return sys_times(tf);

    /* Scheduler (minimal) */
    case SYS_sched_setaffinity: return sys_sched_setaffinity(tf);
    case SYS_sched_getaffinity: return sys_sched_getaffinity(tf);
    case SYS_sched_setscheduler: return sys_sched_setscheduler(tf);

    /* Select / poll */
    case SYS_pselect6:   return sys_stub_enosys(tf);
    case SYS_ppoll:      return sys_stub_enosys(tf);

    /* Socket family (ENOSYS stubs for now) */
    case SYS_socket:     return sys_stub_enosys(tf);
    case SYS_bind:       return sys_stub_enosys(tf);
    case SYS_listen:     return sys_stub_enosys(tf);
    case SYS_accept:     return sys_stub_enosys(tf);
    case SYS_connect:    return sys_stub_enosys(tf);
    case SYS_sendto:     return sys_stub_enosys(tf);
    case SYS_recvfrom:   return sys_stub_enosys(tf);
    case SYS_getsockname: return sys_stub_enosys(tf);
    case SYS_getpeername: return sys_stub_enosys(tf);

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
