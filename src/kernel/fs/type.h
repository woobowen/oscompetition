#pragma once
#include "../arch/type.h"
#include "../mem/type.h"

/*----------------------关于磁盘----------------------*/

#define VIRTIO_BASE 0x10001000
#define VIRTIO_IRQ 1

#define R(r) ((volatile uint32 *)(VIRTIO_BASE + (r)))

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1

#define VIRTIO_NUM 8

typedef struct vring_desc {
    uint64 addr;
    uint32 len;
    uint16 flags;
    uint16 next;
} vring_desc_t;

typedef struct vring_used_elem {
    uint32 id;
    uint32 len;
} vring_used_elem_t;

typedef struct used_area {
    uint16 flags;
    uint16 id;
    vring_used_elem_t elems[VIRTIO_NUM];
} used_area_t;

typedef struct disk {
    // 驱动需要8KB的连续空间, 不适合用pmem_alloc来申请
    // 所以直接定义在这里
    char pages[2 * PGSIZE];
    
    vring_desc_t *desc;
    used_area_t *used;
    uint16 *avail;
    char free[VIRTIO_NUM];
    uint16 used_idx;
    struct
    {
        struct buffer *b;
        char status;
    } info[VIRTIO_NUM];
    spinlock_t vdisk_lock;  
} disk_t;

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c
#define VIRTIO_MMIO_QUEUE_PFN 0x040
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070

#define VIRTIO_CONFIG_S_ACKNOWLEDGE 1
#define VIRTIO_CONFIG_S_DRIVER 2
#define VIRTIO_CONFIG_S_DRIVER_OK 4
#define VIRTIO_CONFIG_S_FEATURES_OK 8

#define VIRTIO_BLK_F_RO 5
#define VIRTIO_BLK_F_SCSI 7  
#define VIRTIO_BLK_F_CONFIG_WCE 11
#define VIRTIO_BLK_F_MQ 12
#define VIRTIO_F_ANY_LAYOUT 27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX 29

/*-------------------关于块缓冲区--------------------*/

#define BLOCK_SIZE 4096              // 基本管理单位的大小
#define N_BUFFER_TEST 8              // 测试时的N_BUFFER取值
#define N_BUFFER (32 * 512)          // 最多可以用32MB内存空间(25%)作为Block缓冲区
#define BLOCK_NUM_UNUSED 0xFFFFFFFF  // 未使用的Buffer需要将block_num设为这个值

/* 以Block为单位在内存和磁盘间传递数据 */
typedef struct buffer {
    /*
        锁的说明:
        1. block_num和ref由全局的自旋锁lk_buf_cache保护
        2. data和disk由内部的睡眠锁slk保护
    */
    uint32 block_num;                // buffer对应的磁盘内block序号 
    uint32 ref;                      // 引用数 (该buffer被get的次数)
    sleeplock_t slk;                 // 睡眠锁
    uint8* data;                     // block数据(大小为BLOCK_SIZE)
    bool disk;                       // 在virtio.c中使用
} buffer_t;

/* 将buffer这种数据结构包装成资源节点 */
typedef struct buffer_node {
    buffer_t buf;                     // 资源
    struct buffer_node *next;         // 链接
    struct buffer_node *prev;         // 链接
} buffer_node_t;

/*-------------------关于文件系统--------------------*/

#define FS_MAGIC 0x12341234                 // 魔数
#define FS_SB_BLOCK 0                       // 超级块的序号
#define EXT4_SUPER_OFFSET 1024
#define EXT4_SUPER_MAGIC 0xEF53

