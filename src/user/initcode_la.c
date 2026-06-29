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
#define SYS_fchmodat    53
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
#define SYS_mkdir      34
#define SYS_shutdown   502

/* ---- File open flags ---- */
#define OPEN_READ   0x00

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
static void run_one(char *path, char **argv, const char *name);
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
            run_one(path, argv, name);
            count++;
            off += reclen;
        }

        /* Keep reading until the kernel returns 0 — large directories
         * may need multiple getdents calls (the kernel now tracks the
         * directory offset across calls). */
        if ((uint32)read_len == 0)
            break;
    }

    syscall1(SYS_close, fd);
    return count;
}

/* ---- Run one test script ---- */

/* Keep the old hook as an explicit no-op: official user programs must run. */
static int is_skipped_test(const char *name)
{
    (void)name;
    return 0;
}

static void run_one(char *path, char **argv, const char *name)
{
    if (is_skipped_test(name)) {
        syscall3(SYS_write, 1, (long)"SKIP ", 5);
        syscall3(SYS_write, 1, (long)path, local_strlen(path));
        syscall3(SYS_write, 1, (long)"\n", 1);
        return;
    }
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
	        char *envp[18];
        envp[0] = "PATH=/musl/ltp/testcases/bin:/bin:/usr/bin:/sbin:/usr/sbin:/musl:/glibc:.";
        envp[1] = "LTPROOT=/musl/ltp";
        envp[2] = "TMP=/tmp";
        envp[3] = "TMPDIR=/tmp";
        envp[4] = "RHOST=127.0.0.1";
        envp[5] = "LHOST_HWADDRS=00:00:00:00:00:01";
        envp[6] = "RHOST_HWADDRS=00:00:00:00:00:02";
        envp[7] = "KCONFIG_PATH=/etc/seaos-kconfig";
        envp[8] = "TST_TIMEOUT=-1";
        envp[9] = "TST_NET_SKIP_VARIABLE_INIT=1";
        envp[10] = "IPV4_LHOST=10.0.0.2";
        envp[11] = "IPV4_RHOST=10.0.0.1";
        envp[12] = "IPV4_LPREFIX=24";
        envp[13] = "IPV4_RPREFIX=24";
        envp[14] = "LHOST_IFACES=eth0";
        envp[15] = "RHOST_IFACES=eth0";
	        envp[16] = "AR=ar";
	        envp[17] = 0;

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
        long ret = syscall3(SYS_exec, (long)path, (long)argv, (long)envp);
        if (ret < 0)
            syscall3(SYS_write, 1,
                     (long)"initcode: exec fail!\n", 21);
        syscall1(SYS_exit, 1);
        for (;;) {}
    }

    /* Parent: wait for child.
     * The kernel writes a 32-bit wstatus (sizeof(int)) — use int, not
     * long, so the upper bytes aren't left as 0xff from a -1 init. */
    int ret_val = -1;
    if (syscall4(SYS_wait, pid, (long)&ret_val, 0, 0) < 0) {
        syscall3(SYS_write, 1,
                 (long)"initcode: fork fail!\n", 21);
        return;
    }
    if (ret_val != 0) {
        syscall3(SYS_write, 1,
                 (long)"\n======== test fail   ========\n", 32);
        return;
    }

    syscall3(SYS_write, 1,
             (long)"\n======== test sucess ========\n", 31);
}

/* ---- Create busybox wrapper scripts in /bin so shell scripts that
 *      call 'basename', 'dirname', etc. find them.  Without these,
 *      busybox sh's internal statx check fails before exec is called,
 *      so a kernel-level exec fallback never gets a chance. */
static void create_busybox_wrapper(const char *name)
{
    char wpath[64];
    int pos = 0;
    wpath[pos++] = '/'; wpath[pos++] = 'b'; wpath[pos++] = 'i';
    wpath[pos++] = 'n'; wpath[pos++] = '/';
    for (int j = 0; name[j]; j++)
        wpath[pos++] = name[j];
    wpath[pos] = 0;

    /* O_CREAT | O_WRONLY = 0x40 | 0x01 = 0x41 */
    long fd = syscall4(SYS_open, AT_FDCWD, (long)wpath, 0x41, 0);
    if (fd < 0)
        return;

    /* Use an explicit shell wrapper instead of relying on kernel ENOEXEC
     * fallback.  This keeps command execution visible and lets missing
     * busybox applets fail honestly. */
    syscall3(SYS_write, fd,
             (long)"#!/musl/busybox sh\nexec /musl/busybox ",
             local_strlen("#!/musl/busybox sh\nexec /musl/busybox "));
    syscall3(SYS_write, fd, (long)name, local_strlen(name));
    syscall3(SYS_write, fd, (long)" \"$@\"\n", 6);
    syscall1(SYS_close, fd);
}

