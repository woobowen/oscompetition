/*
 * LoongArch initcode — the first user-space process.
 *
 * Scans the filesystem for *_testcode.sh scripts and executes them.
 * Matches the functionality of the RISC-V initcode.c.
 *
 * Self-contained: no external headers, all helpers are static.
 * Uses LoongArch "syscall 0" instruction for system calls.
 *
 * Syscall numbers match the RISC-V / SeaOS ABI.
 */

typedef unsigned short     uint16;
typedef unsigned int      uint32;
typedef unsigned long long uint64;

/* ---- Syscall numbers ---- */
#define SYS_fork         4
#define SYS_chdir       49
#define SYS_open        56
#define SYS_close       57
#define SYS_get_dentries 61
#define SYS_read        63
#define SYS_write       64
#define SYS_lseek       62
#define SYS_dup         23
#define SYS_exit        93
#define SYS_getpid     172
#define SYS_exec       221
#define SYS_wait       260
#define SYS_kill        129
#define SYS_clock_gettime 113
#define SYS_nanosleep   101
#define SYS_shutdown   502

/* ---- File open flags ---- */
#define OPEN_READ   0x02

/* openat() dirfd sentinel (asm-generic: syscall 56 is openat, not open).
 * initcode must pass AT_FDCWD in a0 and the path in a1 to match musl. */
#define AT_FDCWD    (-100)

/* ---- LoongArch syscall wrappers ---- */

static inline long syscall0(long n)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0");
    __asm__ __volatile__("syscall 0"
                         : "=r"(a0)
                         : "r"(a7)
                         : "memory");
    return a0;
}

static inline long syscall1(long n, long a)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    __asm__ __volatile__("syscall 0"
                         : "+&r"(a0)
                         : "r"(a7)
                         : "memory");
    return a0;
}

static inline long syscall2(long n, long a, long b)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    __asm__ __volatile__("syscall 0"
                         : "+&r"(a0)
                         : "r"(a7), "r"(a1)
                         : "memory");
    return a0;
}

static inline long syscall3(long n, long a, long b, long c)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    __asm__ __volatile__("syscall 0"
                         : "+&r"(a0)
                         : "r"(a7), "r"(a1), "r"(a2)
                         : "memory");
    return a0;
}

static inline long syscall4(long n, long a, long b, long c, long d)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    __asm__ __volatile__("syscall 0"
                         : "+&r"(a0)
                         : "r"(a7), "r"(a1), "r"(a2), "r"(a3)
                         : "memory");
    return a0;
}

/* ---- Helper functions ---- */

static int local_strlen(const char *str)
{
    int len = 0;
    while (str[len] != 0)
        len++;
    return len;
}

static int local_strncmp(const char *lhs, const char *rhs, uint32 n)
{
    while (n > 0 && *lhs != 0 && *lhs == *rhs) {
        n--;
        lhs++;
        rhs++;
    }
    if (n == 0)
        return 0;
    return (unsigned char)*lhs - (unsigned char)*rhs;
}

static int is_testcode_name(const char *name)
{
    int len = local_strlen(name);
    const char *suffix = "_testcode.sh";
    int suffix_len = 12;
    if (len < suffix_len)
        return 0;
    return local_strncmp(name + len - suffix_len, suffix, suffix_len) == 0;
}

static void build_path(char *dst, const char *dir, const char *name)
{
    int pos = 0;
    for (int i = 0; dir[i] != 0; i++)
        dst[pos++] = dir[i];
    if (pos == 0 || dst[pos - 1] != '/')
        dst[pos++] = '/';
    for (int i = 0; name[i] != 0; i++)
        dst[pos++] = name[i];
    dst[pos] = 0;
}

static void build_argv0(char *dst, const char *name)
{
    int pos = 0;
    while (name[pos] != 0 && name[pos] != '.') {
        dst[pos] = name[pos];
        pos++;
    }
    dst[pos] = 0;
}

/* Forward declarations */
static void run_one(char *path, char **argv);
static int run_test_entries(const char *dir);

/* ---- Directory scan ---- */

#define MAXLEN_STR 127

