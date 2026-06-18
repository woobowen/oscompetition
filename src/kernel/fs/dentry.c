#include "mod.h"

/*
	出于简化目的的假设:
	如果inode_disk.type == INODE_TYPE_DIR
	那么inode_disk.size <= BLOCKSIZE (只有inode_disk.index[0]有效)
	也就是说, 单个目录最多包含BLOCKSIZE / sizeof(dentry)个目录项

	另外, INODE_TYPE_DATA要求数据之间没有空隙
	但是对于INODE_TYPE_DIR来说是无法做到的(目录项的删除很常见)
	因此, ip->size代表block中已经使用的空间大小
*/


/*----------------dentry的查找、增加、删除操作-----------------*/

/*
	在目录ip中查找是否存在名字为name的目录项
	如果找到了返回目录项中存储的inode_num
	如果没找到返回INVALID_INODE_NUM
	注意: 调用者需要持有ip->slk
*/
uint32 dentry_search(inode_t *ip, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_search: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_search: not dir!");

	// 目前简化假设目录只占用一个 block (index[0])
	uint32 block_num = ip->disk_info.index[0];
	if (block_num == 0)
		return INVALID_INODE_NUM;

	buffer_t *buf = buffer_get(block_num);
	dentry_t *de = (dentry_t *)buf->data;

	// 遍历 block 中的所有 dentry 槽位
	for (int i = 0; i < DENTRY_PER_BLOCK; i++) {
		// 检查当前槽位是否有效且名称匹配
		if (de[i].name[0] != 0 && strncmp(de[i].name, name, MAXLEN_FILENAME) == 0) {
			uint32 inode_num = de[i].inode_num;
			buffer_put(buf);
			return inode_num; // 找到匹配的目录项，返回对应的 inode_num
		}
	}

	buffer_put(buf);
	return INVALID_INODE_NUM; // 未找到匹配的目录项
}

/*
	在目录ip中查找是否存在序号为inode_num的目录项
	如果存在则将它的名字拷贝到name, 返回name_len
	如果不存在则返回-1
	注意: 调用者需要持有ip->slk
*/
uint32 dentry_search_2(inode_t *ip, uint32 inode_num, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_search_2: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_search_2: not dir!");

	uint32 block_num = ip->disk_info.index[0];
	if (block_num == 0)
		return (uint32)-1;

	buffer_t *buf = buffer_get(block_num);
	dentry_t *de = (dentry_t *)buf->data;

	// 遍历 block 中的所有 dentry 槽位
	for (int i = 0; i < DENTRY_PER_BLOCK; i++) {
		// 检查当前槽位是否有效且 inode_num 匹配
		if (de[i].name[0] != 0 && de[i].inode_num == inode_num) {
			// 找到匹配的目录项，拷贝名称
			memmove(name, de[i].name, MAXLEN_FILENAME);
			name[MAXLEN_FILENAME - 1] = 0; // 确保以 '\0' 结尾

			uint32 name_len = (uint32)strlen(name);
			buffer_put(buf);
			return name_len; // 返回名称长度
		}
	}

	buffer_put(buf);
	return (uint32)-1; // 未找到匹配的目录项
}

