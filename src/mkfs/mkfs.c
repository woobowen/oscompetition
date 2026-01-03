#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include "mkfs.h"

static int disk_fd;                 // 磁盘映像的文件描述符
static super_block_t sb;            // 超级块
static int global_inode_num = 0;    // 全局的inode_num
static int global_block_num = 0;    // 全局的block_num

static char bitmap_buf[BLOCK_SIZE]; // bitmap区域的读写缓冲 供block_alloc和inode_alloc使用
static char inode_buf[BLOCK_SIZE];  // inode区域的读写缓冲 供inode_rw使用
static char data_buf[BLOCK_SIZE];   // data区的读写缓冲 block_rw使用

/*-------------------------关于小端序-------------------------*/

// 转换成小端序
unsigned short xshort(unsigned short x)
{
    unsigned short y;
    unsigned char* a = (unsigned char*)&y;
    a[0] = x;
    a[1] = x >> 8;
    return y;
}

// 转换成小端序
unsigned int xint(unsigned int x)
{
    unsigned int y;
    unsigned char* a = (unsigned char*)&y;
    a[0] = x;
    a[1] = x >> 8;
    a[2] = x >> 16;
    a[3] = x >> 24;
    return y;
}

/*-------------------------磁盘区域读写能力-------------------------*/

