#include "mod.h"

device_t device_table[N_DEVICE];

static bool device_path_eq(const char *path, const char *target)
{
	int target_len;

	if (path == NULL || target == NULL)
		return false;

	target_len = strlen(target);
	if (strlen(path) != target_len)
		return false;

	return strncmp(path, target, (uint32)target_len) == 0;
}

bool device_path_lookup(const char *path, uint16 *major)
{
	if (path == NULL || major == NULL)
		return false;

	if (device_path_eq(path, "/dev/stdin")) {
		*major = INODE_MAJOR_STDIN;
		return true;
	}
	if (device_path_eq(path, "/dev/stdout")) {
		*major = INODE_MAJOR_STDOUT;
		return true;
	}
	if (device_path_eq(path, "/dev/stderr")) {
		*major = INODE_MAJOR_STDERR;
		return true;
	}
	if (device_path_eq(path, "/dev/zero")) {
		*major = INODE_MAJOR_ZERO;
		return true;
	}
	if (device_path_eq(path, "/dev/null")) {
		*major = INODE_MAJOR_NULL;
		return true;
	}
	if (device_path_eq(path, "/dev/gpt0")) {
		*major = INODE_MAJOR_GPT0;
		return true;
	}
	if (device_path_eq(path, "/dev/rtc") || device_path_eq(path, "/dev/rtc0") ||
		device_path_eq(path, "/dev/misc/rtc")) {
		*major = INODE_MAJOR_RTC;
		return true;
	}

	return false;
}

/* 标准输入设备 */
static uint32 device_stdin_read(uint32 len, uint64 dst, bool is_user_dst)
{
	return cons_read(len, dst, is_user_dst);
}

/* 标准输出设备 */
static uint32 device_stdout_write(uint32 len, uint64 src, bool is_user_src)
{
	return cons_write(len, src, is_user_src);
}

/* 标准错误输出设备 */
static uint32 device_stderr_write(uint32 len, uint64 src, bool is_user_src)
{
	printf("ERROR: ");
	return cons_write(len, src, is_user_src);
}

/* 无限0流 */
static uint32 device_zero_read(uint32 len, uint64 dst, bool is_user_dst)
{
	uint32 write_len = 0, cut_len = 0;

	uint64 src = (uint64)pmem_alloc(true);
	proc_t *p = myproc();

	while (write_len < len)
	{
		cut_len = MIN(len - write_len, PGSIZE);
		
		if (is_user_dst)
			uvm_copyout(p->pgtbl, dst, src, cut_len);
		else
			memmove((void*)dst, (void*)src, cut_len);

		dst += cut_len;
		write_len += cut_len;
	}

	pmem_free(src, true);

	return write_len;
}

/* 空设备读取 */
static uint32 device_null_read(uint32 len, uint64 dst, bool is_user_dst)
{
	return 0;
}

/* 空设备写入 */
static uint32 device_null_write(uint32 len, uint64 src, bool is_user_src)
{
	return len;
}

/* 彩蛋: 笨蛋GPT */
static uint32 device_gpt0_write(uint32 len, uint64 src, bool is_user_src)
{
	char tmp[STR_MAXLEN + 1];
	proc_t *p = myproc();

	tmp[len] = '\0';

	if (is_user_src)
		uvm_copyin(p->pgtbl, (uint64)tmp, src, len);
	else
		memmove(tmp, (void*)src, len);

	if (strncmp(tmp, "Hello", len) == 0) {
		printf("Hi, I am gpt0!\n");
	} else if (strncmp(tmp, "Guess who I am", len) == 0) {
		printf("Your procid is %d and name is %s.\n", p->pid, p->name);
	} else if (strncmp(tmp, "How many free memory left", len) == 0) {
		uint32 kernel_free_pages, user_free_pages;
		pmem_stat(&kernel_free_pages, &user_free_pages);
		printf("We have %d free pages in kernel space, %d free pages in user space!\n",
			kernel_free_pages, user_free_pages);
	} else if (strncmp(tmp, "Good job", len) == 0) {
		printf("Thanks for your kind words!\n");
	} else if (strncmp(tmp, "Happy New Year!", len) == 0) {
		printf("Welcome to 2026,human! Have a nice year!\n");
	} else if (strncmp(tmp, "Have we really finished these 9 labs?", len) == 0) {
		printf("Yes! You really did it! Congratulations!!\n");
	} else {
		printf("Sorry, I can not understand it.\n");
	}

	return len;
}

static uint32 device_rtc_read(uint32 len, uint64 dst, bool is_user_dst)
{
	(void)dst;
	(void)is_user_dst;
	return len > 0 ? 0 : 0;
}

