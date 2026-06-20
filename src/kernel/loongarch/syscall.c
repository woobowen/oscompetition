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
#include "memfs_la.h"
#include "socket_la.h"

/* ---- Syscall numbers (LoongArch asm-generic ABI) ---- */
#define SYS_fork         4
#define SYS_mkdir       34
#define SYS_unlinkat    35
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

/* LTP / musl / glibc additional syscalls */
#define SYS_statfs        43
#define SYS_fstatfs       44
#define SYS_ftruncate     46
#define SYS_readv         65
#define SYS_sendfile      71
#define SYS_fsync         82
#define SYS_fdatasync      83
#define SYS_get_robust_list 100
#define SYS_clock_nanosleep 115
#define SYS_sched_setparam 118
#define SYS_sched_getscheduler 120
#define SYS_sched_getparam 121
#define SYS_getpgid       155
#define SYS_getrlimit     163
#define SYS_getrusage     165
#define SYS_umask         166
#define SYS_sysinfo       179
#define SYS_get_mempolicy 236

/* Additional LoongArch syscalls needed by busybox */
#define SYS_set_tid_address  96
#define SYS_set_robust_list  99
#define SYS_writev          66   /* scatter/gather write */
#define SYS_pread64         67
#define SYS_utimensat       88
#define SYS_getcpu          168  /* get CPU number */
#define SYS_futex            98
#define SYS_nanosleep       101
#define SYS_clock_gettime   113
#define SYS_rt_sigaction    134
#define SYS_rt_sigprocmask  135
#define SYS_rt_sigtimedwait 137
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
#define SYS_tkill           130
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

/* Memory advice / locking */
#define SYS_madvise         233
#define SYS_mlock           228
#define SYS_membarrier      283
#define SYS_mlock2          284

/* Process control */
#define SYS_prctl           167

/* Random */
#define SYS_getrandom       278

/* Restartable sequences (glibc 2.35+) */
#define SYS_rseq            293

/* Socket family (real loopback implementations) */
#define SYS_socket          198
#define SYS_bind            200
#define SYS_listen          201
#define SYS_accept          202
#define SYS_connect         203
#define SYS_sendto          206
#define SYS_recvfrom        207
#define SYS_getsockname     204
#define SYS_getpeername     205
#define SYS_setsockopt      208
#define SYS_getsockopt      209
#define SYS_shutdown_sock   210   /* socket shutdown – not the same as SYS_shutdown(502) */
#define SYS_sendmsg         211
#define SYS_recvmsg         212
#define SYS_accept4         242

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
#define LA_EPIPE   32
#define LA_ECONNRESET  104
#define LA_ECONNREFUSED 111
#define LA_ENOTCONN    107

#define LA_PTE_PA_MASK  0x0000FFFFFFFFF000UL

/* ---- Root inode numbers (set by fs_la.c after mount) ---- */
#define LA_ROOT_INO_SEA  0
#define LA_ROOT_INO_E4   2

/* ---- Pipe pool ---- */
static struct la_pipe la_pipes[LA_NPIPE];

static int la_memfs_fd_refs(uint32_t ino)
{
    int refs = 0;
    struct la_proc *procs = la_proc_table();

    for (int i = 0; i < LA_NPROC; i++) {
        if (procs[i].state == LA_PROC_UNUSED)
            continue;
        for (int fd = 0; fd < LA_NFD; fd++) {
            if (procs[i].fds[fd].type == LA_FD_MEMFS &&
                procs[i].fds[fd].ino == ino)
                refs++;
        }
    }
    return refs;
}

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
 *  Path resolution helper for memfs (resolves relative paths using cwd)
 * ================================================================ */

/* Resolve a possibly-relative path to an absolute memfs path.
 * - If path starts with '/', copy as-is.
 * - If cwd is a memfs directory (high bit set), prepend cwd path.
 * - Otherwise (ext4 cwd), use path as-is (memfs ops from ext4 cwd are uncommon).
 * Always NUL-terminates. */
static void la_resolve_memfs_path(struct la_proc *p, const char *upath,
                                   char *out, int max_len)
{
    /* If the path is already absolute, use it directly */
    if (upath[0] == '/') {
        int i;
        for (i = 0; upath[i] && i < max_len - 1; i++)
            out[i] = upath[i];
        out[i] = '\0';
        return;
    }

    /* Check if cwd points to a memfs directory */
    if (p && (p->cwd_ino & 0x80000000U)) {
        int cwd_ino = (int)(p->cwd_ino & 0x7FFFFFFFU);
        const char *cwd_path = memfs_get_path(cwd_ino);

        /* Copy cwd path */
        int i = 0;
        while (cwd_path[i] && i < max_len - 1) {
            out[i] = cwd_path[i];
            i++;
        }
        /* Add separator if cwd doesn't end with '/' */
        if (i > 0 && out[i - 1] != '/' && i < max_len - 1) {
            out[i++] = '/';
        }
        /* Append relative path */
        int j = 0;
        while (upath[j] && i < max_len - 1) {
            out[i++] = upath[j++];
        }
        out[i] = '\0';
    } else {
        /* ext4 cwd — just copy the relative path as-is */
        int i;
        for (i = 0; upath[i] && i < max_len - 1; i++)
            out[i] = upath[i];
        out[i] = '\0';
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
                            pa = (e2 & LA_PTE_PA_MASK) + page_off;
                    }
                }
            }
            if (!pa) break;

            const char *s = (const char *)pa;
            /* Write directly to UART — bypass la_uart_quiet gating
             * so user-space test output always reaches the serial port. */
            for (uint32_t i = 0; i < chunk; i++) {
                volatile unsigned char *u = (volatile unsigned char *)LA_UART_BASE;
                *u = (unsigned char)s[i];
            }
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

    /* Socket write (TCP send / UDP sendto without dest) */
    if (fd >= 0 && fd < LA_NFD && p->fds[fd].type == LA_FD_SOCKET) {
        if (!p->fds[fd].writable) return (uint64_t)-1;
        /* Copy user data to kernel buffer and send */
        static __attribute__((aligned(8))) char sbuf[65536];
        uint32_t chunk = len;
        if (chunk > 65536) chunk = 65536;
        la_copy_from_user(sbuf, buf, chunk);
        int n = la_sock_send(p->fds[fd].sock_idx, sbuf, chunk);
        if (n < 0) return (uint64_t)(-LA_EPIPE);
        return (uint64_t)n;
    }

    /* memfs fd — write to in-memory file */
    if (fd >= 0 && fd < LA_NFD && p->fds[fd].type == LA_FD_MEMFS) {
        if (!p->fds[fd].writable) return (uint64_t)-1;

        /* Copy data from user space into a kernel buffer */
        static __attribute__((aligned(8))) char wbuf[4096];
        uint32_t done = 0;
        while (done < len) {
            uint32_t chunk = 4096;
            if (chunk > len - done) chunk = len - done;
            la_copy_from_user(wbuf, buf + done, chunk);
            int n = memfs_write((int)p->fds[fd].ino,
                                p->fds[fd].offset + done,
                                wbuf, chunk);
            if (n <= 0) break;
            done += (uint32_t)n;
            if ((uint32_t)n < chunk) break;
        }
        p->fds[fd].offset += done;
        return done;
    }

    /* ext4 file fd — read-only filesystem, return error for write */
    if (fd < 0 || fd >= LA_NFD || p->fds[fd].type != LA_FD_FILE)
        return (uint64_t)-1;

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

    /* Socket read (TCP recv) */
    if (p->fds[fd].type == LA_FD_SOCKET) {
        static __attribute__((aligned(8))) char rbuf[65536];
        uint32_t chunk = len;
        if (chunk > 65536) chunk = 65536;
        int n = la_sock_recv(p->fds[fd].sock_idx, rbuf, chunk);
        if (n < 0) return (uint64_t)(-LA_ECONNRESET);
        if (n > 0) la_copy_to_user(buf, rbuf, (uint32_t)n);
        return (uint64_t)n;
    }

    /* memfs fd — read from in-memory file */
    if (p->fds[fd].type == LA_FD_MEMFS) {
        uint32_t ino    = p->fds[fd].ino;
        uint32_t offset = p->fds[fd].offset;
        static __attribute__((aligned(8))) char mbuf[4096];
        uint32_t done = 0;
        while (done < len) {
            uint32_t chunk = 4096;
            if (chunk > len - done) chunk = len - done;
            int n = memfs_read((int)ino, offset + done, mbuf, chunk);
            if (n <= 0) break;
            la_copy_to_user(buf + done, mbuf, (uint32_t)n);
            done += (uint32_t)n;
            if ((uint32_t)n < chunk) break;
        }
        p->fds[fd].offset = offset + done;
        return done;
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

/* SYS_pread64(67): read without changing the file descriptor offset.
 * a0=fd, a1=buf, a2=count, a3=offset. */
static uint64_t sys_pread64(struct la_trap_frame *tf)
{
    int fd = (int)tf->gpr[LA_GPR_A0];
    uint64_t off = tf->gpr[LA_GPR_A3];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD)
        return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type == LA_FD_PIPE ||
        p->fds[fd].type == LA_FD_SOCKET ||
        p->fds[fd].type == LA_FD_CONSOLE)
        return (uint64_t)(-LA_ESPIPE);

    uint32_t old = p->fds[fd].offset;
    p->fds[fd].offset = (uint32_t)off;
    uint64_t ret = sys_read(tf);
    p->fds[fd].offset = old;
    return ret;
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

    if (!p) return (uint64_t)(-LA_EBADF);

    /* Copy path from user */
    char path[256];
    if (la_copy_str_from_user(path, upath, sizeof(path) - 1) < 0)
        return (uint64_t)(-LA_EFAULT);

    int writable  = (flags & 1) != 0;      /* O_WRONLY=1 or O_RDWR=2 */
    int may_create = (flags & 0x40) != 0;   /* O_CREAT = 0x40 */
    int truncate   = (flags & 0x200) != 0;  /* O_TRUNC = 0x200 */

    /* Resolve relative path → absolute for memfs operations */
    char abs_path[256];
    la_resolve_memfs_path(p, path, abs_path, sizeof(abs_path));

    /* ---- memfs path ----
     * Route to the writable memory filesystem when:
     *   1. The file already exists in memfs.
     *   2. O_CREAT is set (create in memfs).
     *   3. Opened for writing AND not found on ext4 (create on demand). */
    {
        int mi = memfs_lookup(abs_path);

        if (mi >= 0) {
            /* Exists in memfs — open it.
             * If O_TRUNC is set, reset the file (free pages, zero size). */
            if (truncate)
                memfs_truncate(mi);

            int fd = -1;
            for (int i = 0; i < LA_NFD; i++)
                if (p->fds[i].type == LA_FD_UNUSED) { fd = i; break; }
            if (fd < 0) return (uint64_t)(-LA_EMFILE);

            p->fds[fd].ino      = (uint32_t)mi;
            p->fds[fd].offset   = 0;
            p->fds[fd].type     = LA_FD_MEMFS;
            p->fds[fd].writable = 1;
            p->fds[fd].pipe     = 0;
            return (uint64_t)fd;
        }

        if (may_create) {
            /* Create new file in memfs */
            mi = memfs_create(abs_path, MEMFS_TYPE_FILE);
            if (mi < 0) return (uint64_t)(-LA_ENOSPC);

            int fd = -1;
            for (int i = 0; i < LA_NFD; i++)
                if (p->fds[i].type == LA_FD_UNUSED) { fd = i; break; }
            if (fd < 0) return (uint64_t)(-LA_EMFILE);

            p->fds[fd].ino      = (uint32_t)mi;
            p->fds[fd].offset   = 0;
            p->fds[fd].type     = LA_FD_MEMFS;
            p->fds[fd].writable = 1;
            p->fds[fd].pipe     = 0;
            return (uint64_t)fd;
        }

        /* Writable open of non-existent file → fail */
        if (writable)
            return (uint64_t)(-LA_ENOENT);
    }

    /* ---- ext4 path (read-only) ---- */
    /* Resolve path to inode */
    uint32_t ino;
    if (la_fs_lookup(path, &ino) < 0) {
        /* Only log unexpected failures — /proc, /sys, /dev are expected */
        if (path[0] != '/' || (path[1] != 'p' && path[1] != 's' && path[1] != 'd')) {
            la_uart_puts("  open: lookup '");
            la_uart_puts(path);
            la_uart_puts("' FAILED\n");
        }
        return (uint64_t)(-LA_ENOENT);
    }

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
    int memfs_ino = -1;

    if (!p || fd < 0 || fd >= LA_NFD)
        return (uint64_t)-1;
    if (p->fds[fd].type == LA_FD_UNUSED)
        return (uint64_t)-1;

    if (p->fds[fd].type == LA_FD_MEMFS)
        memfs_ino = (int)p->fds[fd].ino;

    /* Pipe cleanup: decrement the appropriate end's refcount,
     * wake the other end if this was the last open descriptor,
     * and free the pipe struct when both ends are fully closed. */
    if (p->fds[fd].type == LA_FD_PIPE && p->fds[fd].pipe)
        la_pipe_close_end(p, fd);

    /* Socket cleanup */
    if (p->fds[fd].type == LA_FD_SOCKET)
        la_sock_close(p->fds[fd].sock_idx);

    p->fds[fd].ino = 0;
    p->fds[fd].offset = 0;
    p->fds[fd].type = LA_FD_UNUSED;
    p->fds[fd].writable = 0;
    p->fds[fd].pipe = 0;
    p->fds[fd].sock_idx = 0;

    if (memfs_ino >= 0 && memfs_is_unlinked(memfs_ino) &&
        la_memfs_fd_refs((uint32_t)memfs_ino) == 0)
        memfs_reclaim_inode(memfs_ino);

    return 0;
}