/*
	在目录ip中寻找空闲槽位, 插入新的dentry
	如果成功插入则返回这个目录项的偏移量(还需要更新size)
	如果插入失败(没有空间/发生重名)返回-1
	注意: 调用者需要持有ip->slk
*/
uint32 dentry_create(inode_t *ip, uint32 inode_num, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_create: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_create: not dir!");

	// 检查重名
	if (dentry_search(ip, name) != INVALID_INODE_NUM) {
		return -1; // 重名，插入失败
	}

	uint32 block_num = ip->disk_info.index[0];
	// 如果目录还没有分配 block，先分配一个
    if (block_num == 0) {
		block_num = bitmap_alloc_block();
		if (block_num == (uint32)-1) {
			return -1; // 分配 block 失败
		}
		ip->disk_info.index[0] = block_num;

		// 新分配的块需要清零，保证 dentry.name[0] == 0
		buffer_t *buf = buffer_get(block_num);
        memset(buf->data, 0, BLOCK_SIZE);
        buffer_write(buf);
        buffer_put(buf);

		// 目录大小更新为 BLOCK_SIZE（一个块）
		ip->disk_info.size = BLOCK_SIZE;
        inode_rw(ip, true); // 写回 inode 元数据
	}

	buffer_t *buf = buffer_get(block_num);
	dentry_t *de = (dentry_t *)buf->data;
	int empty_slot = -1;

	// 遍历 block 中的所有 dentry 槽位，寻找空闲槽位
	for (int i = 0; i < DENTRY_PER_BLOCK; i++) {
		if (de[i].name[0] == 0) { // 找到空闲槽位
			empty_slot = i;
			break;
		}
	}

	if (empty_slot == -1) {
		buffer_put(buf);
		return -1; // 没有空闲槽位，插入失败
	}

	// 写入新的 dentry
	memmove(de[empty_slot].name, name, MAXLEN_FILENAME);
    de[empty_slot].inode_num = inode_num;

	buffer_write(buf);
	buffer_put(buf);
	// 返回插入的目录项偏移量
	return (uint32)(empty_slot * sizeof(dentry_t)); 
}

/*
	在目录ip下删除名称为name的dentry, 返回它的inode_num
	如果匹配失败或者遇到非法情况返回INVALID_INODE_NUM
	注意: 调用者需要持有ip->slk
*/
uint32 dentry_delete(inode_t *ip, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_delete: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_delete: not dir!");

	uint32 block_num = ip->disk_info.index[0];
	if (block_num == 0)
		return INVALID_INODE_NUM; // 目录为空，无法删除
	
	buffer_t *buf = buffer_get(block_num);
	dentry_t *de = (dentry_t *)buf->data;

	// 遍历 block 中的所有 dentry 槽位，寻找匹配的目录项
	for (int i = 0; i < DENTRY_PER_BLOCK; i++) {
		// 检查当前槽位是否有效且名称匹配
		if (de[i].name[0] != 0 && strncmp(de[i].name, name, MAXLEN_FILENAME) == 0) {
			uint32 inode_num = de[i].inode_num;
			// 删除：标记该槽位为空
			memset(de[i].name, 0, MAXLEN_FILENAME);
			de[i].inode_num = 0;

			buffer_write(buf);
			buffer_put(buf);
			return inode_num; // 返回被删除目录项的 inode_num
		}
	}

	buffer_put(buf);
	return INVALID_INODE_NUM; // 未找到匹配的目录项，删除失败
}