/* 超级块 */
typedef struct super_block {
    unsigned int magic_num;                  // 用于标识文件系统类型
    unsigned int block_size;                 // 基本存储单位的大小 (字节)
    unsigned int total_blocks;               // 总共的块数量
    unsigned int total_inodes;               // 总共的inode数量

    unsigned int inode_bitmap_firstblock;    // inode_bitmap区域的起始块号
	unsigned int inode_bitmap_blocks;        // inode_bitmap区域的块数量
	unsigned int inode_firstblock;           // inode区域的起始块号
    unsigned int inode_blocks;               // inode区域的块数量
	unsigned int data_bitmap_firstblock;     // data_bitmap区域的起始块号
	unsigned int data_bitmap_blocks;         // data_bitmap区域的块数量
	unsigned int data_firstblock;            // data区域的起始块号
    unsigned int data_blocks;                // data区域的块数量
} super_block_t;

typedef struct ext4_super_preview {
    uint32 inodes_count;
    uint32 blocks_count_lo;
    uint32 r_blocks_count_lo;
    uint32 free_blocks_count_lo;
    uint32 free_inodes_count;
    uint32 first_data_block;
    uint32 log_block_size;
    uint32 log_cluster_size;
    uint32 blocks_per_group;
    uint32 clusters_per_group;
    uint32 inodes_per_group;
    uint32 mtime;
    uint32 wtime;
    uint16 mnt_count;
    uint16 max_mnt_count;
    uint16 magic;
    uint16 state;
    uint16 errors;
    uint16 minor_rev_level;
    uint32 lastcheck;
    uint32 checkinterval;
    uint32 creator_os;
    uint32 rev_level;
    uint16 def_resuid;
    uint16 def_resgid;
    uint32 first_ino;
    uint16 inode_size;
    uint16 block_group_nr;
    uint32 feature_compat;
    uint32 feature_incompat;
    uint32 feature_ro_compat;
    uint8  uuid[16];
    char   volume_name[16];
    char   last_mounted[64];
    uint32 algorithm_usage_bitmap;
    uint8  prealloc_blocks;
    uint8  prealloc_dir_blocks;
    uint16 reserved_gdt_blocks;
    uint8  journal_uuid[16];
    uint32 journal_inum;
    uint32 journal_dev;
    uint32 last_orphan;
    uint32 hash_seed[4];
    uint8  def_hash_version;
    uint8  jnl_backup_type;
    uint16 desc_size;
    uint32 default_mount_opts;
    uint32 first_meta_bg;
    uint32 mkfs_time;
    uint32 jnl_blocks[17];
    uint32 blocks_count_hi;
    uint32 r_blocks_count_hi;
    uint32 free_blocks_count_hi;
    uint16 min_extra_isize;
    uint16 want_extra_isize;
    uint32 flags;
    uint16 raid_stride;
    uint16 mmp_interval;
    uint64 mmp_block;
    uint32 raid_stripe_width;
    uint8  log_groups_per_flex;
    uint8  checksum_type;
    uint16 reserved_pad;
    uint64 kbytes_written;
    uint32 snapshot_inum;
    uint32 snapshot_id;
    uint64 snapshot_r_blocks_count;
    uint32 snapshot_list;
    uint32 error_count;
    uint32 first_error_time;
    uint32 first_error_ino;
    uint64 first_error_block;
    uint8  first_error_func[32];
    uint32 first_error_line;
    uint32 last_error_time;
    uint32 last_error_ino;
    uint32 last_error_line;
    uint64 last_error_block;
    uint8  last_error_func[32];
    uint8  mount_opts[64];
    uint32 usr_quota_inum;
    uint32 grp_quota_inum;
    uint32 overhead_clusters;
    uint32 backup_bgs[2];
    uint8  encrypt_algos[4];
    uint8  encrypt_pw_salt[16];
    uint32 lpf_ino;
    uint32 prj_quota_inum;
    uint32 checksum_seed;
    uint8  wtime_hi;
    uint8  mtime_hi;
    uint8  mkfs_time_hi;
    uint8  lastcheck_hi;
    uint8  first_error_time_hi;
    uint8  last_error_time_hi;
    uint8  first_error_errcode;
    uint8  last_error_errcode;
    uint16 encoding;
    uint16 encoding_flags;
    uint32 orphan_file_inum;
} ext4_super_preview_t;