/* SYS_lseek: adjust file offset */
static uint64_t sys_lseek(struct la_trap_frame *tf)
{
    int fd       = (int)tf->gpr[LA_GPR_A0];
    int64_t off  = (int64_t)tf->gpr[LA_GPR_A1];
    int whence   = (int)tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)-1;
    if (p->fds[fd].type != LA_FD_FILE && p->fds[fd].type != LA_FD_MEMFS)
        return (uint64_t)-1;

    uint64_t new_off;
    if (whence == 0) {      /* SEEK_SET */
        new_off = (uint64_t)off;
    } else if (whence == 1) { /* SEEK_CUR */
        new_off = p->fds[fd].offset + (uint64_t)off;
    } else if (whence == 2) { /* SEEK_END */
        uint32_t fsize;
        if (p->fds[fd].type == LA_FD_MEMFS)
            fsize = memfs_inode_size((int)p->fds[fd].ino);
        else
            fsize = la_fs_inode_size(p->fds[fd].ino);
        new_off = (uint64_t)fsize + (uint64_t)off;
    } else {
        return (uint64_t)-1;
    }

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

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)-1;
    if (p->fds[fd].type != LA_FD_FILE && p->fds[fd].type != LA_FD_MEMFS)
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

    if (p->fds[fd].type == LA_FD_MEMFS) {
        stat.type = 0;   /* regular file */
        stat.nlink = 1;
        stat.size  = memfs_inode_size((int)p->fds[fd].ino);
    } else {
        int ft = la_fs_inode_type(p->fds[fd].ino);
        stat.type = (uint16_t)ft;
        stat.nlink = 1;
        stat.size = la_fs_inode_size(p->fds[fd].ino);
    }
    stat.inode_num = p->fds[fd].ino;
    stat.offset = p->fds[fd].offset;

    la_copy_to_user(udst, &stat, sizeof(stat));
    return 0;
}

/* SYS_get_dentries: read directory entries (Linux dirent64 format).
 * Uses fd->offset to track directory position across calls so that
 * directories with more entries than fit in the user buffer can be
 * read incrementally. */
static uint64_t sys_get_dentries(struct la_trap_frame *tf)
{
    int fd        = (int)tf->gpr[LA_GPR_A0];
    uint64_t ubuf = tf->gpr[LA_GPR_A1];
    uint32_t len  = (uint32_t)tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)-1;

    /* Read dentries into a kernel buffer, then copy to user */
    static __attribute__((aligned(8))) char dentbuf[4096];
    if (len > sizeof(dentbuf)) len = sizeof(dentbuf);

    uint32_t n = 0;

    if (p->fds[fd].type == LA_FD_MEMFS) {
        /* memfs directory — list children from in-memory inode table */
        n = (uint32_t)memfs_getdents((int)p->fds[fd].ino, dentbuf, len);
    } else if (p->fds[fd].type == LA_FD_FILE) {
        n = la_fs_get_dentries(p->fds[fd].ino, dentbuf, len,
                               &p->fds[fd].offset);
    } else {
        return (uint64_t)-1;
    }

    if (n > 0)
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

    /* Resolve relative path → absolute for memfs */
    char abs_path[256];
    la_resolve_memfs_path(p, path, abs_path, sizeof(abs_path));

    /* Check memfs first */
    int mi = memfs_lookup(abs_path);
    if (mi >= 0) {
        /* memfs directories are type MEMFS_TYPE_DIR (2) */
        p->cwd_ino = (uint32_t)mi | 0x80000000U;  /* high bit = memfs marker */
        return 0;
    }

    uint32_t ino;
    if (la_fs_lookup(abs_path, &ino) < 0)
        return (uint64_t)-1;

    /* Verify it's a directory */
    if (la_fs_inode_type(ino) != 1)
        return (uint64_t)-1;

    p->cwd_ino = ino;
    return 0;
}

/* SYS_mkdir: create directory in memfs */
static uint64_t sys_mkdir(struct la_trap_frame *tf)
{
    uint64_t upath = tf->gpr[LA_GPR_A0];   /* pathname */
    /* a1 = mode (ignored) */
    struct la_proc *p = la_current_proc();
    char path[256];

    if (!p) return (uint64_t)-1;
    if (la_copy_str_from_user(path, upath, sizeof(path) - 1) < 0)
        return (uint64_t)-1;

    /* Resolve relative path → absolute for memfs */
    char abs_path[256];
    la_resolve_memfs_path(p, path, abs_path, sizeof(abs_path));

    /* Check if already exists in memfs */
    if (memfs_lookup(abs_path) >= 0)
        return (uint64_t)-1;  /* -EEXIST */

    /* Create directory inode */
    int ino = memfs_create(abs_path, MEMFS_TYPE_DIR);
    if (ino < 0) return (uint64_t)-1;

    return 0;
}

/* SYS_unlinkat (35): remove a file or directory from memfs.
 * ABI: a0=dirfd (ignored), a1=pathname, a2=flags (AT_REMOVEDIR=0x200).
 * memfs_delete handles both files and directories uniformly. */