/*
	向缓冲区[dst, dst + len)中填充有效的dentry
	返回成功填充的数据量(字节)
	注意: 调用者需持有ip->slk
*/
uint32 dentry_transmit(inode_t *ip, uint32 offset, uint64 dst, uint32 len, bool is_user_dst)
{
	assert(sleeplock_holding(&ip->slk), "dentry_transmit: slk!");
    assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_transmit: not dir!");

	if (len < sizeof(dentry_t))
		return 0; // 缓冲区太小

	uint32 skip_entries = offset / sizeof(dentry_t);

	if (ext4_is_active()) {
		typedef struct ext4_dirent_hdr {
			uint32 inode;
			uint16 rec_len;
			uint8 name_len;
			uint8 file_type;
		} ext4_dirent_hdr_t;

		uint32 copied = 0;
		uint32 pos = 0;
		while (pos + sizeof(ext4_dirent_hdr_t) <= ip->disk_info.size) {
			ext4_dirent_hdr_t hdr;
			if (ext4_read_inode_data(ip->inode_num, pos, sizeof(hdr), &hdr) != sizeof(hdr))
				break;
			if (hdr.rec_len < sizeof(ext4_dirent_hdr_t))
				break;

			if (hdr.inode != 0 && hdr.name_len > 0 && hdr.name_len < MAXLEN_FILENAME) {
				if (skip_entries > 0) {
					skip_entries--;
				} else {
					dentry_t out;
					memset(&out, 0, sizeof(out));
					if (ext4_read_inode_data(ip->inode_num, pos + sizeof(ext4_dirent_hdr_t), hdr.name_len, out.name) != hdr.name_len)
						break;
					out.name[hdr.name_len] = 0;
					out.inode_num = hdr.inode;

					if (copied + sizeof(dentry_t) > len)
						break;

					if (is_user_dst)
						uvm_copyout(myproc()->pgtbl, dst + copied, (uint64)&out, sizeof(dentry_t));
					else
						memmove((void *)(dst + copied), (void *)&out, sizeof(dentry_t));

					copied += sizeof(dentry_t);
				}
			}

			pos += hdr.rec_len;
		}

		return copied;
	}

	uint32 block_num = ip->disk_info.index[0];
	if (block_num == 0)
		return 0; 
	buffer_t *buf = buffer_get(block_num);
	dentry_t *de = (dentry_t *)buf->data;

	uint32 copied = 0;
	// 遍历 block 中的所有 dentry 槽位
	for (int i = 0; i < DENTRY_PER_BLOCK; i++) {
		// 检查当前槽位是否有效
		if (de[i].name[0] != 0) {
			if (skip_entries > 0) {
				skip_entries--;
				continue;
			}

			// 检查是否还有足够空间拷贝一个 dentry
			if (copied + sizeof(dentry_t) > len) 
				break; // 缓冲区空间不足

			if (is_user_dst) { // 传输到用户空间
				uvm_copyout(myproc()->pgtbl, dst + copied, (uint64)&de[i], sizeof(dentry_t));
			} else { // 传输到内核空间
				memmove((void *)(dst + copied), (void *)&de[i], sizeof(dentry_t));
			}
			copied += sizeof(dentry_t);
		}
	}

	buffer_put(buf);
	return copied; // 返回成功填充的数据量(字节)
}


/* 输出目录中所有有效目录项的信息 (for debug) */
void dentry_print(inode_t *ip)
{
	assert(sleeplock_holding(&ip->slk), "dentry_print: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_print: not dir!");

	dentry_t *de;
	buffer_t *buf;

	if (ip->disk_info.index[0] == 0)
		panic("dentry_print: invalid index[0]!");
	
	printf("inode_num = %d, dentries:\n", ip->inode_num);

	buf = buffer_get(ip->disk_info.index[0]);
	for (de = (dentry_t*)(buf->data); de < (dentry_t*)(buf->data + BLOCK_SIZE); de++)
	{
		if (de->name[0] != 0) {
			printf("dentry: offset = %d, inode_num = %d, name = %s\n",
				(uint32)((uint8*)de - buf->data), de->inode_num, de->name);
		}
	}
	buffer_put(buf);

	printf("\n");
}

/*------------------从文件名到文件路径-----------------*/