typedef struct ext4_group_desc {
    uint32 block_bitmap_lo;
    uint32 inode_bitmap_lo;
    uint32 inode_table_lo;
    uint16 free_blocks_count_lo;
    uint16 free_inodes_count_lo;
    uint16 used_dirs_count_lo;
    uint16 flags;
    uint32 exclude_bitmap_lo;
    uint16 block_bitmap_csum_lo;
    uint16 inode_bitmap_csum_lo;
    uint16 itable_unused_lo;
    uint16 checksum;
    uint32 block_bitmap_hi;
    uint32 inode_bitmap_hi;
    uint32 inode_table_hi;
    uint16 free_blocks_count_hi;
    uint16 free_inodes_count_hi;
    uint16 used_dirs_count_hi;
    uint16 itable_unused_hi;
    uint32 exclude_bitmap_hi;
    uint16 block_bitmap_csum_hi;
    uint16 inode_bitmap_csum_hi;
    uint32 reserved;
} ext4_group_desc_t;

typedef struct ext4_inode_disk {
    uint16 mode;
    uint16 uid_lo;
    uint32 size_lo;
    uint32 atime;
    uint32 ctime;
    uint32 mtime;
    uint32 dtime;
    uint16 gid_lo;
    uint16 links_count;
    uint32 blocks_lo;
    uint32 flags;
    uint32 osd1;
    uint8  block[60];
    uint32 generation;
    uint32 file_acl_lo;
    uint32 size_high;
    uint32 obso_faddr;
    uint8  osd2[12];
} ext4_inode_disk_t;

typedef struct ext4_extent_header {
    uint16 magic;
    uint16 entries;
    uint16 max;
    uint16 depth;
    uint32 generation;
} ext4_extent_header_t;

typedef struct ext4_extent_idx {
    uint32 block;
    uint32 leaf_lo;
    uint16 leaf_hi;
    uint16 unused;
} ext4_extent_idx_t;

typedef struct ext4_extent {
    uint32 block;
    uint16 len;
    uint16 start_hi;
    uint32 start_lo;
} ext4_extent_t;

typedef struct ext4_dirent {
    uint32 inode;
    uint16 rec_len;
    uint8  name_len;
    uint8  file_type;
    char   name[];
} ext4_dirent_t;

#define EXT4_INODE_MODE_FIFO      0x1000
#define EXT4_INODE_MODE_CHR       0x2000
#define EXT4_INODE_MODE_DIR       0x4000
#define EXT4_INODE_MODE_BLK       0x6000
#define EXT4_INODE_MODE_REG       0x8000
#define EXT4_INODE_MODE_LNK       0xA000
#define EXT4_INODE_MODE_SOCK      0xC000
#define EXT4_INODE_MODE_MASK      0xF000

#define EXT4_EXTENTS_FL           0x00080000
#define EXT4_EXT_MAGIC            0xF30A
#define EXT4_ROOT_INO             2
#define EXT4_NAME_LEN             255
#define EXT4_FT_UNKNOWN           0
#define EXT4_FT_REG_FILE          1
#define EXT4_FT_DIR               2

#define EXT4_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT4_RO_COMPAT_LARGE_FILE   0x0002
#define EXT4_RO_COMPAT_BTREE_DIR    0x0004
#define EXT4_RO_COMPAT_HUGE_FILE    0x0008
#define EXT4_RO_COMPAT_GDT_CSUM     0x0010
#define EXT4_RO_COMPAT_DIR_NLINK    0x0020
#define EXT4_RO_COMPAT_EXTRA_ISIZE  0x0040
#define EXT4_RO_COMPAT_QUOTA        0x0100
#define EXT4_RO_COMPAT_BIGALLOC     0x0200
#define EXT4_RO_COMPAT_METADATA_CSUM 0x0400
#define EXT4_RO_COMPAT_READONLY     0x1000
#define EXT4_RO_COMPAT_PROJECT      0x2000

