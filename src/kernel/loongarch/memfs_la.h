/*
 * memfs_la.h — Minimal in-memory writable filesystem for LoongArch.
 *
 * Provides a simple flat file store that supports create, write, read,
 * unlink, mkdir, rmdir, and directory listing.  Mounted alongside the
 * read-only ext4/SeaFS; paths under /tmp/ (or created via O_CREAT) are
 * routed here so test scripts can write temporary files.
 *
 * Data pages are allocated from the kernel physical-page allocator
 * (la_pmem_alloc/free) on demand.
 */

#ifndef SEAOS_LOONGARCH_MEMFS_H
#define SEAOS_LOONGARCH_MEMFS_H

#include <stdint.h>

/* ---- Configuration ---- */
/* Logical inode cap for lmbench lat_fs.  Backing inode pages are allocated
 * lazily from high memory by memfs_la.c, so this does not become a static
 * low-memory BSS cost. */
#define MEMFS_MAX_INODES      3145728
#define MEMFS_HASH_BUCKETS    262144
#define MEMFS_MAX_NAME        256
#define MEMFS_INLINE_NAME     64
#define MEMFS_PAGES_PER_FILE  2048  /* 8 MB max per file (covers fstime up to 32 MB across 4 files) */
#define MEMFS_PAGE_PTRS_PER_TABLE 512
#define MEMFS_PAGE_TABLES \
    ((MEMFS_PAGES_PER_FILE + MEMFS_PAGE_PTRS_PER_TABLE - 1) / MEMFS_PAGE_PTRS_PER_TABLE)

/* Inode types */
#define MEMFS_TYPE_FREE  0
#define MEMFS_TYPE_FILE  1
#define MEMFS_TYPE_DIR   2
#define MEMFS_TYPE_SYMLINK 3

/* Special inode indices */
#define MEMFS_ROOT_INO    0

/*
 * memfs inode.
 *
 * For FILES  — data pages hold file content.
 * For DIRS   — no data pages; children are found by path prefix match.
 */
struct memfs_inode {
    int      type;                        /* MEMFS_TYPE_* */
    char     path_inline[MEMFS_INLINE_NAME]; /* short absolute path */
    char     *path_extra;                 /* one-page storage for long path */
    uint32_t size;                        /* file size in bytes */
    uint32_t mode;                        /* Linux mode bits including file type */
    uint64_t atime_sec;
    uint64_t mtime_sec;
    uint64_t ctime_sec;
    int      hash_next;
    void    *page_tables[MEMFS_PAGE_TABLES]; /* lazily allocated data-page pointer tables */
    void     *small_data;                 /* compact storage for <=1 KiB files */
    uint32_t small_size;
    uint32_t small_slot;
};

/* ---- Public API ---- */
void     memfs_init(void);
int      memfs_lookup(const char *path);        /* returns inode idx, or -1 */
int      memfs_create(const char *path, int type); /* returns inode idx, or -1 */
int      memfs_symlink(const char *target, const char *link_path);
int      memfs_readlink(int ino, char *dst, uint32_t size);
int      memfs_write(int ino, uint32_t offset,
                     const void *buf, uint32_t len);
int      memfs_read(int ino, uint32_t offset,
                    void *buf, uint32_t len);
int      memfs_delete(const char *path);        /* unlink / rmdir */
int      memfs_rename(const char *old_path, const char *new_path);
int      memfs_truncate(int ino);              /* reset file size to 0, free all data pages */
int      memfs_unlink_inode(int ino);          /* remove name, keep open file data */
int      memfs_reclaim_inode(int ino);         /* free unnamed inode data */
int      memfs_is_unlinked(int ino);           /* path removed while fd open */
int      memfs_getdents(int dir_ino, void *buf, uint32_t len);
int      memfs_inode_type(int ino);
uint32_t memfs_inode_size(int ino);
uint64_t memfs_inode_atime(int ino);
uint64_t memfs_inode_mtime(int ino);
uint64_t memfs_inode_ctime(int ino);
uint32_t memfs_inode_mode(int ino);
int      memfs_chmod(int ino, uint32_t mode);
int      memfs_set_times(int ino, uint64_t atime_sec, uint64_t mtime_sec);

/* Helper: does `path` start with the given prefix? */
int      memfs_path_prefix(const char *path, const char *prefix);

/* Return the stored absolute path of a memfs inode (for cwd resolution). */
const char *memfs_get_path(int ino);

#endif