/*
	Examples:
	get_element("a/bb/c", name) = "bb/c" + name = "a"
	get_element("///aa//bb", name) = "bb" + name = "aa"
	get_element("aaa", name) = "" + name = "aaa"
	get_element("", name) = NULL + name = ""
	get_element("//", name) = NULL + name = ""
*/
static char* get_element(char *path, char *name)
{
	/* 跳过前置的'/' */
    while (*path == '/')
		path++;

	/* 如果遇到末尾了则返回 */
    if (*path == 0) {
		name[0] = 0;
		return NULL;
	}

	/* 记录起点位置 */
    char *start = path;
    
	/* 推进path直到遇到'/'或者到达末尾 */
	while (*path != '/' && *path != 0)
        path++;

	/* 提取到的name的长度 */
    int len = path - start;
	len = MIN(len, MAXLEN_FILENAME-1);
	
	/* 设置name */
	memmove(name, start, len);
	name[len] = 0;

	/* 跳过后置的'/' */
    while (*path == '/') path++;

    return path;
}
/*
	根据文件路径(/A/B/C)查找对应inode(inode_B or inode_C)
	如果find_parent_inode == true, 返回父节点inode, name为下一级子节点的名字
	如果find_parent_inode == false, 返回子节点inode, name无意义
	如果失败返回NULL
*/
static inode_t* __path_to_inode(char *path, char *name, bool find_parent_inode)
{
	inode_t *ip, *next_ip;

	if (path == NULL) 
		return NULL;

	// 1. 决定起始目录 （支持绝对路径和相对路径）
	if (path[0] == '/'){
		ip = inode_get(ROOT_INODE); // 从根目录开始
	} else {
		proc_t *p = myproc();
		if (p != NULL && p->cwd != NULL)
			ip = inode_dup(p->cwd); // 从当前工作目录开始
		else 
			ip = inode_get(ROOT_INODE); // 退回根目录
	}

	if (ip == NULL)
		return NULL;

	inode_lock(ip); 

	// 2. 循环解析路径分量
	while ((path = get_element(path, name)) != NULL) {
		// 如果需要找父节点，且 path 已经为空（说明 name 是最后一级），则当前 ip 就是父节点
		if (find_parent_inode && *path == '\0') {
			// 此时 name 已经被 get_element 填充为最后一级的文件名
            inode_unlock(ip); // 【修复】 必须解锁！
			return ip; // 返回父节点
		}

		// 在当前目录 ip 中查找 name
		if (ip->disk_info.type != INODE_TYPE_DIR) {
			// 不是目录，无法继续查找
			inode_unlock(ip);
			inode_put(ip);
			return NULL;
		}

		uint32 next_inode_num = dentry_search(ip, name);
		if (next_inode_num == INVALID_INODE_NUM) {
			// 未找到对应的目录项
			inode_unlock(ip);
			inode_put(ip);
			return NULL;
		}

		// 获取下一级 inode
		// 先释放当前目录锁和 inode，再获取下一级 inode（防止死锁）
		inode_unlock(ip); // 释放当前目录锁
		next_ip = inode_get(next_inode_num);
		inode_put(ip); // 释放当前目录 inode

		if (next_ip == NULL)
			return NULL;

		ip = next_ip;
		inode_lock(ip); // 锁定下一级 inode
	}

	// 3. 循环结束
	if (find_parent_inode) {
		// 需要返回父节点：但路径已经解析完毕，说明没有父节点
		inode_unlock(ip);
		inode_put(ip);
		return NULL;
	}

	// 需要返回最后一级 inode：
	inode_unlock(ip);
	return ip;
}

/*
	基于path寻找inode
	失败返回NULL
*/
static bool path_eq(const char *a, const char *b)
{
	if (a == NULL || b == NULL)
		return false;
	return strlen(a) == strlen(b) && strncmp(a, b, (uint32)strlen(b)) == 0;
}

static char *glibc_lib_alias(char *path)
{
	if (path_eq(path, "/lib/ld-linux-riscv64-lp64d.so.1"))
		return "/glibc/lib/ld-linux-riscv64-lp64d.so.1";
	if (path_eq(path, "/lib/libc.so.6") || path_eq(path, "/lib/libc.so"))
		return "/glibc/lib/libc.so.6";
	if (path_eq(path, "/lib/libm.so.6") || path_eq(path, "/lib/libm.so"))
		return "/glibc/lib/libm.so.6";
	return NULL;
}

inode_t* path_to_inode(char *path)
{
	if (ext4_is_active()) {
		uint32 inode_num;
		uint16 inode_type;
		uint32 start = 0;   // 0 => ext4_lookup_path 内部用 EXT4_ROOT_INO
		if (path[0] != '/') {
			proc_t *p = myproc();
			if (p != NULL && p->cwd != NULL && p->cwd->inode_num != INVALID_INODE_NUM)
				start = p->cwd->inode_num;   // 相对路径: 从 cwd 起查
		}
		if (ext4_lookup_path(start, path, &inode_num, &inode_type) < 0) {
			char *alias = glibc_lib_alias(path);
			if (alias == NULL || ext4_lookup_path(0, alias, &inode_num, &inode_type) < 0)
				return NULL;
		}
		return inode_get(inode_num);
	}

	char name[MAXLEN_FILENAME];
	inode_t *ip = __path_to_inode(path, name, false);
	if (ip == NULL) {
		char *alias = glibc_lib_alias(path);
		if (alias != NULL)
			ip = __path_to_inode(alias, name, false);
	}
	return ip;
}