#define EXT4_INCOMPAT_COMPRESSION  0x0001
#define EXT4_INCOMPAT_FILETYPE     0x0002
#define EXT4_INCOMPAT_RECOVER      0x0004
#define EXT4_INCOMPAT_JOURNAL_DEV  0x0008
#define EXT4_INCOMPAT_META_BG      0x0010
#define EXT4_INCOMPAT_EXTENTS      0x0040
#define EXT4_INCOMPAT_64BIT        0x0080
#define EXT4_INCOMPAT_MMP          0x0100
#define EXT4_INCOMPAT_FLEX_BG      0x0200
#define EXT4_INCOMPAT_EA_INODE     0x0400
#define EXT4_INCOMPAT_DIRDATA      0x1000
#define EXT4_INCOMPAT_CSUM_SEED    0x2000
#define EXT4_INCOMPAT_LARGEDIR     0x4000
#define EXT4_INCOMPAT_INLINE_DATA  0x8000
#define EXT4_INCOMPAT_ENCRYPT      0x10000

typedef struct ext4_info {
    bool active;
    uint32 block_size;
    uint32 blocks_per_group;
    uint32 inodes_per_group;
    uint32 inode_size;
    uint32 first_data_block;
    uint32 desc_size;
    uint32 groups_count;
    uint64 blocks_count;
    uint32 unsupported_incompat;
    uint32 unsupported_ro_compat;
    ext4_super_preview_t sb;
} ext4_info_t;

#define INODE_TYPE_DATA       0              // inode管理无结构的流式数据
#define INODE_TYPE_DIR        1              // inode管理结构化的目录数据
#define INODE_TYPE_DIVICE     2              // inode对应虚拟设备(不管理数据)

/* major和minor的可能取值 */
#define INODE_MAJOR_DEFAULT   1              // 默认的主设备号 (不属于设备文件)
#define INODE_MAJOR_STDIN     2              // 常规设备文件 (/dev/stdin, 可读)
#define INODE_MAJOR_STDOUT    3              // 常规设备文件 (/dev/stdout, 可写)
#define INODE_MAJOR_STDERR    4              // 参观设备文件 (/dev/stderr, 可写)
#define INODE_MAJOR_ZERO      5              // 特殊设备文件 (/dev/zero, 可读)
#define INODE_MAJOR_NULL      6              // 特殊设备文件 (/dev/null, 可读可写)
#define INODE_MAJOR_GPT0      7              // 特殊设备文件 (/dev/gpt0, 可写)
#define INODE_MAJOR_RTC       8              // virtual RTC (/dev/rtc, /dev/rtc0)
#define INODE_MINOR_DEFAULT   1              // 默认的次设备号 (所有文件都使用它)

/* index字段相关 */
#define INODE_INDEX_1        (10)                 // 直接映射 (10个格子)
#define INODE_INDEX_2        (10+2)               // 一级间接映射 (2个格子)
#define INODE_INDEX_3        (10+2+1)             // 二级间接映射 (1个格子)
#define INODE_BLOCK_INDEX_1  (10)                 // 直接映射 (40KB)
#define INODE_BLOCK_INDEX_2  (10+2048)            // 一级间接映射 (40KB + 8MB)
#define INODE_BLOCK_INDEX_3  (10+2048+1024*1024)  // 二级间接映射 (40KB + 8MB + 4GB)

/* 最大的size */
#define INODE_MAX_SIZE (0xFFFFFFFF)

/* 磁盘上的索引节点(64 Byte) */
typedef struct inode_disk {
    short type;                          // 文件类型
    short major;                         // 主设备号
    short minor;                         // 次设备号
    short nlink;                         // 链接数
    unsigned int size;                   // 文件数据长度(字节)
    unsigned int index[INODE_INDEX_3];   // 数据存储位置(10+2+1)
} inode_disk_t;

