#include "help.h"
#include "sys.h"

int main();

__asm__(".section .text\n"
	".globl _start\n"
	"_start:\n"
	"  call main\n"
	"1: j 1b\n");

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
	return (uint8)*lhs - (uint8)*rhs;
}

static int is_testcode_name(const char *name)
{
	int len = local_strlen(name);
	char suffix[] = "_testcode.sh";
	int suffix_len = sizeof(suffix) - 1;
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

static void run_one(char *path, char **argv);

static int run_test_entries(const char *dir)
{
	uint32 fd = syscall(SYS_open, dir, OPEN_READ);
	if ((int)fd < 0)
		return 0;

	int count = 0;
	char de_buf[1024];
	while (1) {
		uint32 read_len = syscall(SYS_get_dentries, fd, de_buf, sizeof(de_buf));
		if ((int)read_len <= 0)
			break;

		for (uint32 off = 0; off + 19 <= read_len; ) {
			uint16 reclen = *(uint16 *)(de_buf + off + 16);
			char *name = de_buf + off + 19;
			if (reclen < 20 || off + reclen > read_len)
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

		if (read_len < sizeof(de_buf))
			break;
	}

	syscall(SYS_close, fd);
	return count;
}

static void run_one(char *path, char **argv)
{
	char str_1[] = "initcode: fork fail!\n";
	char str_2[] = "\n======== test start  ========\n\n";
	char str_3[] = "\n======== test end    ========\n";
	char str_4[] = "\n======== test fail   ========\n";
	char str_5[] = "initcode: exec fail!\n";

	syscall(SYS_write, 1, "run ", 4);
	syscall(SYS_write, 1, path, local_strlen(path));
	for (int i = 0; argv[i] != 0; i++) {
		syscall(SYS_write, 1, " ", 1);
		syscall(SYS_write, 1, argv[i], local_strlen(argv[i]));
	}
	syscall(SYS_write, 1, "\n", 1);
	syscall(SYS_write, 1, str_2, sizeof(str_2));

	int pid = (int)syscall(SYS_fork);
	if (pid < 0) {
		syscall(SYS_write, 1, str_1, sizeof(str_1));
		return;
	}
	if (pid == 0) {
		// chdir 到脚本所在目录, 使脚本内相对路径(如 ./busybox)能解析
		char dir[MAXLEN_STR + 1];
		int last = -1;
		for (int i = 0; path[i]; i++)
			if (path[i] == '/') last = i;
		if (last == 0) {
			dir[0] = '/'; dir[1] = 0;
			syscall(SYS_chdir, dir);
		} else if (last > 0) {
			for (int i = 0; i < last; i++) dir[i] = path[i];
			dir[last] = 0;
			syscall(SYS_chdir, dir);
		}
		int ret = (int)syscall(SYS_exec, path, argv, 0);
		if (ret < 0)
			syscall(SYS_write, 1, str_5, sizeof(str_5));
		syscall(SYS_exit, 1);
		while (1) ;
	}

	int ret = -1;
	if ((int)syscall(SYS_wait, pid, &ret, 0, 0) < 0) {
		syscall(SYS_write, 1, str_1, sizeof(str_1));
		return;
	}
	if (ret != 0) {
		syscall(SYS_write, 1, str_4, sizeof(str_4));
		return;
	}

	syscall(SYS_write, 1, str_3, sizeof(str_3));
}

int main()
{
	char banner[] = "initcode: started\n";
	char no_test[] = "initcode: no *_testcode.sh found\n";
	syscall(SYS_write, 1, banner, sizeof(banner) - 1);

	int count = 0;

	count += run_test_entries("/musl");
	count += run_test_entries("/glibc");

	if (count == 0)
		count += run_test_entries("/");

	if (count == 0) {
		syscall(SYS_write, 1, no_test, sizeof(no_test) - 1);
		while (1) ;
	}

	syscall(SYS_shutdown);
	while (1) ;
	return 0;
}