/* 
	基于path寻找inode->parent, 将inode->name放入name
	失败返回NULL, 同时name无效
*/
inode_t* path_to_parent_inode(char *path, char *name)
{
	return __path_to_inode(path, name, true);
}

/*
	将inode对应的完整路径填入path中(缓冲区长度为len)
	成功返回偏移量(从path+offset开始有效), 失败返回-1
*/
uint32 inode_to_path(inode_t *ip, char *path, uint32 len)
{
	// 至少需要放一个 '/' 和 '\0'
	if (ip == NULL || path == NULL || len < 2) // 空间不足
		return (uint32)-1;

	inode_lock(ip);
	// 输入 inode 需要是目录
	if (ip->disk_info.type != INODE_TYPE_DIR) {
		inode_unlock(ip);
		return (uint32)-1;
	}
	inode_unlock(ip);

	// 末尾放 '\0'
	uint32 off = len;
	path[len - 1] = '\0';

	inode_t *cur = inode_dup(ip); 

	while (1){
		// 回溯终止条件：到根目录，直接放一个 '/'
		if (cur->inode_num == ROOT_INODE){
			if (off < 2) {
				// 空间不足
				inode_put(cur);
				return (uint32)-1;
			}
			path[--off] = '/';
			inode_put(cur);
			return off;
		}

		// 1. 找父 inode：通过 cur 目录中的 ".."
		inode_lock(cur);
		uint32 parent_num = dentry_search(cur, "..");
		inode_unlock(cur);

		if (parent_num == INVALID_INODE_NUM) {
			// 找不到父目录，失败
			inode_put(cur);
			return (uint32)-1;
		}

		inode_t *parent = inode_get(parent_num);

		// 2. 在父目录中反查 cur 的名字：利用 dentry_search_2
		char name[MAXLEN_FILENAME];
		inode_lock(parent);
		uint32 name_len = dentry_search_2(parent, cur->inode_num, name);
		inode_unlock(parent);

		if (name_len == (uint32)-1 || name_len == 0) {
			// 反查失败
			inode_put(parent);
			inode_put(cur);
			return (uint32)-1;
		}

		// 3. 预留 "/" + name 的空间
		if (off < name_len + 1) {
			// 空间不足
			inode_put(parent);
			inode_put(cur);
			return (uint32)-1;
		}

		off -= name_len;
		memmove(path + off, name, name_len);
		path[--off] = '/';

		// 4. 继续向上回溯，直到根目录
		inode_put(cur);
		cur = parent;
	}
}