static uint32 device_rtc_write(uint32 len, uint64 src, bool is_user_src)
{
	(void)src;
	(void)is_user_src;
	return len;
}

/* 注册设备 */
static void device_register(uint32 index, char* name,
	uint32(*read)(uint32, uint64, bool),
	uint32(*write)(uint32, uint64, bool))
{
	memmove(device_table[index].name, name, MAXLEN_FILENAME);
	device_table[index].read = read;
	device_table[index].write = write;
}

/* 初始化device_table */
void device_init()
{
	// 1. 清空表
	for (int i = 0; i < (int)N_DEVICE; i++) {
		memset(device_table[i].name, 0, MAXLEN_FILENAME);
        device_table[i].read = NULL;
        device_table[i].write = NULL;
    }

	// 2. 注册设备
	device_register(INODE_MAJOR_STDIN,  "stdin",  device_stdin_read,  NULL);
    device_register(INODE_MAJOR_STDOUT, "stdout", NULL,              device_stdout_write);
    device_register(INODE_MAJOR_STDERR, "stderr", NULL,              device_stderr_write);
    device_register(INODE_MAJOR_ZERO,   "zero",   device_zero_read,  NULL);
    device_register(INODE_MAJOR_NULL,   "null",   device_null_read,  device_null_write);
    device_register(INODE_MAJOR_GPT0,   "gpt0",   NULL,              device_gpt0_write);
    device_register(INODE_MAJOR_RTC,    "rtc",    device_rtc_read,   device_rtc_write);

	// EXT4 评测盘为只读，不在磁盘上创建 /dev 节点。
	// /dev/* 路径由 file_open 的虚拟设备映射直接处理。
	if (ext4_is_active())
		return;

	// 3.  确保 /dev 存在（不存在则创建目录）
	if (path_to_inode("/dev") == NULL) {
        inode_t *devdir = path_create_inode("/dev", INODE_TYPE_DIR,
            INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
        if (devdir)
            inode_put(devdir);
    }

	if (path_to_inode("/dev") == NULL)
		return;

	// 4. 确保 /dev/* 设备文件存在（不存在则创建 device inode）
	struct { const char *path; uint16 major; } devs[] = {
        {"/dev/stdin",  INODE_MAJOR_STDIN},
        {"/dev/stdout", INODE_MAJOR_STDOUT},
        {"/dev/stderr", INODE_MAJOR_STDERR},
        {"/dev/zero",   INODE_MAJOR_ZERO},
        {"/dev/null",   INODE_MAJOR_NULL},
        {"/dev/gpt0",   INODE_MAJOR_GPT0},
        {"/dev/rtc",    INODE_MAJOR_RTC},
        {"/dev/rtc0",   INODE_MAJOR_RTC},
        {"/dev/misc/rtc", INODE_MAJOR_RTC},
    };

	for (int i = 0; i < (int)(sizeof(devs) / sizeof(devs[0])); i++) {
		if (path_to_inode((char*)devs[i].path) != NULL)
            continue;

		// 创建设备文件 inode
		inode_t *dip = path_create_inode((char*)devs[i].path, INODE_TYPE_DIVICE,
            devs[i].major, INODE_MINOR_DEFAULT);
        if (dip)
            inode_put(dip);
    }
}

/* 检查文件major字段的合法性 */
bool device_open_check(uint16 major, uint32 open_mode)
{
	// major 超出范围
	if (major >= N_DEVICE)
		return false;

	device_t *d = &device_table[major];
    if (d->name[0] == 0)
		return false;
        

	bool want_r = (open_mode & FILE_OPEN_READ) != 0;
    bool want_w = (open_mode & FILE_OPEN_WRITE) != 0;

	// 打开的读/写权限与设备支持的操作不匹配
	if (want_r && d->read == NULL)  
		return false;
	if (want_w && d->write == NULL) 
		return false;

	return true;
}

/* 从设备文件中读取数据 */
uint32 device_read_data(uint16 major, uint32 len, uint64 dst, bool is_user_dst)
{
	// major 超出范围
	if (major >= N_DEVICE)
		return (uint32)-1;

	device_t *d = &device_table[major];

	// 设备不存在或不支持读操作
    if (d->read == NULL)  
		return (uint32)-1;

	return d->read(len, dst, is_user_dst);
}

/* 向设备文件写入数据 */
uint32 device_write_data(uint16 major, uint32 len, uint64 src, bool is_user_src)
{
	// major 超出范围
	if (major >= N_DEVICE)
        return (uint32)-1;

	device_t *d = &device_table[major];

	// 设备不存在或不支持写操作
	if (d->write == NULL)
		return (uint32)-1;

	return d->write(len, src, is_user_src);
}