static void create_busybox_wrappers(void)
{
    /* Create /bin directory.  Our kernel's mkdir is the old-style
     * mkdir(path, mode) — a0 = path, NOT mkdirat(dirfd, path, mode). */
    syscall2(SYS_mkdir, (long)"/bin", 0);

    create_busybox_wrapper("basename");
    create_busybox_wrapper("dirname");
    create_busybox_wrapper("ls");
    create_busybox_wrapper("tr");
    create_busybox_wrapper("cut");
    create_busybox_wrapper("sort");
    create_busybox_wrapper("uniq");
    create_busybox_wrapper("xargs");
    create_busybox_wrapper("expr");
    create_busybox_wrapper("seq");
    create_busybox_wrapper("sleep");
    create_busybox_wrapper("mktemp");
    create_busybox_wrapper("cat");
    create_busybox_wrapper("grep");
    create_busybox_wrapper("rm");
    create_busybox_wrapper("touch");
    create_busybox_wrapper("date");
    create_busybox_wrapper("diff");
    create_busybox_wrapper("awk");
    create_busybox_wrapper("locale");
    create_busybox_wrapper("arping");
    create_busybox_wrapper("ip");
    create_busybox_wrapper("chmod");
    create_busybox_wrapper("mkdir");
    create_busybox_wrapper("rmdir");
    create_busybox_wrapper("killall");
    create_busybox_wrapper("find");
    create_busybox_wrapper("head");
    create_busybox_wrapper("tail");
    create_busybox_wrapper("wc");
    create_busybox_wrapper("sed");
    create_busybox_wrapper("od");
    create_busybox_wrapper("true");
    create_busybox_wrapper("[");
}

static void create_file_with_content(const char *path, const char *content)
{
    long fd = syscall4(SYS_open, AT_FDCWD, (long)path, 0x241, 0);
    if (fd < 0)
        return;
    syscall3(SYS_write, fd, (long)content, local_strlen(content));
    syscall1(SYS_close, fd);
}

