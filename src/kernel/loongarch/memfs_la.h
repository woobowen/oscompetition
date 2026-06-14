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
#define MEMFS_MAX_INODES      128
#define MEMFS_MAX_NAME        256
#define MEMFS_PAGES_PER_FILE  16    /* 64 KB max per file */

/* Inode types */
#define MEMFS_TYPE_FREE  0
#define MEMFS_TYPE_FILE  1
#define MEMFS_TYPE_DIR   2

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
    char     path[MEMFS_MAX_NAME];        /* full absolute path */
    uint32_t size;                        /* file size in bytes */
    void    *pages[MEMFS_PAGES_PER_FILE]; /* data pages (NULL = unallocated) */
};

/* ---- Public API ---- */
void     memfs_init(void);
int      memfs_lookup(const char *path);        /* returns inode idx, or -1 */
int      memfs_create(const char *path, int type); /* returns inode idx, or -1 */
int      memfs_write(int ino, uint32_t offset,
                     const void *buf, uint32_t len);
int      memfs_read(int ino, uint32_t offset,
                    void *buf, uint32_t len);
int      memfs_delete(const char *path);        /* unlink / rmdir */
int      memfs_getdents(int dir_ino, void *buf, uint32_t len);
uint32_t memfs_inode_size(int ino);

/* Helper: does `path` start with the given prefix? */
int      memfs_path_prefix(const char *path, const char *prefix);

#endif