/*
	基于path创建新的inode
	成功返回inode, 失败返回NULL
*/
inode_t* path_create_inode(char *path, uint16 type, uint16 major, uint16 minor)
{
	if (path == NULL)
		return NULL;

	char name[MAXLEN_FILENAME];
	// 父目录 inode
	inode_t *parent = path_to_parent_inode(path, name);
	if (parent == NULL) {
		return NULL; 
	}

	inode_lock(parent);

	// 父目录必须是目录类型
	if (parent->disk_info.type != INODE_TYPE_DIR) {
		inode_unlock(parent);
		inode_put(parent);
		return NULL;
	}

	// 检查重名
	if (dentry_search(parent, name) != INVALID_INODE_NUM) {
		inode_unlock(parent);
		inode_put(parent);
		return NULL; // 重名，创建失败
	}

	// 创建新的 inode
	inode_t *ip = inode_create(type, major, minor);
	if (ip == NULL) {
		inode_unlock(parent);
		inode_put(parent);
		return NULL; // 创建 inode 失败
	}

	// 若创建目录：初始化 "." 和 ".."
	if (type == INODE_TYPE_DIR) {
        inode_lock(ip);
		if (dentry_create(ip, ip->inode_num, ".") == (uint32)-1) {
			// 创建 "." 失败 -- 回滚
			// 标记 nlink=0，使 inode_put 后可回收
			ip->disk_info.nlink = 0;
			inode_rw(ip, true); // 写回 inode 元数据
			inode_unlock(ip);
			inode_unlock(parent);
			inode_put(parent);
            inode_put(ip);
            return NULL;
		}
		if (dentry_create(ip, parent->inode_num, "..") == (uint32)-1) {
			// 创建 ".." 失败 -- 回滚
			ip->disk_info.nlink = 0;
			inode_rw(ip, true);
            inode_unlock(ip);
			inode_unlock(parent);
			inode_put(parent);
			inode_put(ip);
			return NULL;
		}
		inode_unlock(ip);
	}

	// 在父目录中创建新的 dentry: name -> ip->inode_num
	if (dentry_create(parent, ip->inode_num, name) == (uint32)-1) {
		// 创建 dentry 失败 -- 回滚
		inode_lock(ip);
		ip->disk_info.nlink = 0;
        inode_rw(ip, true);
        inode_unlock(ip);
		inode_unlock(parent);
        inode_put(parent);
        inode_put(ip);
        return NULL;
	}

	inode_unlock(parent);
	inode_put(parent);
	return ip; // 成功返回新创建的 inode
}

/*
	构建文件硬链接 (new_path 指向 old_path 指向的 inode)
	核心操作包括 nlink++ 和 dentry_create()
	注意: old_path指向的inode不能是目录类型的
	成功返回0, 失败返回-1
*/
uint32 path_link(char *old_path, char *new_path)
{
	if (old_path == NULL || new_path == NULL)
        return (uint32)-1;
	
	inode_t *old_ip = path_to_inode(old_path);
	if (old_ip == NULL)
		return (uint32)-1;

	// 禁止链接目录
	inode_lock(old_ip);
    if (old_ip->disk_info.type == INODE_TYPE_DIR) {
        inode_unlock(old_ip);
        inode_put(old_ip);
        return (uint32)-1;
    }
    inode_unlock(old_ip);

	// 获取 new_path 的父目录
	char name[MAXLEN_FILENAME];
    inode_t *parent = path_to_parent_inode(new_path, name);
	if (parent == NULL) {
		inode_put(old_ip);
		return (uint32)-1;
	}

	inode_lock(parent);

	// 父目录必须是目录类型
	if (parent->disk_info.type != INODE_TYPE_DIR) {
		inode_unlock(parent);
		inode_put(parent);
        inode_put(old_ip);
        return (uint32)-1;
	}

	// 检查重名: new_path 已存在则失败
	if (dentry_search(parent, name) != INVALID_INODE_NUM) {
		inode_unlock(parent);
		inode_put(parent);
		inode_put(old_ip);
		return (uint32)-1;
	}

	// 先创建新的 dentry
	if (dentry_create(parent, old_ip->inode_num, name) == (uint32)-1) {
		// 创建 dentry 失败
		inode_unlock(parent);
		inode_put(parent);
		inode_put(old_ip);
		return (uint32)-1;
	}

	// 再增加 nlink （持 inode 锁写回磁盘）
	inode_lock(old_ip);
	old_ip->disk_info.nlink++;
	inode_rw(old_ip, true);
	inode_unlock(old_ip);
	inode_unlock(parent);
	inode_put(parent);
	inode_put(old_ip);
	return 0; 
}