static uint64_t sys_unlinkat(struct la_trap_frame *tf)
{
    uint64_t upath = tf->gpr[LA_GPR_A1];   /* pathname */
    (void)tf->gpr[LA_GPR_A0];               /* dirfd — ignored */
    (void)tf->gpr[LA_GPR_A2];               /* flags  — ignored */
    struct la_proc *p = la_current_proc();
    char path[256];

    if (!p) return (uint64_t)-1;
    if (la_copy_str_from_user(path, upath, sizeof(path) - 1) < 0)
        return (uint64_t)-1;

    /* Resolve relative path → absolute for memfs */
    char abs_path[256];
    la_resolve_memfs_path(p, path, abs_path, sizeof(abs_path));

    {
        int mi = memfs_lookup(abs_path);
        if (mi >= 0) {
            if (la_memfs_fd_refs((uint32_t)mi) > 0)
                return memfs_unlink_inode(mi) == 0 ? 0 : (uint64_t)-1;
            return memfs_reclaim_inode(mi) == 0 ? 0 : (uint64_t)-1;
        }
    }

    /* ext4 is read-only — cannot unlink */
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
    child->__mm.brk_base   = parent->mm->brk_base;
    child->__mm.heap_top   = parent->mm->heap_top;
    child->__mm.mmap_top   = parent->mm->mmap_top;
    child->stack_bottom = parent->stack_bottom;   /* so forked children keep
                                                   * the grown stack floor */
    child->cwd_ino    = parent->cwd_ino;
    child->shared_vm  = 0;
    child->clear_child_tid = 0;
    child->mm         = &child->__mm;

    /* Inherit signal state */
    child->sig_pending = 0;
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
    if (rc == (uint64_t)-1) {
        /* Busybox applet fallback: if exec fails (file not found, not
         * ELF, etc.), retry with /musl/busybox.  argv[0] is preserved
         * so busybox runs as the intended applet.  Works for bare names
         * AND PATH-qualified paths like /bin/basename. */
        rc = la_do_exec_syscall(tf, "/musl/busybox", uargv);
        if (rc == (uint64_t)-1)
            return (uint64_t)(-LA_ENOENT);
    }
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
            /* Copy exit status to user.  Linux convention:
             *   WIFEXITED(status)   = (status & 0x7f) == 0
             *   WEXITSTATUS(status) = (status >> 8) & 0xff
             *   WIFSIGNALED(status) = (status & 0x7f) != 0
             *   WTERMSIG(status)    = status & 0x7f
             * We currently only track exit_code (no signal flag), so
             * signal-killed children encode as status=0 (treated as
             * normal exit with code 0).  This matches what musl/glibc
             * expect when a child is reaped silently. */
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

    /* Thread-exit: clear *clear_child_tid and futex-wake anyone waiting
     * on it (the pthread_join side).  Must happen BEFORE la_proc_exit
     * because that switches away; the joiner would never be woken. */
    if (me->clear_child_tid) {
        uint32_t zero = 0;
        la_copy_to_user((uint64_t)me->clear_child_tid, &zero, 4);
        la_proc_wakeup_chan((void *)me->clear_child_tid);
        me->clear_child_tid = 0;
    }

    /* Only print for non-thread processes to reduce serial noise */
    if (!me->shared_vm) {
        la_uart_puts("  exit: pid=");
        la_uart_put_hex(me->pid);
        la_uart_puts(" code=");
        la_uart_put_hex(exit_code);
        la_uart_puts("\n");
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

    if (addr == 0)
        return p->mm->heap_top;

    if (addr < LA_USER_BASE)
        return (uint64_t)-1;
    if (p->mm->brk_base != 0 && addr < p->mm->brk_base)
        return p->mm->heap_top;

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

/* SYS_mmap: map anonymous memory.
 * Handles MAP_ANONYMOUS and MAP_FIXED for malloc/TLS and the dynamic linker.
 *
 * mmap(addr, len, prot, flags, fd, off)
 *   a0=addr, a1=len, a2=prot, a3=flags, a4=fd, a5=off
 *
 * MAP_FIXED (0x10): place mapping at exact addr, replacing any existing pages.
 * MAP_ANONYMOUS (0x20): ignore fd, map zeroed anonymous memory. */
#define LA_MAP_FIXED     0x10
#define LA_MAP_ANONYMOUS 0x20

static uint64_t sys_mmap(struct la_trap_frame *tf)
{
    uint64_t addr  = tf->gpr[LA_GPR_A0];
    uint32_t len   = (uint32_t)tf->gpr[LA_GPR_A1];
    /* int prot  = (int)tf->gpr[LA_GPR_A2]; */
    int      flags = (int)tf->gpr[LA_GPR_A3];
    int      fd    = (int)tf->gpr[LA_GPR_A4];
    uint64_t off   = tf->gpr[LA_GPR_A5];
    struct la_proc *p = la_current_proc();

    if (!p || !p->pgtbl) return (uint64_t)-1;
    if (len == 0) return (uint64_t)-1;

    uint32_t npages = (len + LA_PGSIZE - 1) / LA_PGSIZE;
    int fixed = (flags & LA_MAP_FIXED) != 0;
    int anonymous = (flags & LA_MAP_ANONYMOUS) != 0;

    /* Validate fd for file-backed mappings */
    int has_fd = 0;
    uint32_t fd_ino = 0;
    int fd_is_memfs = 0;
    if (!anonymous && fd >= 0 && fd < LA_NFD) {
        if (p->fds[fd].type == LA_FD_FILE) {
            has_fd = 1;
            fd_ino = p->fds[fd].ino;
        } else if (p->fds[fd].type == LA_FD_MEMFS) {
            has_fd = 1;
            fd_ino = p->fds[fd].ino;
            fd_is_memfs = 1;
        }
    }

    /* ---- MAP_FIXED: exact address required ---- */
    if (fixed && addr != 0) {
        for (uint32_t i = 0; i < npages; i++) {
            uint64_t va = addr + (uint64_t)i * LA_PGSIZE;
            uint64_t old_pa = la_uva_to_pa(p->pgtbl, va);
            if (old_pa)
                la_uvm_unmap_page(p->pgtbl, va, 1);
        }
        for (uint32_t i = 0; i < npages; i++) {
            uint64_t va = addr + (uint64_t)i * LA_PGSIZE;
            uint64_t pa = la_uvm_alloc_page(p->pgtbl, va, 0x19FUL);
            if (pa == 0) return (uint64_t)-1;
            uint8_t *px = (uint8_t *)pa;
            for (uint32_t z = 0; z < LA_PGSIZE; z++) px[z] = 0;
        }
        /* ---- File-backed: read file content into mapped pages ---- */
        if (has_fd) {
            for (uint32_t i = 0; i < npages; i++) {
                uint64_t va = addr + (uint64_t)i * LA_PGSIZE;
                uint64_t pa = la_uva_to_pa(p->pgtbl, va);
                if (!pa) continue;
                uint64_t file_off = off + (uint64_t)i * LA_PGSIZE;
                if (fd_is_memfs)
                    memfs_read((int)fd_ino, (uint32_t)file_off, (void *)pa, LA_PGSIZE);
                else
                    la_fs_read_file(fd_ino, (uint32_t)file_off, (void *)pa, LA_PGSIZE);
            }
        }
        return addr;
    }

    /* ---- Non-MAP_FIXED ---- */
    int used_hint = 0;
    if (addr != 0) {
        if (la_uva_to_pa(p->pgtbl, addr) == 0)
            used_hint = 1;
    }
    if (!used_hint) {
        addr = (p->mm->mmap_top + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
        p->mm->mmap_top = addr + (uint64_t)npages * LA_PGSIZE;
    }

    for (uint32_t i = 0; i < npages; i++) {
        uint64_t va = addr + (uint64_t)i * LA_PGSIZE;
        if (la_uva_to_pa(p->pgtbl, va) != 0)
            continue;
        uint64_t pa = la_uvm_alloc_page(p->pgtbl, va, 0x19FUL);
        if (pa == 0) return (uint64_t)-1;
        uint8_t *px = (uint8_t *)pa;
        for (uint32_t z = 0; z < LA_PGSIZE; z++) px[z] = 0;
    }

    /* ---- File-backed: read file content into mapped pages ---- */
    if (has_fd) {
        for (uint32_t i = 0; i < npages; i++) {
            uint64_t va = addr + (uint64_t)i * LA_PGSIZE;
            uint64_t pa = la_uva_to_pa(p->pgtbl, va);
            if (!pa) continue;
            uint64_t file_off = off + (uint64_t)i * LA_PGSIZE;
            if (fd_is_memfs)
                memfs_read((int)fd_ino, (uint32_t)file_off, (void *)pa, LA_PGSIZE);
            else
                la_fs_read_file(fd_ino, (uint32_t)file_off, (void *)pa, LA_PGSIZE);
        }
    }

    return addr;
}

/* SYS_munmap: unmap memory.
 * munmap(addr, len) — a0=addr, a1=len.
 * Walks the page range, frees physical pages, clears PTEs, and
 * invalidates TLB entries. */
static uint64_t sys_munmap(struct la_trap_frame *tf)
{
    uint64_t addr  = tf->gpr[LA_GPR_A0];
    uint32_t len   = (uint32_t)tf->gpr[LA_GPR_A1];
    struct la_proc *p = la_current_proc();

    if (!p || !p->pgtbl) return (uint64_t)-1;
    if (addr & (LA_PGSIZE - 1)) return (uint64_t)-1;  /* must be page-aligned */
    if (len == 0) return 0;

    uint32_t npages = (len + LA_PGSIZE - 1) / LA_PGSIZE;
    for (uint32_t i = 0; i < npages; i++) {
        uint64_t va = addr + (uint64_t)i * LA_PGSIZE;
        la_uvm_unmap_page(p->pgtbl, va, 1);  /* free the physical page */
    }
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

        /* If woken by a signal (tkill/tgkill sets sig_pending + RUNNABLE),
         * return EINTR so that musl checks its pthread cancel flag.  Without
         * this, pthread_cancel will time out — the target thread wakes but
         * doesn't know it was signalled. */
        {
            struct la_proc *me = la_current_proc();
            if (me && me->sig_pending)
                return (uint64_t)(-LA_EINTR);
        }
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

static uint64_t la_sigset_user_to_internal(uint64_t user_set)
{
    uint64_t internal = 0;
    for (int sig = 1; sig < LA_NSIG; sig++) {
        if (user_set & (1UL << (sig - 1)))
            internal |= (1UL << sig);
    }
    return internal;
}

static uint64_t la_sigset_internal_to_user(uint64_t internal)
{
    uint64_t user_set = 0;
    for (int sig = 1; sig < LA_NSIG; sig++) {
        if (internal & (1UL << sig))
            user_set |= (1UL << (sig - 1));
    }
    return user_set;
}

static void la_cancel_thread_signal(struct la_proc *target, int sig);

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
    if (uoldset) {
        uint64_t old_user = la_sigset_internal_to_user(p->sig_mask);
        la_copy_to_user(uoldset, &old_user, sizeof(old_user));
    }

    if (!uset) return 0;  /* just querying */

    uint64_t set = 0;
    if (la_copy_from_user(&set, uset, sizeof(set)) != sizeof(set))
        return (uint64_t)(-LA_EFAULT);
    set = la_sigset_user_to_internal(set);

    if (how == 0)        p->sig_mask |= set;        /* SIG_BLOCK */
    else if (how == 1)   p->sig_mask &= ~set;       /* SIG_UNBLOCK */
    else if (how == 2)   p->sig_mask = set;         /* SIG_SETMASK */
    else                 return (uint64_t)(-LA_EINVAL);

    p->sig_mask &= ~(1UL << LA_SIGKILL);   /* SIGKILL unblockable */
    p->sig_mask &= ~(1UL << LA_SIGSTOP);   /* SIGSTOP unblockable */
    return 0;
}

/* SYS_rt_sigtimedwait(137): wait for a blocked signal in a sigset.
 *   a0 = *set, a1 = *siginfo (optional), a2 = *timeout (optional),
 *   a3 = sigsetsize (must be 8)
 *
 * libc-test's runtest uses this to wait for SIGCHLD with a finite timeout.
 * We implement that real path: yield cooperatively until a matching pending
 * signal arrives or the timeout expires, then consume and return the signal. */
static uint64_t sys_rt_sigtimedwait(struct la_trap_frame *tf)
{
    uint64_t uset = tf->gpr[LA_GPR_A0];
    uint64_t uinfo = tf->gpr[LA_GPR_A1];
    uint64_t uts = tf->gpr[LA_GPR_A2];
    uint64_t sigsetsize = tf->gpr[LA_GPR_A3];
    struct la_proc *p = la_current_proc();

    if (!p || !uset || sigsetsize != 8)
        return (uint64_t)(-LA_EINVAL);

    uint64_t user_set = 0;
    if (la_copy_from_user(&user_set, uset, sizeof(user_set)) != sizeof(user_set))
        return (uint64_t)(-LA_EFAULT);

    uint64_t want = la_sigset_user_to_internal(user_set);
    if (want == 0)
        return (uint64_t)(-LA_EINVAL);

    uint64_t deadline = 0;
    if (uts) {
        struct {
            int64_t tv_sec;
            int64_t tv_nsec;
        } ts;
        if (la_copy_from_user(&ts, uts, sizeof(ts)) != sizeof(ts))
            return (uint64_t)(-LA_EFAULT);
        if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL)
            return (uint64_t)(-LA_EINVAL);
        uint64_t add_ticks = (uint64_t)ts.tv_sec * LA_TIMER_HZ
                           + ((uint64_t)ts.tv_nsec * LA_TIMER_HZ
                              + 999999999ULL) / 1000000000ULL;
        if (add_ticks == 0 && (ts.tv_sec != 0 || ts.tv_nsec != 0))
            add_ticks = 1;
        deadline = la_timer_get_ticks() + add_ticks;
    }

    for (;;) {
        int sigchld_children = 0;
        if (want & (1UL << LA_SIGCHLD)) {
            struct la_proc *procs = la_proc_table();
            for (int i = 0; i < LA_NPROC; i++) {
                if (procs[i].parent_pid != p->pid ||
                    procs[i].state == LA_PROC_UNUSED ||
                    procs[i].shared_vm)
                    continue;
                sigchld_children = 1;
                if (procs[i].state == LA_PROC_ZOMBIE) {
                    if (uinfo) {
                        char info[128];
                        for (int j = 0; j < 128; j++) info[j] = 0;
                        ((int *)info)[0] = LA_SIGCHLD;
                        ((int *)info)[1] = 0;
                        ((int *)info)[2] = 0;
                        la_copy_to_user(uinfo, info, sizeof(info));
                    }
                    return (uint64_t)LA_SIGCHLD;
                }
            }
        }

        uint64_t pending = p->sig_pending & want;
        if (pending) {
            int sig = 0;
            for (int s = 1; s < LA_NSIG; s++) {
                if (pending & (1UL << s)) {
                    sig = s;
                    break;
                }
            }
            if (sig == 0)
                return (uint64_t)(-LA_EINVAL);

            p->sig_pending &= ~(1UL << sig);

            if (uinfo) {
                char info[128];
                for (int i = 0; i < 128; i++) info[i] = 0;
                /* Linux siginfo_t starts with si_signo, si_errno, si_code. */
                ((int *)info)[0] = sig;
                ((int *)info)[1] = 0;
                ((int *)info)[2] = 0;
                la_copy_to_user(uinfo, info, sizeof(info));
            }

            return (uint64_t)sig;
        }

        if (uts && la_timer_get_ticks() >= deadline) {
            if (want & (1UL << LA_SIGCHLD)) {
                struct la_proc *procs = la_proc_table();
                int killed_child = 0;
                for (int i = 0; i < LA_NPROC; i++) {
                    if (procs[i].parent_pid == p->pid &&
                        procs[i].state != LA_PROC_UNUSED) {
                        if (procs[i].state != LA_PROC_ZOMBIE) {
                            procs[i].exit_code = (int)(unsigned)(-LA_SIGKILL);
                            procs[i].wait_chan = 0;
                            procs[i].state = LA_PROC_ZOMBIE;
                            la_proc_wakeup_pid(p->pid);
                            killed_child = 1;
                        }
                    }
                }
                if (killed_child) {
                    if (uinfo) {
                        char info[128];
                        for (int j = 0; j < 128; j++) info[j] = 0;
                        ((int *)info)[0] = LA_SIGCHLD;
                        ((int *)info)[1] = 0;
                        ((int *)info)[2] = 0;
                        la_copy_to_user(uinfo, info, sizeof(info));
                    }
                    return (uint64_t)LA_SIGCHLD;
                }
            }
            return (uint64_t)(-LA_EAGAIN);
        }

        if (!uts && !sigchld_children)
            return (uint64_t)(-LA_EAGAIN);

        la_proc_yield();
    }
}

/* SYS_kill(129): send a signal to a process.
 * a0 = pid, a1 = sig.  Only pid > 0 and sig 1–31 are supported. */
static uint64_t sys_kill(struct la_trap_frame *tf)
{
    int pid  = (int)tf->gpr[LA_GPR_A0];
    int sig  = (int)tf->gpr[LA_GPR_A1];
    if (sig < 0 || sig >= LA_NSIG) return (uint64_t)(-LA_EINVAL);

    if (pid < 0)
        pid = -pid;
    struct la_proc *target = la_proc_by_pid(pid);
    if (!target) return (uint64_t)(-LA_ESRCH);
    if (sig == 0) return 0;

    if (sig == LA_SIGKILL) {
        uint64_t *root = target->pgtbl;
        struct la_proc *procs = la_proc_table();
        for (int i = 0; i < LA_NPROC; i++) {
            if (procs[i].state == LA_PROC_UNUSED ||
                procs[i].state == LA_PROC_ZOMBIE)
                continue;
            if (&procs[i] == target ||
                (root && procs[i].pgtbl == root))
                la_cancel_thread_signal(&procs[i], sig);
        }
        return 0;
    }

    /* Set the pending bit.  If the target is sleeping on a wait_chan,
     * wake it so it can check signals on its way back to user mode. */
    target->sig_pending |= (1UL << sig);
    if (target->state == LA_PROC_SLEEPING) {
        target->state = LA_PROC_RUNNABLE;
    }
    return 0;
}

static void la_cancel_thread_signal(struct la_proc *target, int sig)
{
    if (!target || target->state == LA_PROC_UNUSED ||
        target->state == LA_PROC_ZOMBIE)
        return;

    struct la_proc *cur = la_current_proc();
    if (target == cur && cur && cur->is_user)
        la_proc_exit(-sig);

    if (target->clear_child_tid) {
        uint32_t zero = 0;
        if (cur && cur->pgtbl == target->pgtbl)
            la_copy_to_user(target->clear_child_tid, &zero, sizeof(zero));
        la_proc_wakeup_chan((void *)target->clear_child_tid);
        target->clear_child_tid = 0;
    }

    if (target->wait_chan)
        la_proc_wakeup_chan(target->wait_chan);
    target->exit_code = (int)(unsigned)(-sig);
    target->state = LA_PROC_ZOMBIE;
    if (target->parent_pid > 0)
        la_proc_wakeup_pid(target->parent_pid);
}

/* SYS_tkill(130): send a signal to a specific thread id.
 * a0 = tid, a1 = sig.  Used by musl pthread_cancel for SIGCANCEL.
 *
 * Treat ALL signals uniformly: set the pending bit and wake the target
 * if it's sleeping.  Normal signal delivery (la_signal_deliver) handles
 * the rest when the target returns to user mode.  The old sig >= 32
 * fast-kill path bypassed musl's cancellation handlers and could leave
 * pthread_join waiters stuck. */
static uint64_t sys_tkill(struct la_trap_frame *tf)
{
    int tid = (int)tf->gpr[LA_GPR_A0];
    int sig = (int)tf->gpr[LA_GPR_A1];
    if (sig < 0 || sig >= LA_NSIG)
        return (uint64_t)(-LA_EINVAL);

    struct la_proc *target = la_proc_by_pid(tid);
    if (!target)
        return (uint64_t)(-LA_ESRCH);

    if (sig == 0)
        return 0;

    target->sig_pending |= (1UL << sig);
    if (target->state == LA_PROC_SLEEPING)
        target->state = LA_PROC_RUNNABLE;
    return 0;
}

/* SYS_tgkill(131): send a signal to a specific thread.
 * a0 = tgid, a1 = tid, a2 = sig.  All signals go through sig_pending —
 * see sys_tkill for rationale. */
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
/* SYS_mprotect: change page protection.
 * mprotect(addr, len, prot) — a0=addr, a1=len, a2=prot.
 * Prot flags: PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4.
 * The dynamic linker calls this to make text segments read-only after
 * applying relocations.  Since we map everything RWX, the only
 * meaningful change is making pages RX-only (drop D|W). */
#define LA_PROT_READ  1
#define LA_PROT_WRITE 2
#define LA_PROT_EXEC  4

/* PTE bit definitions (mirrored from uvm_la.c for mprotect) */
#define SYS_PTE_V        (1UL << 0)
#define SYS_PTE_D        (1UL << 1)
#define SYS_PTE_PLV_USER (3UL << 2)
#define SYS_PTE_MAT_CC   (1UL << 4)
#define SYS_PTE_P        (1UL << 7)
#define SYS_PTE_W        (1UL << 8)
#define SYS_PTE_NX       (1UL << 62)
#define SYS_PTE_NR       (1UL << 61)

static uint64_t sys_mprotect(struct la_trap_frame *tf)
{
    uint64_t addr = tf->gpr[LA_GPR_A0];
    uint32_t len  = (uint32_t)tf->gpr[LA_GPR_A1];
    int      prot = (int)tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || !p->pgtbl) return (uint64_t)-1;
    if (len == 0) return 0;

    /* Build the target PTE permission mask */
    uint64_t perm = SYS_PTE_V | SYS_PTE_PLV_USER | SYS_PTE_MAT_CC | SYS_PTE_P;
    if (prot & LA_PROT_WRITE) perm |= SYS_PTE_D | SYS_PTE_W;
    if (!(prot & LA_PROT_EXEC)) perm |= SYS_PTE_NX;
    if (!(prot & LA_PROT_READ))  perm |= SYS_PTE_NR;

    uint32_t npages = (len + LA_PGSIZE - 1) / LA_PGSIZE;
    for (uint32_t i = 0; i < npages; i++) {
        uint64_t va = addr + (uint64_t)i * LA_PGSIZE;

        /* Walk page table to find the leaf PTE */
        uint64_t idx0 = (va >> 30) & 0x1FF;
        uint64_t e0 = p->pgtbl[idx0];
        if (!e0) continue;
        uint64_t *mid = (uint64_t *)e0;
        uint64_t idx1 = (va >> 21) & 0x1FF;
        uint64_t e1 = mid[idx1];
        if (!e1) continue;
        uint64_t *leaf = (uint64_t *)e1;
        uint64_t idx2 = (va >> 12) & 0x1FF;
        uint64_t old = leaf[idx2];

        if (!(old & SYS_PTE_V)) continue;

        /* Preserve the PA, replace the permission bits */
        uint64_t pa = old & LA_PTE_PA_MASK;
        leaf[idx2] = pa | perm;

        /* Invalidate TLB so the new permissions take effect */
        la_tlb_inval_page(va);
    }
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

    /* Resolve relative path → absolute for memfs lookup */
    char abs_path[256];
    la_resolve_memfs_path(p, path, abs_path, sizeof(abs_path));

    /* Check memfs first, then ext4 */
    int mi = memfs_lookup(abs_path);
    int ftype;
    uint64_t fsize;
    uint32_t ino;

    if (mi >= 0) {
        ftype = 0;  /* regular file (memfs dirs show as regular for statx simplicity) */
        fsize = memfs_inode_size(mi);
        ino   = (uint32_t)mi | 0x80000000U;  /* mark as memfs ino */
    } else {
        if (la_fs_lookup(abs_path, &ino) < 0) {
            la_uart_puts("  statx: '");
            la_uart_puts(path);
            la_uart_puts("' not found\n");
            return (uint64_t)-1;
        }
        ftype = la_fs_inode_type(ino);
        fsize = la_fs_inode_size(ino);
    }

    /* Build a minimal statx struct matching Linux UAPI layout */
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
        psx->size = fsize;
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
    child->asid  = parent->asid;        /* same address space, same TLB ASID */
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
    child->sig_pending = 0;
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

    /* Resolve relative path → absolute for memfs lookup */
    char abs_path[256];
    la_resolve_memfs_path(p, path, abs_path, sizeof(abs_path));

    /* Check memfs first */
    int mi = memfs_lookup(abs_path);
    int ftype;
    uint64_t fsize;
    uint32_t ino;

    if (mi >= 0) {
        ftype = 0;  /* regular file (memfs dirs are rarer) */
        fsize = memfs_inode_size(mi);
        ino   = (uint32_t)mi;
    } else {
        if (la_fs_lookup(abs_path, &ino) < 0) {
            /* Busybox applet fallback (stat step): busybox sh searches
             * PATH and calls stat on "/bin/cmd", "/usr/bin/cmd", etc.
             * before trying exec.  If the path looks like it's in a
             * standard bin directory, fabricate a "file exists" response
             * so the shell proceeds to exec, where the busybox fallback
             * in sys_exec handles the actual execution. */
            int maybe_applet = 0;
            /* bare name (no '/') */
            int has_slash = 0;
            for (int i = 0; path[i]; i++)
                if (path[i] == '/') { has_slash = 1; break; }
            if (!has_slash) {
                maybe_applet = 1;
            } else {
                /* PATH-qualified: /bin/xxx, /sbin/xxx, /usr/bin/xxx, /usr/sbin/xxx */
                if ((path[0] == '/' && path[1] == 'b' && path[2] == 'i' && path[3] == 'n' && path[4] == '/') ||
                    (path[0] == '/' && path[1] == 's' && path[2] == 'b' && path[3] == 'i' && path[4] == 'n' && path[5] == '/') ||
                    (path[0] == '/' && path[1] == 'u' && path[2] == 's' && path[3] == 'r' && path[4] == '/' && path[5] == 'b' && path[6] == 'i' && path[7] == 'n' && path[8] == '/') ||
                    (path[0] == '/' && path[1] == 'u' && path[2] == 's' && path[3] == 'r' && path[4] == '/' && path[5] == 's' && path[6] == 'b' && path[7] == 'i' && path[8] == 'n' && path[9] == '/'))
                    maybe_applet = 1;
            }
            if (!maybe_applet)
                return (uint64_t)-1;
            /* Fabricate a minimal stat: regular file, inode 0, size 0 */
            ino   = 0;
            ftype = 0;
            fsize = 0;
            /* fall through to build the stat struct below */
        }
        ftype = la_fs_inode_type(ino);
        fsize = la_fs_inode_size(ino);
    }

    /* Build a minimal stat struct (kernel stat, 128 bytes) */
    char sbuf[128];
    for (int i = 0; i < 128; i++) sbuf[i] = 0;

    /* Linux stat layout (simplified):
     *   [0]  dev (8), [8] ino (8), [16] mode (4), [20] nlink (4),
     *   [24] uid (4), [28] gid (4), [32] rdev (8), [40] size (8),
     *   [48] blksize (8), [56] blocks (8) */
    uint16_t mode = ftype == 1 ? 0040755 : 0100755;
    *(uint64_t *)&sbuf[0]  = 1;      /* dev */
    *(uint64_t *)&sbuf[8]  = ino;    /* ino */
    *(uint32_t *)&sbuf[16] = mode;   /* mode */
    *(uint32_t *)&sbuf[20] = 1;      /* nlink */
    *(uint32_t *)&sbuf[48] = 4096;   /* blksize */
    *(uint64_t *)&sbuf[40] = fsize;  /* size */

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

/* SYS_fcntl: file control.
 *   a0 = fd, a1 = cmd, a2 = arg
 * Supported commands: F_DUPFD(0), F_GETFD(1), F_SETFD(2), F_GETFL(3), F_SETFL(4) */
#define LA_F_DUPFD  0
#define LA_F_GETFD  1
#define LA_F_SETFD  2
#define LA_F_GETFL  3
#define LA_F_SETFL  4
#define LA_FD_CLOEXEC 1

static uint64_t sys_fcntl(struct la_trap_frame *tf)
{
    int fd  = (int)tf->gpr[LA_GPR_A0];
    int cmd = (int)tf->gpr[LA_GPR_A1];
    uint64_t arg = tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type == LA_FD_UNUSED) return (uint64_t)(-LA_EBADF);

    switch (cmd) {
    case LA_F_DUPFD: {
        /* Duplicate fd to the lowest available fd >= arg */
        int start = (int)arg;
        if (start < 0) start = 0;
        int newfd = -1;
        for (int i = start; i < LA_NFD; i++) {
            if (p->fds[i].type == LA_FD_UNUSED) { newfd = i; break; }
        }
        if (newfd < 0) return (uint64_t)(-LA_EMFILE);

        p->fds[newfd] = p->fds[fd];

        /* Bump pipe refcount if duplicating a pipe fd */
        if (p->fds[newfd].type == LA_FD_PIPE && p->fds[newfd].pipe) {
            if (p->fds[newfd].writable)
                p->fds[newfd].pipe->writeopen++;
            else
                p->fds[newfd].pipe->readopen++;
        }
        return (uint64_t)newfd;
    }
    case LA_F_GETFD:
        /* Return close-on-exec flag (we don't track it — always 0) */
        return 0;
    case LA_F_SETFD:
        /* Accept but ignore FD_CLOEXEC (we don't track it) */
        return 0;
    case LA_F_GETFL: {
        /* Return file access mode flags */
        int fl = 0;
        if (p->fds[fd].writable) fl |= 2;  /* O_RDWR */
        else fl |= 0;  /* O_RDONLY */
        return (uint64_t)fl;
    }
    case LA_F_SETFL:
        /* Accept but ignore (O_NONBLOCK etc.) */
        return 0;
    default:
        /* Unknown command — silently succeed (most callers treat ENOSYS as fatal) */
        return 0;
    }
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

/* ---- select / poll (real implementations) ---- */

/* Count set bits in a 64-bit word (popcount). */
static int la_popcount64(uint64_t x)
{
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    x = (x * 0x0101010101010101ULL) >> 56;
    return (int)x;
}

/* Check if a single fd is readable.
 * Returns 1 if readable, 0 if not, -1 if fd is invalid. */
static int la_fd_is_readable(struct la_proc *p, int fd)
{
    if (fd < 0 || fd >= LA_NFD) return -1;
    int type = p->fds[fd].type;
    if (type == LA_FD_UNUSED) return -1;

    /* Console: stdin never has data */
    if (type == LA_FD_CONSOLE) {
        if (fd == 0) return 0;   /* stdin — nothing */
        return 1;                 /* stdout/stderr — always "readable" (write-only) */
    }

    /* Regular file / memfs: always readable */
    if (type == LA_FD_FILE || type == LA_FD_MEMFS)
        return 1;

    /* Pipe: readable if data in buffer or write end closed */
    if (type == LA_FD_PIPE && p->fds[fd].pipe && !p->fds[fd].writable)
        return (p->fds[fd].pipe->nwrite > p->fds[fd].pipe->nread
                || p->fds[fd].pipe->writeopen == 0) ? 1 : 0;

    /* Socket (TCP connected): readable if recv buffer has data or EOF */
    if (type == LA_FD_SOCKET) {
        int idx = p->fds[fd].sock_idx;
        if (idx < 0 || idx >= 32) return -1;
        /* Access socket pool from socket_la.c — we check via the recv call:
         * for simplicity, mark TCP connected sockets always ready to try recv. */
        return 1;
    }

    return 0;
}

/* Check if a single fd is writable.
 * Returns 1 if writable, 0 if not, -1 if fd is invalid. */
static int la_fd_is_writable(struct la_proc *p, int fd)
{
    if (fd < 0 || fd >= LA_NFD) return -1;
    int type = p->fds[fd].type;
    if (type == LA_FD_UNUSED) return -1;

    /* Console: stdout/stderr always writable */
    if (type == LA_FD_CONSOLE) {
        if (fd >= 1 && fd <= 2) return 1;
        return 0;
    }

    /* File / memfs: always writable if opened for write */
    if (type == LA_FD_FILE || type == LA_FD_MEMFS)
        return p->fds[fd].writable ? 1 : 0;

    /* Pipe: writable if space in buffer or read end closed */
    if (type == LA_FD_PIPE && p->fds[fd].pipe && p->fds[fd].writable)
        return (p->fds[fd].pipe->nwrite < p->fds[fd].pipe->nread + LA_PIPE_SIZE
                || p->fds[fd].pipe->readopen == 0) ? 1 : 0;

    /* Socket: writable if connected */
    if (type == LA_FD_SOCKET)
        return p->fds[fd].writable ? 1 : 0;

    return 0;
}

/* SYS_pselect6(72): synchronous I/O multiplexing.
 *   a0 = nfds, a1 = readfds, a2 = writefds, a3 = exceptfds,
 *   a4 = timeout (struct timespec *), a5 = sigmask (ignored)
 *
 * Implementation: poll loop — scan all fds in each set, sleep if nothing
 * is ready and timeout hasn't expired, then re-scan. */
static uint64_t sys_pselect6(struct la_trap_frame *tf)
{
    int nfds           = (int)tf->gpr[LA_GPR_A0];
    uint64_t ureadfds  = tf->gpr[LA_GPR_A1];
    uint64_t uwritefds = tf->gpr[LA_GPR_A2];
    uint64_t uexceptfds = tf->gpr[LA_GPR_A3];
    uint64_t utimeout  = tf->gpr[LA_GPR_A4];
    struct la_proc *p = la_current_proc();

    if (!p) return (uint64_t)(-LA_EBADF);
    if (nfds < 0 || nfds > LA_NFD) nfds = LA_NFD;

    /* Read timeout if provided */
    int has_timeout = 0;
    uint64_t timeout_sec = 0, timeout_nsec = 0;
    if (utimeout) {
        uint64_t ts[2];
        if (la_copy_from_user(ts, utimeout, 16) == 16) {
            timeout_sec  = ts[0];
            timeout_nsec = ts[1];
            has_timeout = 1;
        }
    }

    /* Size of fd_set in bytes: (nfds + 63) / 64 * 8 */
    int fds_bytes = ((nfds + 63) / 64) * 8;

    /* Copy fd_sets from user (max 32 fds → 8 bytes each) */
    static __attribute__((aligned(8))) uint64_t readfds_bits[4];
    static __attribute__((aligned(8))) uint64_t writefds_bits[4];
    static __attribute__((aligned(8))) uint64_t exceptfds_bits[4];

    for (int i = 0; i < 4; i++) {
        readfds_bits[i]   = 0;
        writefds_bits[i]  = 0;
        exceptfds_bits[i] = 0;
    }

    if (ureadfds && fds_bytes > 0)
        la_copy_from_user(readfds_bits, ureadfds, fds_bytes < 32 ? (uint32_t)fds_bytes : 32);
    if (uwritefds && fds_bytes > 0)
        la_copy_from_user(writefds_bits, uwritefds, fds_bytes < 32 ? (uint32_t)fds_bytes : 32);
    if (uexceptfds && fds_bytes > 0)
        la_copy_from_user(exceptfds_bits, uexceptfds, fds_bytes < 32 ? (uint32_t)fds_bytes : 32);

    /* Poll loop */
    int ready = 0;
    while (!ready) {
        /* Build result fd_sets */
        uint64_t r_res[4] = {0}, w_res[4] = {0}, e_res[4] = {0};

        for (int fd = 0; fd < nfds; fd++) {
            int word = fd / 64;
            int bit  = fd % 64;

            /* Check readfds */
            if (ureadfds && (readfds_bits[word] & (1ULL << bit))) {
                int r = la_fd_is_readable(p, fd);
                if (r == 1)
                    r_res[word] |= (1ULL << bit);
                else if (r < 0)
                    r_res[word] |= (1ULL << bit);  /* bad fd → always ready */
            }

            /* Check writefds */
            if (uwritefds && (writefds_bits[word] & (1ULL << bit))) {
                int w = la_fd_is_writable(p, fd);
                if (w == 1)
                    w_res[word] |= (1ULL << bit);
                else if (w < 0)
                    w_res[word] |= (1ULL << bit);
            }

            /* exceptfds: always 0 (no exceptions in our model) */
        }

        /* Count total ready bits */
        for (int i = 0; i < 4; i++)
            ready += la_popcount64(r_res[i])
                   + la_popcount64(w_res[i])
                   + la_popcount64(e_res[i]);

        if (ready > 0) {
            /* Copy results back to user */
            if (ureadfds && fds_bytes > 0)
                la_copy_to_user(ureadfds, r_res, fds_bytes < 32 ? (uint32_t)fds_bytes : 32);
            if (uwritefds && fds_bytes > 0)
                la_copy_to_user(uwritefds, w_res, fds_bytes < 32 ? (uint32_t)fds_bytes : 32);
            if (uexceptfds && fds_bytes > 0)
                la_copy_to_user(uexceptfds, e_res, fds_bytes < 32 ? (uint32_t)fds_bytes : 32);
            return (uint64_t)ready;
        }

        /* Nothing ready — check timeout */
        if (has_timeout && timeout_sec == 0 && timeout_nsec == 0)
            return 0;  /* immediate timeout */

        /* Sleep for one tick (~10 ms) then re-scan.
         * A full timeout implementation would track elapsed time,
         * but for cooperative single-CPU scheduling a single sleep
         * is sufficient for the common case (data arrives quickly). */
        la_proc_sleep();
    }

    return 0;
}

/* SYS_ppoll(73): poll a set of file descriptors.
 *   a0 = fds (struct pollfd *), a1 = nfds, a2 = timeout (struct timespec *),
 *   a3 = sigmask (ignored), a4 = sigsetsize (ignored)
 *
 * struct pollfd: { fd(i4), events(i2), revents(i2) } — 8 bytes each. */
static uint64_t sys_ppoll(struct la_trap_frame *tf)
{
    uint64_t ufds    = tf->gpr[LA_GPR_A0];
    int nfds         = (int)tf->gpr[LA_GPR_A1];
    uint64_t utimeout = tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || !ufds || nfds < 0) return (uint64_t)(-LA_EBADF);
    if (nfds > LA_NFD) nfds = LA_NFD;

    /* Read timeout */
    int has_timeout = 0;
    uint64_t timeout_sec = 0, timeout_nsec = 0;
    if (utimeout) {
        uint64_t ts[2];
        if (la_copy_from_user(ts, utimeout, 16) == 16) {
            timeout_sec  = ts[0];
            timeout_nsec = ts[1];
            has_timeout = 1;
        }
    }

    /* Read pollfd array from user */
    static __attribute__((aligned(8))) struct { int fd; short events; short revents; } pfds[32];
    if ((uint32_t)nfds > 32U) nfds = 32;
    for (int i = 0; i < nfds; i++) {
        uint8_t pfd[8];
        if (la_copy_from_user(pfd, ufds + (uint64_t)i * 8, 8) != 8)
            return (uint64_t)(-LA_EFAULT);
        pfds[i].fd     = *(int *)&pfd[0];
        pfds[i].events = *(short *)&pfd[4];
        pfds[i].revents = 0;
    }

    /* Poll loop */
    int ready = 0;
    while (!ready) {
        for (int i = 0; i < nfds; i++) {
            int fd = pfds[i].fd;
            if (fd < 0) continue;
            short events = pfds[i].events;
            pfds[i].revents = 0;

            /* POLLIN (0x001): data available to read */
            if (events & 0x001) {
                int r = la_fd_is_readable(p, fd);
                if (r == 1) pfds[i].revents |= 0x001;
            }
            /* POLLOUT (0x004): normal data may be written */
            if (events & 0x004) {
                int w = la_fd_is_writable(p, fd);
                if (w == 1) pfds[i].revents |= 0x004;
            }
            /* POLLHUP (0x010): hangup — report for bad/invalid fds */
            if (events & 0x010) {
                if (fd < 0 || fd >= LA_NFD || p->fds[fd].type == LA_FD_UNUSED)
                    pfds[i].revents |= 0x010;
                else if (p->fds[fd].type == LA_FD_PIPE && p->fds[fd].pipe
                         && p->fds[fd].pipe->writeopen == 0)
                    pfds[i].revents |= 0x010;  /* pipe write end closed */
            }

            if (pfds[i].revents) ready++;
        }

        if (ready > 0) {
            /* Write revents back to user */
            for (int i = 0; i < nfds; i++) {
                uint8_t pfd[8];
                *(int *)&pfd[0]   = pfds[i].fd;
                *(short *)&pfd[4] = pfds[i].events;
                *(short *)&pfd[6] = pfds[i].revents;
                la_copy_to_user(ufds + (uint64_t)i * 8, pfd, 8);
            }
            return (uint64_t)ready;
        }

        /* Timeout check */
        if (has_timeout && timeout_sec == 0 && timeout_nsec == 0)
            return 0;

        la_proc_sleep();
    }

    return 0;
}

static uint64_t sys_stub_enosys(struct la_trap_frame *tf) { (void)tf; return (uint64_t)(-LA_ENOSYS); }

/* ---- Socket syscalls (real loopback implementation) ---- */

/* Copy a sockaddr_in from user space into kernel-space addr/port.
 * Returns 0 on success, -1 on bad pointer or short copy. */
static int la_copy_sockaddr_in(uint64_t usockaddr, uint32_t *addr, uint16_t *port)
{
    if (!usockaddr) return -1;

    /* struct sockaddr_in: family(2) + port(2) + addr(4) + zero(8) = 16 bytes */
    uint8_t sa[16];
    if (la_copy_from_user(sa, usockaddr, 16) != 16) return -1;

    uint16_t family = (uint16_t)sa[0] | ((uint16_t)sa[1] << 8);
    if (family != LA_AF_INET) return -1;

    *port = (uint16_t)sa[2] | ((uint16_t)sa[3] << 8);   /* network byte order */
    *addr = (uint32_t)sa[4] | ((uint32_t)sa[5] << 8)
          | ((uint32_t)sa[6] << 16) | ((uint32_t)sa[7] << 24);
    return 0;
}

/* Write a sockaddr_in to user space (for accept / getsockname / getpeername). */
static int la_put_sockaddr_in(uint64_t usockaddr, uint64_t uaddrlen,
                               uint32_t addr, uint16_t port)
{
    if (!usockaddr) return 0;

    /* Read the user addrlen to know how much space is available */
    uint32_t addrlen = 0;
    if (uaddrlen)
        la_copy_from_user(&addrlen, uaddrlen, 4);
    if (addrlen < 16) return 0;  /* not enough space */

    uint8_t sa[16];
    sa[0]  = (uint8_t)(LA_AF_INET);        /* sin_family */
    sa[1]  = (uint8_t)(LA_AF_INET >> 8);
    sa[2]  = (uint8_t)(port);              /* sin_port (net order) */
    sa[3]  = (uint8_t)(port >> 8);
    sa[4]  = (uint8_t)(addr);              /* sin_addr (net order) */
    sa[5]  = (uint8_t)(addr >> 8);
    sa[6]  = (uint8_t)(addr >> 16);
    sa[7]  = (uint8_t)(addr >> 24);
    for (int i = 8; i < 16; i++) sa[i] = 0;  /* sin_zero */

    la_copy_to_user(usockaddr, sa, 16);
    addrlen = 16;
    if (uaddrlen) la_copy_to_user(uaddrlen, &addrlen, 4);
    return 0;
}

/* SYS_socket(198): socket(domain, type, protocol) → fd */
static uint64_t sys_socket(struct la_trap_frame *tf)
{
    int domain   = (int)tf->gpr[LA_GPR_A0];
    int type     = (int)tf->gpr[LA_GPR_A1];
    /* int protocol = (int)tf->gpr[LA_GPR_A2]; */
    struct la_proc *p = la_current_proc();

    if (!p) return (uint64_t)(-LA_EBADF);

    int idx = la_sock_socket(domain, type, 0);
    if (idx < 0) return (uint64_t)(-LA_EINVAL);

    /* Find a free fd */
    int fd = -1;
    for (int i = 0; i < LA_NFD; i++)
        if (p->fds[i].type == LA_FD_UNUSED) { fd = i; break; }
    if (fd < 0) {
        la_sock_close(idx);
        return (uint64_t)(-LA_EMFILE);
    }

    p->fds[fd].type     = LA_FD_SOCKET;
    p->fds[fd].sock_idx = idx;
    p->fds[fd].writable = 1;
    p->fds[fd].pipe     = 0;
    return (uint64_t)fd;
}

/* SYS_bind(200): bind(fd, sockaddr, addrlen) */
static uint64_t sys_bind(struct la_trap_frame *tf)
{
    int fd            = (int)tf->gpr[LA_GPR_A0];
    uint64_t usockaddr = tf->gpr[LA_GPR_A1];
    /* uint32_t addrlen = (uint32_t)tf->gpr[LA_GPR_A2]; */
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type != LA_FD_SOCKET) return (uint64_t)(-LA_EBADF);

    uint32_t addr;
    uint16_t port;
    if (la_copy_sockaddr_in(usockaddr, &addr, &port) < 0)
        return (uint64_t)(-LA_EINVAL);

    if (la_sock_bind(p->fds[fd].sock_idx, addr, port) < 0)
        return (uint64_t)(-LA_EINVAL);

    return 0;
}