static int run_test_entries(const char *dir)
{
    /* openat(AT_FDCWD, dir, OPEN_READ, 0) — asm-generic ABI for syscall 56. */
    long fd = syscall4(SYS_open, AT_FDCWD, (long)dir, OPEN_READ, 0);
    if (fd < 0)
        return 0;

    int count = 0;
    char de_buf[1024];
    while (1) {
        long read_len = syscall3(SYS_get_dentries, fd, (long)de_buf, sizeof(de_buf));
        if (read_len <= 0)
            break;

        for (uint32 off = 0; off + 19 <= (uint32)read_len; ) {
            /* Linux dirent64 format:
             *   uint64 d_ino     (off+0)
             *   uint64 d_off     (off+8)
             *   uint16 d_reclen  (off+16)
             *   uint8  d_type    (off+18)
             *   char   d_name[]  (off+19)
             */
            uint16 reclen = *(uint16 *)(de_buf + off + 16);
            char *name = de_buf + off + 19;
            if (reclen < 20 || off + reclen > (uint32)read_len)
                break;
            if (!is_testcode_name(name)) {
                off += reclen;
                continue;
            }
            char path[MAXLEN_STR + 1];
            char argv0[MAXLEN_STR + 1];
            char *argv[2];
            build_path(path, dir, name);
            build_argv0(argv0, name);
            argv[0] = argv0;
            argv[1] = 0;
            run_one(path, argv);
            count++;
            off += reclen;
        }

        if ((uint32)read_len < sizeof(de_buf))
            break;
    }

    syscall1(SYS_close, fd);
    return count;
}

/* ---- Run one test script ---- */

static void run_one(char *path, char **argv)
{
    syscall3(SYS_write, 1, (long)"run ", 4);
    syscall3(SYS_write, 1, (long)path, local_strlen(path));
    for (int i = 0; argv[i] != 0; i++) {
        syscall3(SYS_write, 1, (long)" ", 1);
        syscall3(SYS_write, 1, (long)argv[i], local_strlen(argv[i]));
    }
    syscall3(SYS_write, 1, (long)"\n", 1);
    syscall3(SYS_write, 1,
             (long)"\n======== test start  ========\n\n",
             32);

    long pid = syscall0(SYS_fork);
    if (pid < 0) {
        syscall3(SYS_write, 1,
                 (long)"initcode: fork fail!\n", 21);
        return;
    }
    if (pid == 0) {
        /* Child: chdir to script directory */
        char dir[MAXLEN_STR + 1];
        int last = -1;
        for (int i = 0; path[i]; i++)
            if (path[i] == '/') last = i;
        if (last == 0) {
            dir[0] = '/'; dir[1] = 0;
            syscall1(SYS_chdir, (long)dir);
        } else if (last > 0) {
            for (int i = 0; i < last; i++) dir[i] = path[i];
            dir[last] = 0;
            syscall1(SYS_chdir, (long)dir);
        }
        long ret = syscall3(SYS_exec, (long)path, (long)argv, 0);
        if (ret < 0)
            syscall3(SYS_write, 1,
                     (long)"initcode: exec fail!\n", 21);
        syscall1(SYS_exit, 1);
        for (;;) {}
    }

    /* Parent: wait for child */
    long ret = -1;
    if (syscall4(SYS_wait, pid, (long)&ret, 0, 0) < 0) {
        syscall3(SYS_write, 1,
                 (long)"initcode: fork fail!\n", 21);
        return;
    }
    if (ret != 0) {
        syscall3(SYS_write, 1,
                 (long)"\n======== test fail   ========\n", 32);
        return;
    }

    syscall3(SYS_write, 1,
             (long)"\n======== test sucess ========\n", 31);
}

/* ---- Entry point ---- */

int main(void)
{
    syscall3(SYS_write, 1,
             (long)"initcode: started\n", 18);

    int count = 0;

    count += run_test_entries("/musl");
    count += run_test_entries("/glibc");

    if (count == 0)
        count += run_test_entries("/");

    if (count == 0) {
        syscall3(SYS_write, 1,
                 (long)"initcode: no *_testcode.sh found\n", 33);
        for (;;) {}
    }

    syscall0(SYS_shutdown);
    for (;;) {}
    return 0;
}

/* ---- _start (LoongArch) ---- */
__asm__(
    ".section .text\n"
    ".globl _start\n"
    "_start:\n"
    "  bl main\n"
    "1:\n"
    "  b 1b\n"
);
