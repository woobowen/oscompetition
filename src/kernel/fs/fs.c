#include "mod.h"

super_block_t sb; /* 超级块 */
static bool fs_readonly_ext4;

file_t file_table[N_FILE]; // 文件资源池
spinlock_t lk_file_table; // 保护它的锁

/* 初始化file_table */
void file_init()
{
	spinlock_init(&lk_file_table, "lk_file_table");

	// 清空file_table
	spinlock_acquire(&lk_file_table);
	for (int i = 0; i < (int)N_FILE; i++) {
		file_table[i].ip = NULL;
	file_table[i].is_device = false;
	file_table[i].dev_major = 0;
        file_table[i].readable = false;
        file_table[i].writbale = false;
        file_table[i].offset = 0;
        file_table[i].ref = 0;
	}
	spinlock_release(&lk_file_table);
}

/* ===================== 管道实现 (pipe) ===================== */
static pipe_t pipe_pool[N_PIPE];
static spinlock_t lk_pipe_pool;

void pipe_init(void)
{
	spinlock_init(&lk_pipe_pool, "pipepool");
	for (int i = 0; i < N_PIPE; i++)
		pipe_pool[i].used = 0;
}

static pipe_t *pipe_pool_alloc(void)
{
	spinlock_acquire(&lk_pipe_pool);
	for (int i = 0; i < N_PIPE; i++) {
		if (!pipe_pool[i].used) {
			pipe_pool[i].used = 1;
			spinlock_release(&lk_pipe_pool);
			return &pipe_pool[i];
		}
	}
	spinlock_release(&lk_pipe_pool);
	return NULL;
}

static void pipe_pool_free(pipe_t *pi)
{
	spinlock_acquire(&lk_pipe_pool);
	pi->used = 0;
	spinlock_release(&lk_pipe_pool);
}

// 创建管道: *rf=读端, *wf=写端。成功返回0, 失败-1。
int pipe_alloc(file_t **rf, file_t **wf)
{
	pipe_t *pi = pipe_pool_alloc();
	if (pi == NULL)
		return -1;
	spinlock_init(&pi->lk, "pipe");
	pi->nread = 0;
	pi->nwrite = 0;
	pi->readopen = 1;
	pi->writeopen = 1;

	*rf = file_alloc();
	*wf = file_alloc();
	if (*rf == NULL || *wf == NULL) {
		if (*rf) file_close(*rf);
		if (*wf) file_close(*wf);
		pipe_pool_free(pi);
		return -1;
	}
	(*rf)->is_pipe = true; (*rf)->pipe = pi; (*rf)->readable = true;  (*rf)->writbale = false;
	(*wf)->is_pipe = true; (*wf)->pipe = pi; (*wf)->readable = false; (*wf)->writbale = true;
	return 0;
}

// 读管道: 空且写端开 -> 睡等; 写端关且空 -> 返回0(EOF)。一次最多 PIPE_SIZE。
uint32 pipe_read(pipe_t *pi, uint64 addr, uint32 n, bool is_user)
{
	char buf[PIPE_SIZE];
	uint32 i = 0;
	spinlock_acquire(&pi->lk);
	while (pi->nread == pi->nwrite && pi->writeopen) {
		proc_sleep(&pi->nread, &pi->lk);   // 返回后仍持有 pi->lk
	}
	for (i = 0; i < n && i < PIPE_SIZE && pi->nread != pi->nwrite; i++) {
		buf[i] = pi->data[pi->nread % PIPE_SIZE];
		pi->nread++;
	}
	proc_wakeup(&pi->nwrite);   // 唤醒写者
	spinlock_release(&pi->lk);

	if (i > 0) {
		if (is_user) uvm_copyout(myproc()->pgtbl, addr, (uint64)buf, i);
		else memmove((void *)addr, buf, i);
	}
	return i;
}