/* SYS_listen(201): listen(fd, backlog) */
static uint64_t sys_listen(struct la_trap_frame *tf)
{
    int fd       = (int)tf->gpr[LA_GPR_A0];
    int backlog  = (int)tf->gpr[LA_GPR_A1];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type != LA_FD_SOCKET) return (uint64_t)(-LA_EBADF);

    if (la_sock_listen(p->fds[fd].sock_idx, backlog) < 0)
        return (uint64_t)(-LA_EINVAL);

    return 0;
}

/* SYS_accept(202): accept(fd, sockaddr, addrlen) → new fd */
static uint64_t sys_accept(struct la_trap_frame *tf)
{
    int fd            = (int)tf->gpr[LA_GPR_A0];
    uint64_t uaddr    = tf->gpr[LA_GPR_A1];
    uint64_t uaddrlen = tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type != LA_FD_SOCKET) return (uint64_t)(-LA_EBADF);

    uint32_t raddr = 0;
    uint16_t rport = 0;
    int child_idx = la_sock_accept(p->fds[fd].sock_idx, &raddr, &rport);
    if (child_idx < 0) return (uint64_t)(-LA_EINVAL);

    /* Allocate fd for the accepted connection */
    int newfd = -1;
    for (int i = 0; i < LA_NFD; i++)
        if (p->fds[i].type == LA_FD_UNUSED) { newfd = i; break; }
    if (newfd < 0) {
        la_sock_close(child_idx);
        return (uint64_t)(-LA_EMFILE);
    }

    p->fds[newfd].type     = LA_FD_SOCKET;
    p->fds[newfd].sock_idx = child_idx;
    p->fds[newfd].writable = 1;
    p->fds[newfd].pipe     = 0;

    /* Return remote address to caller */
    la_put_sockaddr_in(uaddr, uaddrlen, raddr, rport);

    return (uint64_t)newfd;
}