/*
	解除文件硬链接
	成功返回0, 失败返回-1
*/
uint32 path_rename(char *old_path, char *new_path)
{
	if (old_path == NULL || new_path == NULL)
		return (uint32)-1;

	char old_name[MAXLEN_FILENAME], new_name[MAXLEN_FILENAME];
	inode_t *old_parent = path_to_parent_inode(old_path, old_name);
	inode_t *new_parent = path_to_parent_inode(new_path, new_name);
	if (old_parent == NULL || new_parent == NULL) {
		if (old_parent) inode_put(old_parent);
		if (new_parent) inode_put(new_parent);
		return (uint32)-1;
	}

	if (old_parent->inode_num == new_parent->inode_num) {
		inode_lock(old_parent);
		uint32 inum = dentry_search(old_parent, old_name);
		if (inum == INVALID_INODE_NUM || dentry_search(old_parent, new_name) != INVALID_INODE_NUM) {
			inode_unlock(old_parent);
			inode_put(old_parent);
			inode_put(new_parent);
			return (uint32)-1;
		}
		if (dentry_create(old_parent, inum, new_name) == (uint32)-1 ||
			dentry_delete(old_parent, old_name) == INVALID_INODE_NUM) {
			inode_unlock(old_parent);
			inode_put(old_parent);
			inode_put(new_parent);
			return (uint32)-1;
		}
		inode_unlock(old_parent);
		inode_put(old_parent);
		inode_put(new_parent);
		return 0;
	}

	inode_lock(old_parent);
	uint32 inum = dentry_search(old_parent, old_name);
	if (inum == INVALID_INODE_NUM) {
		inode_unlock(old_parent);
		inode_put(old_parent);
		inode_put(new_parent);
		return (uint32)-1;
	}
	inode_unlock(old_parent);

	inode_lock(new_parent);
	if (dentry_search(new_parent, new_name) != INVALID_INODE_NUM ||
		dentry_create(new_parent, inum, new_name) == (uint32)-1) {
		inode_unlock(new_parent);
		inode_put(old_parent);
		inode_put(new_parent);
		return (uint32)-1;
	}
	inode_unlock(new_parent);

	inode_lock(old_parent);
	uint32 deleted = dentry_delete(old_parent, old_name);
	inode_unlock(old_parent);
	inode_put(old_parent);
	inode_put(new_parent);
	return deleted == INVALID_INODE_NUM ? (uint32)-1 : 0;
}

uint32 path_unlink(char *path)
{
	if (path == NULL)
        return (uint32)-1;
	if (ext4_is_active())
        return (uint32)-1;

	char name[MAXLEN_FILENAME];
	inode_t *parent = path_to_parent_inode(path, name);
	if (parent == NULL)
        return (uint32)-1;

	// 禁止删除 "." 和 ".."
	if (strncmp(name, ".", MAXLEN_FILENAME) == 0 || strncmp(name, "..", MAXLEN_FILENAME) == 0) {
		inode_put(parent);
		return (uint32)-1;
	}

	inode_lock(parent);
	// 父目录必须是目录类型
	if (parent->disk_info.type != INODE_TYPE_DIR) {
		inode_unlock(parent);
		inode_put(parent);
		return (uint32)-1;
	}

	// 先定位目标 inode
	uint32 inode_num = dentry_search(parent, name);
	if (inode_num == INVALID_INODE_NUM) {
		// 目录项不存在
		inode_unlock(parent);
		inode_put(parent);
		return (uint32)-1;
	}

	inode_t *ip = inode_get(inode_num);
    inode_lock(ip);

	// 不允许做目录的 unlink
	if (ip->disk_info.type == INODE_TYPE_DIR) {
		inode_unlock(ip);
		inode_put(ip);
        inode_unlock(parent);
        inode_put(parent);
		return (uint32)-1;
	}

	// 再从父目录中删除 dentry
	if (dentry_delete(parent, name) == INVALID_INODE_NUM) {
		// 删除失败
		inode_unlock(ip);
		inode_put(ip);
		inode_unlock(parent);
		inode_put(parent);
		return (uint32)-1;
	}

	// 最后 nlink-- （持 inode 锁写回磁盘）
	if (ip->disk_info.nlink > 0) {
		ip->disk_info.nlink--;
	}
	inode_rw(ip, true);
	inode_unlock(ip);
	inode_put(ip);
	inode_unlock(parent);
	inode_put(parent); // 如果 nlink 变为0, 会自动回收inode
	return 0;
}