/* 读取/写回1个block */
void block_rw(unsigned int block_num, void *buf, bool write_it)
{
    if (lseek(disk_fd, BLOCK_SIZE * block_num, 0) != BLOCK_SIZE * block_num) {
        perror("lseek");
        exit(1);
    }

    if (write_it) {
        if (write(disk_fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
            perror("write");
            exit(1);
        }
    } else {
        if (read(disk_fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
            perror("read");
            exit(1);
        }
    }
}

/* 读取/写回1个inode */
void inode_rw(unsigned int inode_num, inode_disk_t *ip, bool write_it)
{
    unsigned int block_num = sb.inode_firstblock + inode_num / INODE_PER_BLOCK;
    unsigned int byte_offset = (inode_num % INODE_PER_BLOCK) * sizeof(inode_disk_t);
    
    if (write_it) {
        block_rw(block_num, inode_buf, false);
        memcpy(inode_buf + byte_offset, ip, sizeof(inode_disk_t));
        block_rw(block_num, inode_buf, true);
    } else {
        block_rw(block_num, inode_buf, false);
        memcpy(ip, inode_buf + byte_offset, sizeof(inode_disk_t));
    }
}

/* 从磁盘中申请1个空闲的data block */
unsigned int block_alloc()
{
    unsigned int block_num, byte_offset, bit_offset;
    
    block_num = sb.data_bitmap_firstblock + global_block_num / BIT_PER_BLOCK;
    bit_offset = global_block_num % BIT_PER_BLOCK;
    byte_offset = bit_offset / BIT_PER_BYTE;
    bit_offset = bit_offset % BIT_PER_BYTE;

    block_rw(block_num, bitmap_buf, false);
    bitmap_buf[byte_offset] |= (1 << bit_offset);
    block_rw(block_num, bitmap_buf, true);

    return sb.data_firstblock + global_block_num++;
}

/* 从磁盘中申请1个空闲inode */
unsigned int inode_alloc()
{
    unsigned int block_num, byte_offset, bit_offset;
    
    block_num = sb.inode_bitmap_firstblock + global_inode_num / BIT_PER_BLOCK;
    bit_offset = global_inode_num % BIT_PER_BLOCK;
    byte_offset = bit_offset / BIT_PER_BYTE;
    bit_offset = bit_offset % BIT_PER_BYTE;

    block_rw(block_num, bitmap_buf, false);
    bitmap_buf[byte_offset] |= (1 << bit_offset);
    block_rw(block_num, bitmap_buf, true);

    return global_inode_num++;
}

/*-------------------------inode精细化管理-------------------------*/

// 获取/分配 inode 的第 logical_block_num 个数据块号（支持：直接/一级间接/二级间接）
static unsigned int inode_get_or_alloc_block(inode_disk_t *ip, unsigned int logical_block_num)
{
    // 每个 block 能存放的索引数量 (1024)
    unsigned int index_per_block = BLOCK_SIZE / sizeof(unsigned int);

    // 1) 直接映射 (0 ~ 9)
    if (logical_block_num < INODE_INDEX_1) {
        if (ip->index[logical_block_num] == 0)
            ip->index[logical_block_num] = block_alloc();
        return ip->index[logical_block_num];
    }

    // 2) 一级间接映射 (10 ~ 10+2048-1)
    if (logical_block_num < INODE_BLOCK_INDEX_2) {
        unsigned int rel = logical_block_num - INODE_BLOCK_INDEX_1;
        unsigned int l1_idx = rel / index_per_block; // 0..1
        unsigned int l1_off = rel % index_per_block;

        if (l1_idx >= (INODE_INDEX_2 - INODE_INDEX_1)) {
            printf("inode_append: bad l1_idx=%u\n", l1_idx);
            exit(1);
        }

        unsigned int l1_block = ip->index[INODE_INDEX_1 + l1_idx];
        if (l1_block == 0) {
            l1_block = block_alloc();
            ip->index[INODE_INDEX_1 + l1_idx] = l1_block;
            memset(data_buf, 0, BLOCK_SIZE);
            block_rw(l1_block, data_buf, true);
        }

        block_rw(l1_block, data_buf, false);
        unsigned int *l1_table = (unsigned int *)data_buf;
        unsigned int data_block = l1_table[l1_off];
        if (data_block == 0) {
            data_block = block_alloc();
            l1_table[l1_off] = data_block;
            block_rw(l1_block, data_buf, true);
        }
        return data_block;
    }

    // 3) 二级间接映射 (10+2048 ~ 10+2048+1024*1024-1)
    if (logical_block_num < INODE_BLOCK_INDEX_3) {
        unsigned int rel = logical_block_num - INODE_BLOCK_INDEX_2;
        unsigned int l2_idx = rel / index_per_block;
        unsigned int l1_off = rel % index_per_block;

        // 二级索引块（只占一个槽位）
        unsigned int l2_block = ip->index[INODE_INDEX_2];
        if (l2_block == 0) {
            l2_block = block_alloc();
            ip->index[INODE_INDEX_2] = l2_block;
            memset(data_buf, 0, BLOCK_SIZE);
            block_rw(l2_block, data_buf, true);
        }

        // 读取二级索引块，拿到对应的一级索引块
        block_rw(l2_block, data_buf, false);
        unsigned int *l2_table = (unsigned int *)data_buf;
        unsigned int l1_block = l2_table[l2_idx];
        if (l1_block == 0) {
            l1_block = block_alloc();
            l2_table[l2_idx] = l1_block;
            block_rw(l2_block, data_buf, true);

            memset(data_buf, 0, BLOCK_SIZE);
            block_rw(l1_block, data_buf, true);
        }

        // 读取一级索引块，拿到数据块
        block_rw(l1_block, data_buf, false);
        unsigned int *l1_table = (unsigned int *)data_buf;
        unsigned int data_block = l1_table[l1_off];
        if (data_block == 0) {
            data_block = block_alloc();
            l1_table[l1_off] = data_block;
            block_rw(l1_block, data_buf, true);
        }
        return data_block;
    }

    printf("inode_append: data len out of space!\n");
    exit(1);
}

/* inode初始化 */
void inode_init(inode_disk_t *ip, short type, short major, short minor)
{
    ip->type = type;
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    ip->size = 0;
    for (int i = 0; i < 13; i++)
        ip->index[i] = 0;
}

/* 对inode管理的数据做追加写 */
void inode_append(inode_disk_t *ip, void *data, unsigned int len)
{
    unsigned int old_blocks, new_blocks;
    unsigned int cut_len, tar_len; 
    unsigned int tmp, offset;
    char *data_new = (char*)data; 
    
    old_blocks = COUNT_BLOCKS(ip->size, BLOCK_SIZE);
    new_blocks = COUNT_BLOCKS(ip->size + len, BLOCK_SIZE);
    tmp = ip->size / BLOCK_SIZE;
    tar_len = len;

    /* 如果有必要, 扩充block空间（通过映射函数分配必要的数据块/索引块） */
    if (new_blocks > old_blocks) {
        for (int i = old_blocks; i < new_blocks; i++) {
            (void)inode_get_or_alloc_block(ip, (unsigned int)i);
        }
    }

    /* 分段写入各个block */
    while (len > 0)
    {
        unsigned int logical_block = ip->size / BLOCK_SIZE;
        unsigned int data_block = inode_get_or_alloc_block(ip, logical_block);

        // 读出旧块（如果是追加到已有块内），否则新块从全0开始
        offset = ip->size % BLOCK_SIZE;
        if (offset != 0) {
            block_rw(data_block, data_buf, false);
        } else {
            memset(data_buf, 0, BLOCK_SIZE);
        }

        cut_len = MIN(BLOCK_SIZE - offset, len);
        memcpy(data_buf + offset, data_new, cut_len);
        block_rw(data_block, data_buf, true);

        len -= cut_len;
        data_new += cut_len;
        tmp++;
    }
    ip->size += tar_len;
}

/* /aaaa/bbb/ccc.elf -> ccc */
void get_name_from_path(char *path, char *name)
{
    // 1. 找到最后一个 '/' 之后的部分（basename）
    char *basename = path;
    char *p = strrchr(path, '/');
    if (p != NULL) {
        basename = p + 1;
    }

    // 处理路径为 "/" 或 "//" 等情况
    if (*basename == '\0')
        *name = '\0';

    // 2. 找到 basename 中第一个 '.'
    char *dot = strchr(basename, '.');
    int len;
    if (dot != NULL) {
        len = dot - basename;
        memcpy(name, basename, len);
    } else {
        len = strlen(basename);
        memcpy(name, basename, len);
    }
    name[len] = '\0';
}

int main(int argc, char* argv[])
{
    assert(BLOCK_SIZE % sizeof(inode_disk_t) == 0);

    unsigned int inode_num[ELF_MAXARGS];
    inode_disk_t inode[ELF_MAXARGS];
    dentry_t dentry[ELF_MAXARGS];
    
    char name[MAXLEN_FILENAME];
    char buf[BLOCK_SIZE];
    unsigned int read_len, total_len;
    int fd;


	/* step-1: 填充 superblock 结构体 */
	sb.magic_num = FS_MAGIC;
	sb.block_size = BLOCK_SIZE;
	sb.inode_bitmap_firstblock = 1;
	sb.inode_bitmap_blocks = COUNT_BLOCKS(N_INODE, BIT_PER_BLOCK);
	sb.inode_firstblock = sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks;
	sb.inode_blocks = COUNT_BLOCKS(N_INODE, INODE_PER_BLOCK);
	sb.data_bitmap_firstblock = sb.inode_firstblock + sb.inode_blocks;
	sb.data_bitmap_blocks = COUNT_BLOCKS(N_DATA_BLOCK, BIT_PER_BLOCK);
	sb.data_firstblock = sb.data_bitmap_firstblock + sb.data_bitmap_blocks;
	sb.data_blocks = N_DATA_BLOCK;
    sb.total_inodes = N_INODE;
	sb.total_blocks = 1 + sb.inode_bitmap_blocks + sb.inode_blocks
			+ sb.data_bitmap_blocks + sb.data_blocks;

    /* step-2: 创建磁盘文件 */
    disk_fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
    if(disk_fd < 0) {
        perror(argv[1]);
        exit(1);
    }

    /* step-3: 准备一个清零的磁盘映像 */
    memset(data_buf, 0, BLOCK_SIZE);
    printf("\nPreparing disk.img...\n\n");
    for(int i = 0; i < sb.total_blocks; i++)
        block_rw(i, data_buf, true);

    /* step-4: 制作根目录 */
    inode_num[0] = inode_alloc();
    if (inode_num[0] != ROOT_INODE_NUM) {
        printf("invalid inode[i]_num = %u\n", inode_num[0]);
        return -1;
    }
    inode_init(&inode[0], INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);

    dentry[0].inode_num = xint(inode_num[0]);
    dentry[1].inode_num = xint(inode_num[0]);
    strcpy(dentry[0].name, ".");
    strcpy(dentry[1].name, "..");
    inode_append(&inode[0], dentry, sizeof(dentry_t) * 2);

    /* step-5: 在根目录下载入可执文件 */
    for (int i = 2; i < argc; i++)
    {
        // 获取文件名
        get_name_from_path(argv[i], name);

        // 申请inode + 初始化inode + 准备dentry
        inode_num[i] = inode_alloc();
        inode_init(&inode[i], INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
        dentry[i].inode_num = xint(inode_num[i]);
        strcpy(dentry[i].name, name);
    }
    inode_append(&inode[0], dentry + 2, sizeof(dentry_t) * (argc - 2));

    /* step-6: 填充文件内容 */
    for (int i = 2; i < argc; i++)
    {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            perror(argv[i]);
            exit(1);
        }

        total_len = 0;
        while (1)
        {
            read_len = read(fd, buf, BLOCK_SIZE);
            if (read_len == -1) {
                perror("read fail!\n");
                exit(1);
            }

            total_len += read_len;
            inode_append(&inode[i], buf, read_len);
            if (read_len < BLOCK_SIZE)
                break;
        }
        
        close(fd);
    }

    /* step-7: 写回super block和inode */
    sb.magic_num = xint(sb.magic_num);
    sb.block_size = xint(sb.block_size);
    sb.inode_bitmap_blocks = xint(sb.inode_bitmap_blocks);
    sb.inode_bitmap_firstblock = xint(sb.inode_bitmap_firstblock);
    sb.inode_blocks = xint(sb.inode_blocks);
    sb.inode_firstblock = xint(sb.inode_firstblock);
    sb.data_bitmap_blocks = xint(sb.data_bitmap_blocks);
    sb.data_bitmap_firstblock = xint(sb.data_bitmap_firstblock);
    sb.data_blocks = xint(sb.data_blocks);
    sb.data_firstblock = xint(sb.data_firstblock);
    sb.total_inodes = xint(sb.total_inodes);
    sb.total_blocks = xint(sb.total_blocks);
    memcpy(data_buf, &sb, sizeof(sb));
    block_rw(0, data_buf, true);

    for (int i = 0; i < argc; i++) {
        if (i == 1) continue; // 这个inode被跳过
        inode[i].type = xshort(inode[i].type);
        inode[i].major = xshort(inode[i].major);
        inode[i].minor = xshort(inode[i].minor);
        inode[i].nlink = xshort(inode[i].nlink);
        inode[i].size = xint(inode[i].size);
        for(int j = 0; j < INODE_INDEX_3; j++)
            inode[i].index[j] = xint(inode[i].index[j]);
        inode_rw(inode_num[i], &inode[i], true);
    }

    /* step-8: 关闭磁盘文件 */
    close(disk_fd);

    return 0;
}