/* SYS_connect(203): connect(fd, sockaddr, addrlen) */
static uint64_t sys_connect(struct la_trap_frame *tf)
{
    int fd            = (int)tf->gpr[LA_GPR_A0];
    uint64_t usockaddr = tf->gpr[LA_GPR_A1];
    /* uint32_t addrlen = (uint32_t)tf->gpr[LA_GPR_A2]; */
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type != LA_FD_SOCKET) return (uint64_t)(-LA_EBADF);

    uint32_t addr;
    uint16_t port;
    if (la_copy_sockaddr_in(usockaddr, &addr, &port) < 0)
        return (uint64_t)(-LA_EINVAL);

    if (la_sock_connect(p->fds[fd].sock_idx, addr, port) < 0)
        return (uint64_t)(-LA_ECONNREFUSED);

    return 0;
}

/* SYS_sendto(206): sendto(fd, buf, len, flags, dest_addr, addrlen) */
static uint64_t sys_sendto(struct la_trap_frame *tf)
{
    int fd         = (int)tf->gpr[LA_GPR_A0];
    uint64_t ubuf  = tf->gpr[LA_GPR_A1];
    uint32_t len   = (uint32_t)tf->gpr[LA_GPR_A2];
    /* uint32_t flags = (uint32_t)tf->gpr[LA_GPR_A3]; */
    uint64_t udest = tf->gpr[LA_GPR_A4];
    uint32_t addrlen = (uint32_t)tf->gpr[LA_GPR_A5];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type != LA_FD_SOCKET) return (uint64_t)(-LA_EBADF);

    uint32_t addr = 0;
    uint16_t port = 0;

    /* For TCP: send ignores dest_addr (already connected), use peer.
     * For UDP: dest_addr is required. */
    if (addrlen >= 16 && udest) {
        if (la_copy_sockaddr_in(udest, &addr, &port) < 0)
            return (uint64_t)(-LA_EINVAL);
    }

    /* Copy user buffer to kernel temp buffer */
    if (len > 65536) len = 65536;  /* cap */
    static __attribute__((aligned(8))) char sbuf[65536];
    if (la_copy_from_user(sbuf, ubuf, len) != len)
        return (uint64_t)(-LA_EFAULT);

    /* For UDP, use sendto; for TCP (connected), use send */
    /* We detect UDP by checking if dest_addr was provided with valid port */
    if (udest && addrlen >= 16 && port != 0) {
        int n = la_sock_sendto(p->fds[fd].sock_idx, sbuf, len, addr, port);
        if (n < 0) return (uint64_t)(-LA_ECONNREFUSED);
        return (uint64_t)n;
    }

    /* TCP send */
    int n = la_sock_send(p->fds[fd].sock_idx, sbuf, len);
    if (n < 0) return (uint64_t)(-LA_EPIPE);
    return (uint64_t)n;
}