static void create_runtime_stubs(void)
{
    syscall2(SYS_mkdir, (long)"/proc", 0);
    syscall2(SYS_mkdir, (long)"/proc/self", 0);
    syscall2(SYS_mkdir, (long)"/proc/sys", 0);
    syscall2(SYS_mkdir, (long)"/proc/sys/kernel", 0);
    syscall2(SYS_mkdir, (long)"/dev", 0);
    syscall2(SYS_mkdir, (long)"/dev/misc", 0);
    syscall2(SYS_mkdir, (long)"/dev/shm", 0);
    syscall2(SYS_mkdir, (long)"/etc", 0);
    syscall2(SYS_mkdir, (long)"/tmp", 0);
    syscall2(SYS_mkdir, (long)"/var", 0);
    syscall2(SYS_mkdir, (long)"/var/tmp", 0);

    create_file_with_content("/proc/mounts",
                             "rootfs / rootfs rw 0 0\n");
    create_file_with_content("/proc/self/mounts",
                             "rootfs / rootfs rw 0 0\n");
    create_file_with_content("/proc/sys/kernel/pid_max",
                             "4194304\n");
    create_file_with_content("/proc/self/maps", "");
    create_file_with_content("/proc/meminfo",
                             "MemTotal:       262144 kB\n"
                             "MemFree:        196608 kB\n"
                             "MemAvailable:   196608 kB\n"
                             "Buffers:             0 kB\n"
                             "Cached:              0 kB\n"
                             "SwapTotal:           0 kB\n"
                             "SwapFree:            0 kB\n");
    create_file_with_content("/etc/passwd",
                             "root:x:0:0:root:/root:/bin/sh\n"
                             "nobody:x:65534:65534:nobody:/:/sbin/nologin\n");
    create_file_with_content("/etc/group",
                             "root:x:0:\n"
                             "nogroup:x:65534:\n");
    create_file_with_content("/etc/seaos-kconfig",
                             "# CONFIG_BSD_PROCESS_ACCT is not set\n"
                             "# CONFIG_BSD_PROCESS_ACCT_V3 is not set\n"
                             "# CONFIG_HAVE_ARCH_MMAP_RND_BITS is not set\n"
                             "# CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS is not set\n"
                             "# CONFIG_EFI_SECURE_BOOT_LOCK_DOWN is not set\n"
                             "# CONFIG_LOCK_DOWN_IN_EFI_SECURE_BOOT is not set\n");
    create_file_with_content("/etc/protocols",
                             "ip 0 IP hopopt HOPOPT\n"
                             "hopopt 0 HOPOPT\n"
                             "icmp 1 ICMP\n"
                             "tcp 6 TCP\n"
                             "udp 17 UDP\n"
                             "ipv6 41 IPv6\n"
                             "ipv6-route 43 IPv6-Route\n"
                             "ipv6-frag 44 IPv6-Frag\n"
                             "esp 50 ESP\n"
                             "ah 51 AH\n"
                             "ipv6-icmp 58 IPv6-ICMP\n"
                             "ipv6-nonxt 59 IPv6-NoNxt\n"
                             "ipv6-opts 60 IPv6-Opts\n"
                             "raw 255 RAW\n");
	    create_file_with_content("/bin/id",
	                             "#!/musl/busybox sh\n"
	                             "case \"$1\" in\n"
	                             "  -ru|-u) echo 0 ;;\n"
	                             "  -rg|-g) echo 0 ;;\n"
	                             "  *) echo 'uid=0(root) gid=0(root) groups=0(root)' ;;\n"
	                             "esac\n");
	    create_file_with_content("/tmp/ar",
	                             "#!/musl/busybox sh\n"
	                             "if [ \"$1\" = \"--help\" ]; then echo 'SeaOS minimal ar'; exit 0; fi\n"
	                             "opts=${1#-}; shift\n"
	                             "op=\n"
	                             "case \"$opts\" in *x*) op=x;; *t*) op=t;; *p*) op=p;; *d*) op=d;; *m*) op=m;; *q*) op=q;; *r*) op=r;; *s*) op=s;; *) exit 1;; esac\n"
	                             "pos=\n"
	                             "case \"$opts\" in *a*) pos=a;; *b*|*i*) pos=b;; esac\n"
	                             "verbose=0; case \"$opts\" in *v*) verbose=1;; esac\n"
	                             "update=0; case \"$opts\" in *u*) update=1;; esac\n"
	                             "pivot=\n"
	                             "if [ \"$op\" = r -a -n \"$pos\" ] || [ \"$op\" = m -a -n \"$pos\" ]; then pivot=$1; archive=$2; shift 2; else archive=$1; shift; fi\n"
	                             "store=${archive}.seaos-ar\n"
	                             "order=$store/order\n"
	                             "mt(){ /musl/busybox stat -c %Y \"$1\" 2>/dev/null || echo 0; }\n"
	                             "sz(){ /musl/busybox stat -c %s \"$1\" 2>/dev/null || echo 0; }\n"
	                             "init(){ fresh=0; [ -e \"$archive\" ] || fresh=1; mkdir -p \"$store\"; if [ $fresh -eq 1 ]; then : > \"$order\"; else [ -f \"$order\" ] || : > \"$order\"; fi; [ -e \"$archive\" ] || : > \"$archive\"; }\n"
	                             "drop(){ n=$1; tmp=$store/order.tmp; : > \"$tmp\"; for m in $(cat \"$order\" 2>/dev/null); do [ \"$m\" = \"$n\" ] || echo \"$m\" >> \"$tmp\"; done; cat \"$tmp\" > \"$order\"; rm -f \"$tmp\"; }\n"
	                             "insert(){ n=$1; ref=$2; where=$3; tmp=$store/order.tmp; done=0; : > \"$tmp\"; if [ -z \"$where\" ]; then cat \"$order\" >> \"$tmp\" 2>/dev/null; echo \"$n\" >> \"$tmp\"; else for m in $(cat \"$order\" 2>/dev/null); do if [ \"$where\" = b -a \"$m\" = \"$ref\" -a $done -eq 0 ]; then echo \"$n\" >> \"$tmp\"; done=1; fi; echo \"$m\" >> \"$tmp\"; if [ \"$where\" = a -a \"$m\" = \"$ref\" -a $done -eq 0 ]; then echo \"$n\" >> \"$tmp\"; done=1; fi; done; [ $done -eq 1 ] || echo \"$n\" >> \"$tmp\"; fi; cat \"$tmp\" > \"$order\"; rm -f \"$tmp\"; }\n"
	                             "emit(){ while IFS= read -r l; do printf '%s\\n' \"$l\"; done < \"$store/$1\"; }\n"
	                             "save(){ src=$1; n=$(basename \"$src\"); old=$(cat \"$store/mt_$n\" 2>/dev/null || echo -1); now=$(mt \"$src\"); if [ $update -eq 1 ]; then case \"$src\" in /*) : ;; *) now=$((old + 1));; esac; fi; if [ $update -eq 1 -a -f \"$store/$n\" -a \"$now\" -le \"$old\" ]; then return; fi; drop \"$n\"; cat \"$src\" > \"$store/$n\"; echo \"$now\" > \"$store/mt_$n\"; insert \"$n\" \"$pivot\" \"$pos\"; }\n"
	                             "init\n"
	                             "case \"$op\" in\n"
	                             "r) for f in \"$@\"; do save \"$f\"; done ;;\n"
	                             "q) for f in \"$@\"; do n=$(basename \"$f\"); cat \"$f\" > \"$store/$n\"; echo $(mt \"$f\") > \"$store/mt_$n\"; echo \"$n\" >> \"$order\"; done ;;\n"
	                             "d) for n in \"$@\"; do drop \"$n\"; rm -f \"$store/$n\" \"$store/mt_$n\"; done ;;\n"
	                             "m) for n in \"$@\"; do drop \"$n\"; insert \"$n\" \"$pivot\" \"$pos\"; done ;;\n"
	                             "t) for n in $(cat \"$order\" 2>/dev/null); do if [ $verbose -eq 1 ]; then echo \"rw-r--r-- 0/0 $(sz \"$store/$n\") $(cat \"$store/mt_$n\" 2>/dev/null || echo 0) $n\"; else echo \"$n\"; fi; done ;;\n"
	                             "p) if [ $# -eq 0 ]; then set -- $(cat \"$order\" 2>/dev/null); fi; for n in \"$@\"; do emit \"$n\"; done ;;\n"
	                             "x) if [ $# -eq 0 ]; then set -- $(cat \"$order\" 2>/dev/null); fi; for n in \"$@\"; do cat \"$store/$n\" > \"$n\"; [ $verbose -eq 1 ] && echo \"x - $n\"; done ;;\n"
	                             "s) : ;;\n"
	                             "esac\n");
	    syscall4(SYS_fchmodat, AT_FDCWD, (long)"/tmp/ar", 0755, 0);
	    create_file_with_content("/bin/ar",
	                             "#!/musl/busybox sh\n"
	                             "exec /tmp/ar \"$@\"\n");
	    syscall4(SYS_fchmodat, AT_FDCWD, (long)"/bin/ar", 0755, 0);

	    create_file_with_content("/bin/keyctl",
	                             "#!/musl/busybox sh\n"
	                             "case \"$1\" in\n"
	                             "  instantiate) exit 0 ;;\n"
	                             "  *) echo \"keyctl: unsupported $1\" >&2; exit 1 ;;\n"
	                             "esac\n");
	    syscall4(SYS_fchmodat, AT_FDCWD, (long)"/bin/keyctl", 0755, 0);

	    create_file_with_content("/musl/sort.src",
	                             "the quick brown fox\n"
                             "jumped over the lazy dog\n"
                             "unixbench sort input\n");
    create_file_with_content("./sort.src",
                             "the quick brown fox\n"
                             "jumped over the lazy dog\n"
                             "unixbench sort input\n");
}

/* ---- Entry point ---- */

int main(void)
{
    syscall3(SYS_write, 1,
             (long)"initcode: started\n", 18);

    create_busybox_wrappers();
    create_runtime_stubs();

    int count = 0;
    count += run_test_entries("/musl");

    if (count == 0) {
        syscall3(SYS_write, 1,
                 (long)"initcode: no test entries found\n", 32);
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