#define ROOT_INODE  0                 // 根节点的序号
#define N_INODE     64                // 内存中最多存在的inode数量

/* 内存里的索引节点 */
typedef struct inode {
    inode_disk_t disk_info;           // 持久化信息 (slk保护)
    bool valid_info;                  // disk_info的有效性 (slk保护)
    uint32 inode_num;                 // inode序号 (slk保护)
    uint32 ref;                       // 引用数 (lk_inode_cache保护)
    sleeplock_t slk;                  // 睡眠锁
} inode_t;

#define MAXLEN_FILENAME 60            // 文件名的最大长度
#define INVALID_INODE_NUM 0xFFFFFFFF  // 无效inode_num

/* 目录项(64 Byte) */
typedef struct dentry {
    char name[MAXLEN_FILENAME];       // 文件名
    unsigned int inode_num;           // 索引节点序号
} dentry_t;

/* 辅助计算 */
#define BIT_PER_BYTE 8
#define BIT_PER_BLOCK (BLOCK_SIZE * BIT_PER_BYTE)
#define INODE_PER_BLOCK (BLOCK_SIZE / sizeof(inode_disk_t))
#define DENTRY_PER_BLOCK  (BLOCK_SIZE / sizeof(dentry_t))
#define COUNT_BLOCKS(ele_num, ele_per_block)  (((ele_num) + (ele_per_block) - 1) / (ele_per_block)) 


#define FILE_OPEN_CREATE 0x01  // 打开文件时, 若文件不存在则创建新的
#define FILE_OPEN_READ   0x02  // 打开文件时, 要求文件可读
#define FILE_OPEN_WRITE  0x04  // 打开文件时, 要求文件可写
#define FILE_OPEN_APPEND 0x08  // append writes at end
#define FILE_OPEN_TRUNC  0x10  // truncate existing file on open

#define FILE_LSEEK_SET   0     // file->offset = lseek_offset
#define FILE_LSEEK_ADD   1     // file->offset += lseek_offset
#define FILE_LSEEK_SUB   2     // file->offset -= lseek_offset

#define PIPE_SIZE 4096
#define N_PIPE 256
typedef struct pipe {
    spinlock_t lk;
    char data[PIPE_SIZE];
    uint32 nread;      // 已读字节数(单调递增)
    uint32 nwrite;     // 已写字节数(单调递增)
    int readopen;      // 读端是否打开
    int writeopen;     // 写端是否打开
    int used;          // 槽位占用
} pipe_t;

typedef struct file {
    inode_t *ip;        // 对应的inode
    bool is_device;     // 是否为虚拟设备文件(不依赖磁盘inode)
    uint16 dev_major;   // 虚拟设备主设备号
    bool is_proc;        // in-memory /proc node
    uint16 proc_kind;    // proc node type
    int proc_pid;        // pid for /proc/<pid> nodes
    bool is_mem;         // small writable in-memory overlay node
    int mem_index;       // mem overlay table index
    bool readable;      // 是否可读
    bool writbale;      // 是否可写
    uint32 offset;      // 读/写指针的偏移量
    uint32 ref;         // 引用数 (lk_file_table保护)
    bool is_pipe;       // 是否为管道
    struct pipe *pipe;  // 管道对象(is_pipe 时有效)
} file_t;

#define N_FILE 1024      // file_table中file的数量

typedef struct file_stat {
    uint16 type;        // inode_disk->type
    uint16 nlink;       // inode_disk->nlink
    uint32 size;        // inode_disk->size
    uint32 inode_num;   // inode->inode_num
    uint32 offset;      // file->offset
} file_stat_t;

typedef struct device {
    char name[MAXLEN_FILENAME];                                 // 设备名称
    uint32 (*read)(uint32 len, uint64 dst, bool is_user_dst);   // 设备读操作函数
    uint32 (*write)(uint32 len, uint64 src, bool is_user_src);  // 设备写操作函数
} device_t;

#define N_DEVICE 16     // device_table中device的数量