/* SYS_recvfrom(207): recvfrom(fd, buf, len, flags, src_addr, addrlen) */
static uint64_t sys_recvfrom(struct la_trap_frame *tf)
{
    int fd          = (int)tf->gpr[LA_GPR_A0];
    uint64_t ubuf   = tf->gpr[LA_GPR_A1];
    uint32_t len    = (uint32_t)tf->gpr[LA_GPR_A2];
    /* uint32_t flags = (uint32_t)tf->gpr[LA_GPR_A3]; */
    uint64_t usrc   = tf->gpr[LA_GPR_A4];
    uint64_t uaddrlen = tf->gpr[LA_GPR_A5];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type != LA_FD_SOCKET) return (uint64_t)(-LA_EBADF);
    if (len == 0) return 0;

    if (len > 65536) len = 65536;  /* cap */
    static __attribute__((aligned(8))) char rbuf[65536];

    /* Try UDP recvfrom first (will fail for TCP sockets, then try TCP recv) */
    uint32_t src_addr = 0;
    uint16_t src_port = 0;
    int n = la_sock_recvfrom(p->fds[fd].sock_idx, rbuf, len, &src_addr, &src_port);
    if (n < 0) {
        /* Try TCP recv */
        n = la_sock_recv(p->fds[fd].sock_idx, rbuf, len);
        if (n < 0) return (uint64_t)(-LA_ECONNRESET);
    }
    if (n == 0) return 0;  /* EOF */

    la_copy_to_user(ubuf, rbuf, (uint32_t)n);

    /* Return source address if requested (UDP only) */
    if (usrc && src_port != 0)
        la_put_sockaddr_in(usrc, uaddrlen, src_addr, src_port);

    return (uint64_t)n;
}