// 写管道: 满 -> 唤醒读者并睡等; 读端关 -> 返回已写(或-1)。
uint32 pipe_write(pipe_t *pi, uint64 addr, uint32 n, bool is_user)
{
	char buf[PIPE_SIZE];
	uint32 total = 0;
	while (total < n) {
		uint32 chunk = n - total;
		if (chunk > PIPE_SIZE) chunk = PIPE_SIZE;
		if (is_user) uvm_copyin(myproc()->pgtbl, (uint64)buf, addr + total, chunk);
		else memmove(buf, (void *)(addr + total), chunk);

		spinlock_acquire(&pi->lk);
		uint32 w = 0;
		while (w < chunk) {
			if (!pi->readopen) {
				spinlock_release(&pi->lk);
				return total > 0 ? total : (uint32)-1;   // broken pipe
			}
			if (pi->nwrite == pi->nread + PIPE_SIZE) {    // 满
				proc_wakeup(&pi->nread);
				proc_sleep(&pi->nwrite, &pi->lk);
				continue;
			}
			pi->data[pi->nwrite % PIPE_SIZE] = buf[w];
			pi->nwrite++;
			w++;
		}
		proc_wakeup(&pi->nread);
		spinlock_release(&pi->lk);
		total += chunk;
	}
	return total;
}

// 关闭管道一端(由 file_close 在 ref 归零时调用)
void pipe_close(pipe_t *pi, bool writable)
{
	spinlock_acquire(&pi->lk);
	if (writable) { pi->writeopen = 0; proc_wakeup(&pi->nread); }
	else          { pi->readopen = 0;  proc_wakeup(&pi->nwrite); }
	int both_closed = (pi->readopen == 0 && pi->writeopen == 0);
	spinlock_release(&pi->lk);
	if (both_closed)
		pipe_pool_free(pi);
}
/* =================== 管道实现结束 =================== */

/* 从file_table中获取1个空闲file */
file_t* file_alloc()
{
	// 分配策略：线性扫描找第一个 ref == 0 的槽位
	spinlock_acquire(&lk_file_table);
	for (int i = 0; i < (int)N_FILE; i++) {
		if (file_table[i].ref == 0) {
            file_table[i].ref = 1;
            file_table[i].ip = NULL;
		file_table[i].is_device = false;
		file_table[i].dev_major = 0;
            file_table[i].readable = false;
            file_table[i].writbale = false;
            file_table[i].offset = 0;
            file_table[i].is_pipe = false;
            file_table[i].pipe = NULL;
            spinlock_release(&lk_file_table);
            return &file_table[i]; // 返回分配的file
		}
	}
	spinlock_release(&lk_file_table);
	return NULL; // 分配失败
}