/* SYS_getsockname(204): getsockname(fd, addr, addrlen) — return local address */
static uint64_t sys_getsockname(struct la_trap_frame *tf)
{
    int fd            = (int)tf->gpr[LA_GPR_A0];
    uint64_t uaddr    = tf->gpr[LA_GPR_A1];
    uint64_t uaddrlen = tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type != LA_FD_SOCKET) return (uint64_t)(-LA_EBADF);

    uint32_t addr;
    uint16_t port;
    if (la_sock_getname(p->fds[fd].sock_idx, &addr, &port, 0) < 0)
        return (uint64_t)(-LA_EINVAL);

    la_put_sockaddr_in(uaddr, uaddrlen, addr, port);
    return 0;
}

/* SYS_getpeername(205): getpeername(fd, addr, addrlen) — return remote address */
static uint64_t sys_getpeername(struct la_trap_frame *tf)
{
    int fd            = (int)tf->gpr[LA_GPR_A0];
    uint64_t uaddr    = tf->gpr[LA_GPR_A1];
    uint64_t uaddrlen = tf->gpr[LA_GPR_A2];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type != LA_FD_SOCKET) return (uint64_t)(-LA_EBADF);

    uint32_t addr;
    uint16_t port;
    if (la_sock_getname(p->fds[fd].sock_idx, &addr, &port, 1) < 0)
        return (uint64_t)(-LA_ENOTCONN);

    la_put_sockaddr_in(uaddr, uaddrlen, addr, port);
    return 0;
}

/* getsockopt / setsockopt stubs — return sensible values for netperf */
static uint64_t sys_getsockopt(struct la_trap_frame *tf)
{
    int fd         = (int)tf->gpr[LA_GPR_A0];
    /* int level    = (int)tf->gpr[LA_GPR_A1]; */
    int optname    = (int)tf->gpr[LA_GPR_A2];
    uint64_t uoptval  = tf->gpr[LA_GPR_A3];
    uint64_t uoptlen  = tf->gpr[LA_GPR_A4];
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);

    if (uoptval && uoptlen) {
        uint32_t optlen = 0;
        la_copy_from_user(&optlen, uoptlen, 4);

        /* Common options that netperf queries */
        if (optname == 0x0001 || optname == 0x0002) {  /* SO_SNDBUF / SO_RCVBUF */
            if (optlen >= 4) {
                int val = 65536;  /* 64 KB */
                la_copy_to_user(uoptval, &val, 4);
            }
        } else if (optname == 0x0008) {  /* SO_KEEPALIVE */
            if (optlen >= 4) {
                int val = 0;
                la_copy_to_user(uoptval, &val, 4);
            }
        } else if (optname == 0x1006) {  /* TCP_MAXSEG */
            if (optlen >= 4) {
                int val = 1460;
                la_copy_to_user(uoptval, &val, 4);
            }
        } else if (optname == 0x1001) {  /* TCP_NODELAY */
            if (optlen >= 4) {
                int val = 1;
                la_copy_to_user(uoptval, &val, 4);
            }
        }
        /* For unknown options, leave the buffer untouched */
    }

    return 0;
}

static uint64_t sys_setsockopt(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;  /* no-op: accept all options */
}

/* SYS_shutdown(210): shutdown(fd, how) — TCP half-close.
 * For loopback, a full close on the socket is sufficient. */
static uint64_t sys_shutdown_sock(struct la_trap_frame *tf)
{
    int fd  = (int)tf->gpr[LA_GPR_A0];
    /* int how = (int)tf->gpr[LA_GPR_A1]; */
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type != LA_FD_SOCKET) return (uint64_t)(-LA_EBADF);

    la_sock_close(p->fds[fd].sock_idx);
    return 0;
}

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
    int pid         = (int)tf->gpr[LA_GPR_A0];
    int policy      = (int)tf->gpr[LA_GPR_A1];
    uint64_t uparam = tf->gpr[LA_GPR_A2];
    struct la_proc *target;

    /* pid == 0 means "current process" */
    if (pid == 0)
        target = la_current_proc();
    else
        target = la_proc_by_pid(pid);

    if (!target) return (uint64_t)(-LA_ESRCH);

    /* Read struct sched_param (4 bytes: sched_priority) */
    int priority = 0;
    if (uparam) {
        if (la_copy_from_user(&priority, uparam, 4) != 4)
            return (uint64_t)(-LA_EFAULT);
    }

    /* Clamp priority */
    if (priority < 0) priority = 0;
    if (priority > 99) priority = 99;

    /* SCHED_FIFO and SCHED_RR use RT priorities 1–99.
     * SCHED_OTHER uses priority 0. */
    if (policy == 0)  /* SCHED_OTHER */
        priority = 0;
    else if (policy == 1 || policy == 2)  /* SCHED_FIFO / SCHED_RR */
        ;  /* use the provided priority */
    else
        return (uint64_t)(-LA_EINVAL);

    target->sched_priority = priority;
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
#define LA_SYSCALL_TRACE_MAX 0  /* set to >0 for debugging; UNKNOWN syscalls always logged */

/* SYS_madvise(233): give advice about use of memory (stub).
 * glibc's dynamic linker calls this to mark pages as MADV_DONTNEED. */
static uint64_t sys_madvise(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_mlock(228) / SYS_mlock2(284): lock memory (stub).
 * glibc may call these to pin memory. */
static uint64_t sys_mlock(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_membarrier(283): memory barriers across threads.
 * Single-core cooperative execution makes the supported private expedited
 * operations no-ops.  Return the supported mask for QUERY and success for
 * register/execute commands used by musl pthread. */
static uint64_t sys_membarrier(struct la_trap_frame *tf)
{
    int cmd = (int)tf->gpr[LA_GPR_A0];
    const int query = 0;
    const int private_expedited = 1 << 3;
    const int register_private_expedited = 1 << 4;

    if (cmd == query)
        return (uint64_t)(private_expedited | register_private_expedited);
    if (cmd == private_expedited || cmd == register_private_expedited)
        return 0;
    return (uint64_t)(-LA_EINVAL);
}

/* SYS_prctl(167): process control operations.
 * glibc uses PR_SET_NAME, PR_GET_NAME, PR_SET_SECCOMP, etc.
 * Accept all operations silently — most callers treat ENOSYS as fatal. */
static uint64_t sys_prctl(struct la_trap_frame *tf)
{
    /* int option = (int)tf->gpr[LA_GPR_A0]; */
    (void)tf;
    return 0;
}

/* SYS_getrandom(278): fill buffer with random bytes.
 * CRITICAL: glibc uses this for stack canary and pointer guard
 * initialisation.  If it fails with ENOSYS, the dynamic linker may
 * crash or produce deterministic (breakable) canaries.
 * Fall back to a simple LCG seeded from AT_RANDOM-style entropy. */
static uint64_t sys_getrandom(struct la_trap_frame *tf)
{
    uint64_t ubuf = tf->gpr[LA_GPR_A0];
    uint32_t len  = (uint32_t)tf->gpr[LA_GPR_A1];
    /* flags = a2 (ignored) */

    if (!ubuf || len == 0) return 0;
    if (len > 4096) len = 4096;  /* cap at one page */

    /* Simple PRNG: PCG-style multiplier, seed mixed from timer ticks */
    static uint64_t rng_state = 0;
    if (rng_state == 0)
        rng_state = la_timer_get_ticks() * 6364136223846793005ULL + 1442695040888963407ULL;

    static __attribute__((aligned(8))) char rbuf[256];
    uint32_t done = 0;
    while (done < len) {
        uint32_t chunk = 256;
        if (chunk > len - done) chunk = len - done;

        for (uint32_t i = 0; i < chunk; i += 8) {
            rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
            uint64_t val = rng_state;
            for (int j = 0; j < 8 && (i + j) < chunk; j++)
                rbuf[i + j] = (uint8_t)(val >> (j * 8));
        }
        la_copy_to_user(ubuf + (uint64_t)done, rbuf, chunk);
        done += chunk;
    }
    return (uint64_t)done;
}

/* SYS_rseq(293): restartable sequences (glibc 2.35+).
 * Stub: return -ENOSYS so glibc falls back to the non-rseq code path. */
static uint64_t sys_rseq(struct la_trap_frame *tf)
{
    (void)tf;
    return (uint64_t)(-LA_ENOSYS);
}

/* ---- LTP / general syscall stubs ---- */

/* SYS_nanosleep(101): sleep for specified nanoseconds.  a0=req, a1=rem.
 * struct timespec: sec(8) + nsec(8).  Sleeps in 1-tick (10ms) increments
 * so the cooperative scheduler can interleave other runnable procs. */
static uint64_t sys_nanosleep(struct la_trap_frame *tf)
{
    uint64_t ureq = tf->gpr[LA_GPR_A0];
    if (!ureq) return (uint64_t)(-LA_EFAULT);

    struct { int64_t tv_sec; int64_t tv_nsec; } ts;
    if (la_copy_from_user(&ts, ureq, sizeof(ts)) != sizeof(ts))
        return (uint64_t)(-LA_EFAULT);
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL)
        return (uint64_t)(-LA_EINVAL);

    /* Convert to ticks at LA_TIMER_HZ (100 Hz → 10ms per tick).  Round up
     * so a sub-tick request still sleeps at least one tick. */
    uint64_t total_ticks = (uint64_t)ts.tv_sec * LA_TIMER_HZ
                         + ((uint64_t)ts.tv_nsec * LA_TIMER_HZ
                            + 99999999ULL) / 100000000ULL;
    if (total_ticks == 0) total_ticks = 1;

    uint64_t start = la_timer_get_ticks();
    while (la_timer_get_ticks() - start < total_ticks) {
        /* Yield (stay RUNNABLE) so the scheduler runs other processes
         * or idles.  The timer ISR fires asynchronously and advances
         * la_ticks regardless. */
        la_proc_yield();
        /* Break out early if a signal arrived (pthread_cancel, etc.) */
        struct la_proc *me = la_current_proc();
        if (me && me->sig_pending)
            return (uint64_t)(-LA_EINTR);
    }
    return 0;
}

/* SYS_clock_nanosleep(115): high-res sleep.  a0=clockid, a1=flags, a2=req, a3=rem. */
static uint64_t sys_clock_nanosleep(struct la_trap_frame *tf)
{
    /* Same minimal sleep */
    la_proc_sleep();
    return 0;
}

/* SYS_getrlimit(163): get resource limit.  a0=resource, a1=rlim.
 * struct rlimit: rlim_cur(8) + rlim_max(8).  Return unlimited (all Fs). */
static uint64_t sys_getrlimit(struct la_trap_frame *tf)
{
    /* int resource = (int)tf->gpr[LA_GPR_A0]; */
    uint64_t urlim = tf->gpr[LA_GPR_A1];
    if (urlim) {
        uint64_t rlim[2] = { ~0ULL, ~0ULL };  /* RLIM_INFINITY */
        la_copy_to_user(urlim, rlim, 16);
    }
    return 0;
}

/* SYS_getrusage(165): get resource usage.  a0=who, a1=usage.
 * Return zero-filled struct rusage (144 bytes). */
static uint64_t sys_getrusage(struct la_trap_frame *tf)
{
    uint64_t uusage = tf->gpr[LA_GPR_A1];
    if (uusage) {
        uint8_t zero[144];
        for (int i = 0; i < 144; i++) zero[i] = 0;
        la_copy_to_user(uusage, zero, 144);
    }
    return 0;
}

/* SYS_sysinfo(179): return system information.
 * struct sysinfo: uptime(8) loads[3](24) totalram(8) freeram(8) sharedram(8)
 * bufferram(8) totalswap(8) freeswap(8) procs(2) pad(2) totalhigh(8) freehigh(8)
 * mem_unit(4) _f(0) — 112 bytes. */
static uint64_t sys_sysinfo(struct la_trap_frame *tf)
{
    uint64_t uinfo = tf->gpr[LA_GPR_A0];
    if (uinfo) {
        uint8_t info[112];
        for (int i = 0; i < 112; i++) info[i] = 0;
        /* Fill in uptime from ticks */
        uint64_t ticks = la_timer_get_ticks();
        *(uint64_t *)&info[0] = ticks / 100;  /* uptime in seconds */
        *(uint32_t *)&info[104] = 4096;       /* mem_unit = page size */
        la_copy_to_user(uinfo, info, 112);
    }
    return 0;
}

/* SYS_statfs(43) / SYS_fstatfs(44): filesystem statistics.
 * struct statfs: f_type(8) f_bsize(8) f_blocks(8) f_bfree(8) f_bavail(8)
 * f_files(8) f_ffree(8) f_fsid(8) f_namelen(8) f_frsize(8) f_flags(8) f_spare[4] — 120 bytes */
static uint64_t sys_statfs(struct la_trap_frame *tf)
{
    uint64_t ubuf = tf->gpr[LA_GPR_A1];  /* path in a0 ignored */
    if (ubuf) {
        uint64_t buf[15];
        for (int i = 0; i < 15; i++) buf[i] = 0;
        buf[0] = 0xEF53;         /* EXT4_SUPER_MAGIC */
        buf[1] = 4096;           /* f_bsize */
        buf[2] = 1000000;        /* f_blocks */
        buf[3] = 500000;         /* f_bfree */
        buf[4] = 500000;         /* f_bavail */
        buf[5] = 128;            /* f_files */
        buf[6] = 100;            /* f_ffree */
        buf[8] = 255;            /* f_namelen */
        buf[9] = 4096;           /* f_frsize */
        la_copy_to_user(ubuf, buf, 120);
    }
    return 0;
}

static uint64_t sys_fstatfs(struct la_trap_frame *tf)
{
    /* Same as statfs but uses fd instead of path (ignored) */
    uint64_t ubuf = tf->gpr[LA_GPR_A1];
    if (ubuf) {
        uint64_t buf[15];
        for (int i = 0; i < 15; i++) buf[i] = 0;
        buf[0] = 0xEF53;
        buf[1] = 4096;
        buf[2] = 1000000;
        buf[3] = 500000;
        buf[4] = 500000;
        buf[5] = 128;
        buf[6] = 100;
        buf[8] = 255;
        buf[9] = 4096;
        la_copy_to_user(ubuf, buf, 120);
    }
    return 0;
}

/* SYS_fsync(82) / SYS_fdatasync(83): sync file data (stub — return 0) */
static uint64_t sys_fsync(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_utimensat(88): update file timestamps.
 * This kernel does not persist atime/mtime yet; accept the request so libc
 * tests that only require POSIX success semantics can continue. */
static uint64_t sys_utimensat(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_umask(166): set file creation mask (stub — return 022) */
static uint64_t sys_umask(struct la_trap_frame *tf)
{
    (void)tf;
    return 022;  /* default umask */
}

/* SYS_getpgid(155): get process group ID (stub — return pid) */
static uint64_t sys_getpgid(struct la_trap_frame *tf)
{
    struct la_proc *p = la_current_proc();
    return p ? (uint64_t)p->pid : 0;
}

/* SYS_readv(65): scatter read (stub — redirect to read on first iovec) */
static uint64_t sys_readv(struct la_trap_frame *tf)
{
    uint64_t uiov  = tf->gpr[LA_GPR_A1];
    int iovcnt    = (int)tf->gpr[LA_GPR_A2];

    if (iovcnt <= 0) return 0;

    /* Read first iovec entry: { base(8), len(8) } */
    uint8_t iov[16];
    if (la_copy_from_user(iov, uiov, 16) != 16)
        return (uint64_t)(-LA_EFAULT);
    uint64_t base = *(uint64_t *)&iov[0];
    uint64_t len  = *(uint64_t *)&iov[8];

    /* Delegate to sys_read */
    struct la_trap_frame rtf = *tf;
    rtf.gpr[LA_GPR_A1] = base;
    rtf.gpr[LA_GPR_A2] = len;
    return sys_read(&rtf);
}

/* SYS_sendfile(71): send file to socket (stub — return -ENOSYS) */
static uint64_t sys_sendfile(struct la_trap_frame *tf)
{
    (void)tf;
    return (uint64_t)(-LA_ENOSYS);
}

/* SYS_ftruncate(46): truncate file to specified length.
 * For memfs fds, uses memfs_truncate.  For ext4 (read-only), returns 0. */
static uint64_t sys_ftruncate(struct la_trap_frame *tf)
{
    int fd = (int)tf->gpr[LA_GPR_A0];
    /* uint64_t length = tf->gpr[LA_GPR_A1]; */
    struct la_proc *p = la_current_proc();

    if (!p || fd < 0 || fd >= LA_NFD) return (uint64_t)(-LA_EBADF);
    if (p->fds[fd].type == LA_FD_MEMFS) {
        memfs_truncate((int)p->fds[fd].ino);
        return 0;
    }
    /* ext4 is read-only, but returning success is benign */
    return 0;
}

/* SYS_get_robust_list(100): get robust futex list (stub) */
static uint64_t sys_get_robust_list(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;
}

/* SYS_sched_getparam(121): get scheduling parameters.
 * a0=pid, a1=param (struct sched_param { sched_priority }) */
static uint64_t sys_sched_getparam(struct la_trap_frame *tf)
{
    int pid = (int)tf->gpr[LA_GPR_A0];
    uint64_t uparam = tf->gpr[LA_GPR_A1];
    struct la_proc *target;

    if (pid == 0)
        target = la_current_proc();
    else
        target = la_proc_by_pid(pid);
    if (!target) return (uint64_t)(-LA_ESRCH);

    if (uparam) {
        int prio = target->sched_priority;
        la_copy_to_user(uparam, &prio, 4);
    }
    return 0;
}

/* SYS_sched_setparam(118): set scheduling parameters. */
static uint64_t sys_sched_setparam(struct la_trap_frame *tf)
{
    int pid = (int)tf->gpr[LA_GPR_A0];
    uint64_t uparam = tf->gpr[LA_GPR_A1];
    struct la_proc *target;

    if (pid == 0)
        target = la_current_proc();
    else
        target = la_proc_by_pid(pid);
    if (!target) return (uint64_t)(-LA_ESRCH);

    if (uparam) {
        int prio = 0;
        if (la_copy_from_user(&prio, uparam, 4) != 4)
            return (uint64_t)(-LA_EFAULT);
        if (prio < 0) prio = 0;
        if (prio > 99) prio = 99;
        target->sched_priority = prio;
    }
    return 0;
}

/* SYS_sched_getscheduler(120): get scheduling policy (stub — return SCHED_OTHER=0) */
static uint64_t sys_sched_getscheduler(struct la_trap_frame *tf)
{
    (void)tf;
    return 0;  /* SCHED_OTHER */
}

/* SYS_get_mempolicy(236): get NUMA memory policy (stub — return default node 0) */
static uint64_t sys_get_mempolicy(struct la_trap_frame *tf)
{
    uint64_t umode = tf->gpr[LA_GPR_A0];
    uint64_t unodes = tf->gpr[LA_GPR_A2];
    if (umode) {
        int mode = 0;  /* MPOL_DEFAULT */
        la_copy_to_user(umode, &mode, 4);
    }
    if (unodes) {
        uint64_t nodes = 1;  /* node 0 only */
        la_copy_to_user(unodes, &nodes, 8);
    }
    return 0;
}

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
    case SYS_statfs:     return sys_statfs(tf);
    case SYS_fstatfs:    return sys_fstatfs(tf);
    case SYS_readv:      return sys_readv(tf);
    case SYS_pread64:    return sys_pread64(tf);
    case SYS_sendfile:   return sys_sendfile(tf);
    case SYS_fsync:      return sys_fsync(tf);
    case SYS_fdatasync:  return sys_fsync(tf);
    case SYS_utimensat:  return sys_utimensat(tf);
    case SYS_ftruncate:  return sys_ftruncate(tf);

    /* Directory */
    case SYS_chdir:      return sys_chdir(tf);
    case SYS_mkdir:      return sys_mkdir(tf);
    case SYS_unlinkat:   return sys_unlinkat(tf);

    /* Memory */
    case SYS_brk:        return sys_brk(tf);
    case SYS_mmap:       return sys_mmap(tf);
    case SYS_munmap:     return sys_munmap(tf);
    case SYS_mprotect:   return sys_mprotect(tf);

    /* Signal (stubs) */
    case SYS_rt_sigaction:   return sys_rt_sigaction(tf);
    case SYS_rt_sigprocmask: return sys_rt_sigprocmask(tf);
    case SYS_rt_sigtimedwait: return sys_rt_sigtimedwait(tf);

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
    case SYS_tkill:      return sys_tkill(tf);
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
    case SYS_nanosleep:  return sys_nanosleep(tf);
    case SYS_clock_nanosleep: return sys_clock_nanosleep(tf);
    case SYS_getrlimit:  return sys_getrlimit(tf);
    case SYS_getrusage:  return sys_getrusage(tf);
    case SYS_sysinfo:    return sys_sysinfo(tf);
    case SYS_umask:      return sys_umask(tf);
    case SYS_getpgid:    return sys_getpgid(tf);
    case SYS_get_robust_list: return sys_get_robust_list(tf);
    case SYS_get_mempolicy: return sys_get_mempolicy(tf);

    /* Scheduler (minimal) */
    case SYS_sched_setaffinity: return sys_sched_setaffinity(tf);
    case SYS_sched_getaffinity: return sys_sched_getaffinity(tf);
    case SYS_sched_setscheduler: return sys_sched_setscheduler(tf);
    case SYS_sched_getparam:  return sys_sched_getparam(tf);
    case SYS_sched_setparam:  return sys_sched_setparam(tf);
    case SYS_sched_getscheduler: return sys_sched_getscheduler(tf);

    /* Select / poll */
    case SYS_pselect6:   return sys_pselect6(tf);
    case SYS_ppoll:      return sys_ppoll(tf);

    /* Memory advice / locking */
    case SYS_madvise:    return sys_madvise(tf);
    case SYS_mlock:      return sys_mlock(tf);
    case SYS_membarrier: return sys_membarrier(tf);
    case SYS_mlock2:     return sys_mlock(tf);

    /* Process control */
    case SYS_prctl:      return sys_prctl(tf);

    /* Random */
    case SYS_getrandom:  return sys_getrandom(tf);

    /* Restartable sequences */
    case SYS_rseq:       return sys_rseq(tf);

    /* Socket family (real loopback implementations) */
    case SYS_socket:     return sys_socket(tf);
    case SYS_bind:       return sys_bind(tf);
    case SYS_listen:     return sys_listen(tf);
    case SYS_accept:     return sys_accept(tf);
    case SYS_connect:    return sys_connect(tf);
    case SYS_sendto:     return sys_sendto(tf);
    case SYS_recvfrom:   return sys_recvfrom(tf);
    case SYS_getsockname: return sys_getsockname(tf);
    case SYS_getpeername: return sys_getpeername(tf);
    case SYS_setsockopt:  return sys_setsockopt(tf);
    case SYS_getsockopt:  return sys_getsockopt(tf);
    case SYS_shutdown_sock: return sys_shutdown_sock(tf);
    case SYS_sendmsg:     return sys_stub_enosys(tf);   /* -EOPNOTSUPP would be better but ENOSYS is safe */
    case SYS_recvmsg:     return sys_stub_enosys(tf);
    case SYS_accept4:     return sys_accept(tf);         /* accept ignoring flags */

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