/*
	根据路径打开文件 (指定打开模式)
	成功返回file, 失败返回NULL
*/
file_t* file_open(char *path, uint32 open_mode)
{
	if (path == NULL)
		return NULL;

	bool want_r = (open_mode & FILE_OPEN_READ) != 0;
    bool want_w = (open_mode & FILE_OPEN_WRITE) != 0;

	// 必须至少读/写之一
    if (!want_r && !want_w)   return NULL;

	uint16 dev_major = 0;
	if (device_path_lookup(path, &dev_major)) {
		if (!device_open_check(dev_major, open_mode))
			return NULL;

		file_t *devf = file_alloc();
		if (devf == NULL)
			return NULL;

		devf->ip = NULL;
		devf->is_device = true;
		devf->dev_major = dev_major;
		devf->readable = want_r;
		devf->writbale = want_w;
		devf->offset = 0;
		return devf;
	}

	// 1. 先按路径找 inode
	inode_t *ip = path_to_inode(path);

	// 2. 不存在且允许创建：创建 DATA 文件
	if (ip == NULL && (open_mode & FILE_OPEN_CREATE)) {
        ip = path_create_inode(path, INODE_TYPE_DATA, 
			INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
    }
	if (ip == NULL) {
		return NULL; // 文件不存在且未创建成功
	}

	// 3. 若是设备文件：检查权限合法性
	inode_lock(ip);
	uint16 type = ip->disk_info.type;
    uint16 major = ip->disk_info.major;
	inode_unlock(ip);

	if (type == INODE_TYPE_DIVICE) {
		if (!device_open_check(major, open_mode)) {
			inode_put(ip);
			return NULL; // 设备文件打开权限检查失败
		}
	} else if (fs_readonly_ext4 && (want_w || (open_mode & FILE_OPEN_CREATE))) {
		inode_put(ip);
		return NULL;
	}

	// 4. 从 file_table 分配 file 并绑定 inode
	file_t *f = file_alloc();
	if (f == NULL) {
		inode_put(ip);
		return NULL; // 分配 file 失败
	}

	f->ip = ip;
    f->readable = want_r;
    f->writbale = want_w;
    f->offset = 0;
    return f;
}

/* 关闭文件 */
void file_close(file_t *file)
{
	if (file == NULL)
        return;

	inode_t *ip = NULL;

	spinlock_acquire(&lk_file_table);

	if (file->ref == 0) {
        spinlock_release(&lk_file_table);
        panic("file_close: ref already zero"); 
    }

	// 减少引用计数
	file->ref--;
	if (file->ref > 0) {
		// 仍有引用，直接返回
		spinlock_release(&lk_file_table);
		return;
	}

	// 此时 ref == 0：回收槽位
	ip = file->ip;
	bool was_pipe = file->is_pipe;
	pipe_t *pi = file->pipe;
	bool was_writable = file->writbale;
    file->ip = NULL;
	file->is_device = false;
	file->dev_major = 0;
    file->readable = false;
    file->writbale = false;
    file->offset = 0;
	file->is_pipe = false;
	file->pipe = NULL;

	spinlock_release(&lk_file_table);

	if (was_pipe && pi != NULL)
		pipe_close(pi, was_writable);

	if (ip != NULL)
		inode_put(ip); // 释放inode
}

/* 读取文件内容, 返回读到的字节数量 */
uint32 file_read(file_t* file, uint32 len, uint64 dst, bool is_user_dst)
{
	if (file == NULL){
		return (uint32)-1;
	}
    if (!file->readable){
		return (uint32)-1;
	}

	if (file->is_pipe)
		return pipe_read(file->pipe, dst, len, is_user_dst);

	if (file->is_device)
		return device_read_data(file->dev_major, len, dst, is_user_dst);

	if (file->ip == NULL)
		return (uint32)-1;
        

	inode_t *ip = file->ip;

	// 先获取文件类型和主设备号
	inode_lock(ip);
	uint16 type = ip->disk_info.type;
	uint16 major = ip->disk_info.major;
    inode_unlock(ip);

	uint32 read_len = 0;

	// 然后根据文件类型分类讨论
	switch (type) {
	case INODE_TYPE_DATA:
		// 数据文件：从inode中按字节流读取
		inode_lock(ip);
		read_len = inode_read_data(ip, file->offset, len, (void*)dst, is_user_dst);
		file->offset += read_len; // 更新偏移量
		inode_unlock(ip);
		return read_len;
	
	case INODE_TYPE_DIR:
		// 目录文件：按“目录项结构体”输出
		inode_lock(ip);
		read_len = dentry_transmit(ip, file->offset, dst, len, is_user_dst);
		file->offset += read_len; // 更新偏移量
		inode_unlock(ip);
		return read_len;

	case INODE_TYPE_DIVICE:
		// 设备文件：不落磁盘，走设备回调
		read_len = device_read_data(major, len, dst, is_user_dst);
		file->offset += read_len; // 更新偏移量
		return read_len;

	default:
		return (uint32)-1; // 不支持的文件类型
	}
}

/* 写入文件内容, 返回写入的字节数量 */
uint32 file_write(file_t* file, uint32 len, uint64 src, bool is_user_src)
{
	if (file == NULL){
		return (uint32)-1;
	}	
    if (!file->writbale){
		return (uint32)-1;
	}

	if (file->is_pipe)
		return pipe_write(file->pipe, src, len, is_user_src);

	if (file->is_device)
		return device_write_data(file->dev_major, len, src, is_user_src);

	if (file->ip == NULL)
		return (uint32)-1;
        

	inode_t *ip = file->ip;

	// 先获取文件类型和主设备号
	inode_lock(ip);
	uint16 type = ip->disk_info.type;
	uint16 major = ip->disk_info.major;
	inode_unlock(ip);

	uint32 write_len = 0;

	// 然后根据文件类型分类讨论
	switch (type) {
	case INODE_TYPE_DATA:
		// 数据文件：写入inode中
		inode_lock(ip);
		write_len = inode_write_data(ip, file->offset, len, (void*)src, is_user_src);
		inode_rw(ip, true); // 写回磁盘
		file->offset += write_len; // 更新偏移量
		inode_unlock(ip);
		return write_len;

	case INODE_TYPE_DIR:
		// 目录文件：不允许写入
		return (uint32)-1;

	case INODE_TYPE_DIVICE:
		// 设备文件：不落磁盘，走设备回调
		write_len = device_write_data(major, len, src, is_user_src);
		file->offset += write_len; // 更新偏移量
		return write_len;

	default:
		return (uint32)-1; // 不支持的文件类型
	}

}

/* 
	读/写指针的移动
	对于不合理的lseek_offset, 只做尽力而为的移动
	返回新的file->offset
*/
uint32 file_lseek(file_t *file, uint32 lseek_offset, uint32 lseek_flag)
{
	if (file == NULL)
        return 0;

	// 根据 lseek_flag 计算新的 offset
	switch (lseek_flag) {
	case FILE_LSEEK_SET:
		// 从文件开头开始计算
		file->offset = lseek_offset;
		break;
	case FILE_LSEEK_ADD:
		// 从当前位置开始计算
		file->offset += lseek_offset;
		break;
	case FILE_LSEEK_SUB:
		// 从当前位置向前计算
		if (file->offset >= lseek_offset)
			file->offset -= lseek_offset;
		else
			file->offset = 0; 
		break;
	default:
		// 非法 flag：不做处理
        break;
	}
	return file->offset; // 返回新的 offset
}

/* file->ref++ with lock protect */
file_t* file_dup(file_t* file)
{
	if (file == NULL)
        return NULL;

	spinlock_acquire(&lk_file_table);
    if (file->ref == 0) {
        spinlock_release(&lk_file_table);
        return NULL;
    }

	// 增加引用计数
	file->ref++;
	spinlock_release(&lk_file_table);
	return file;
}

/* 获取文件参数, 成功返回0, 失败返回-1 */
uint32 file_get_stat(file_t* file, uint64 user_dst)
{
	 if (file == NULL)
        return (uint32)-1;

	// 填充file_stat结构体
	file_stat_t st;
	memset(&st, 0, sizeof(st));

	if (file->is_device) {
		st.type = INODE_TYPE_DIVICE;
		st.nlink = 1;
		st.size = 0;
		st.inode_num = INVALID_INODE_NUM;
		st.offset = file->offset;
		uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
		return 0;
	}

	if (file->ip == NULL)
		return (uint32)-1;

	inode_t *ip = file->ip;

	inode_lock(ip);
	st.type = ip->disk_info.type;
    st.nlink = ip->disk_info.nlink;
    st.size = ip->disk_info.size;
    st.inode_num = ip->inode_num;
    inode_unlock(ip);

	st.offset = file->offset;

	// 拷贝到用户空间
	uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
	return 0;
}

// 填充 Linux/RISC-V struct stat(128B) 到用户空间, 供 musl 的 fstat/fstatat 使用。
// 返回 0 成功, (uint32)-1 失败。
uint32 file_get_stat_linux(file_t* file, uint64 user_dst)
{
	struct {
		uint64 st_dev, st_ino;
		uint32 st_mode, st_nlink, st_uid, st_gid;
		uint64 st_rdev, __pad1;
		uint64 st_size;
		uint32 st_blksize, __pad2;
		uint64 st_blocks;
		uint64 st_atime_sec, st_atime_nsec;
		uint64 st_mtime_sec, st_mtime_nsec;
		uint64 st_ctime_sec, st_ctime_nsec;
		uint32 __unused4, __unused5;
	} st;
	if (file == NULL) return (uint32)-1;
	memset(&st, 0, sizeof(st));
	st.st_blksize = 512;

	if (file->is_device) {
		st.st_mode  = 0020000 | 0666;   // S_IFCHR
		st.st_nlink = 1;
		st.st_ino   = 1;
	} else {
		if (file->ip == NULL) return (uint32)-1;
		inode_t *ip = file->ip;
		inode_lock(ip);
		short  type  = ip->disk_info.type;
		short  nlink = ip->disk_info.nlink;
		uint32 size  = ip->disk_info.size;
		uint32 inum  = ip->inode_num;
		inode_unlock(ip);

		uint32 mode;
		if (type == INODE_TYPE_DIR)         mode = 0040000 | 0755;  // S_IFDIR
		else if (type == INODE_TYPE_DIVICE) mode = 0020000 | 0666;  // S_IFCHR
		else                                mode = 0100000 | 0755;  // S_IFREG
		st.st_mode   = mode;
		st.st_nlink  = (nlink > 0) ? (uint32)nlink : 1;
		st.st_size   = size;
		st.st_ino    = inum;
		st.st_blocks = (size + 511) / 512;
	}
	uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
	return 0;
}


/* 基于superblock输出磁盘布局信息 (for debug) */
static void sb_print()
{
	printf("\ndisk layout information:\n");
	printf("1. super block:  block[0]\n");
	printf("2. inode bitmap: block[%d - %d]\n", sb.inode_bitmap_firstblock,
		sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks - 1);
	printf("3. inode region: block[%d - %d]\n", sb.inode_firstblock,
		sb.inode_firstblock + sb.inode_blocks - 1);
	printf("4. data bitmap:  block[%d - %d]\n", sb.data_bitmap_firstblock,
		sb.data_bitmap_firstblock + sb.data_bitmap_blocks - 1);
	printf("5. data region:  block[%d - %d]\n", sb.data_firstblock,
		sb.data_firstblock + sb.data_blocks - 1);
	printf("block size = %d Byte, total size = %d MB, total inode = %d\n\n", sb.block_size,
		(int)((unsigned long long)(sb.total_blocks) * sb.block_size / 1024 / 1024), sb.total_inodes);
}

static bool fs_try_ext4_preview()
{
	buffer_t *b = buffer_get(FS_SB_BLOCK);
	ext4_super_preview_t ext4_sb;
	memmove(&ext4_sb, b->data + EXT4_SUPER_OFFSET, sizeof(ext4_sb));
	buffer_put(b);

	if (ext4_sb.magic != EXT4_SUPER_MAGIC)
		return false;
	if (!ext4_mount_from_super(&ext4_sb)) {
		printf("\next4 superblock detected but unsupported features: incompat=0x%x ro_compat=0x%x\n\n",
			ext4_sb.feature_incompat, ext4_sb.feature_ro_compat);
		return false;
	}

	fs_readonly_ext4 = true;

	const ext4_info_t *info = ext4_get_info();
	printf("\next4 filesystem detected on primary disk:\n");
	printf("block size = %d Byte, total blocks = %d, total inode = %d\n",
		info->block_size,
		ext4_sb.blocks_count_lo,
		ext4_sb.inodes_count);
	printf("inodes per group = %d, blocks per group = %d, inode size = %d\n\n",
		ext4_sb.inodes_per_group,
		ext4_sb.blocks_per_group,
		ext4_sb.inode_size);
	return true;
}

void fs_init()
{
	fs_readonly_ext4 = false;
	// 初始化缓冲系统
	buffer_init();
	// 初始化inode缓存与锁
	inode_init();
	// 初始化管道池
	pipe_init();

	// 读取超级块
	buffer_t *b = buffer_get(FS_SB_BLOCK);
	// 将缓冲区内容拷贝到内存中的sb
	memmove(&sb, b->data, sizeof(super_block_t));
	// 归还缓冲（不修改，不需要写回）
	buffer_put(b);

	if (sb.magic_num == FS_MAGIC) {
		// 打印布局信息
		sb_print();
	} else if (!fs_try_ext4_preview()) {
		printf("\nunknown filesystem on primary disk: block0 magic = 0x%x\n\n", sb.magic_num);
	}

	// 初始化设备表
	device_init();
	// 初始化文件表
	file_init();
}