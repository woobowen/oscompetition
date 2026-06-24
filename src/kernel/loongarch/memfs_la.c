/*
 * memfs_la.c — In-memory writable filesystem implementation.
 *
 * Flat namespace indexed by full path string.  Data pages are allocated
 * on demand from the kernel pmem allocator and freed on file deletion.
 */
#include "early_boot.h"
#include "memfs_la.h"

/* ---- Inode table ---- */
static struct memfs_inode memfs_inodes[MEMFS_MAX_INODES];

/* ---- Helpers ---- */

static uint64_t memfs_now_sec(void)
{
    uint64_t now = la_timer_get_ticks() / 100;
    return now ? now : 1;
}

/* Match two NUL-terminated strings, return 1 on match. */
static int memfs_streq(const char *a, const char *b)
{
    int i;
    for (i = 0; a[i] && b[i]; i++)
        if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

/* ---- Public API ---- */

void memfs_init(void)
{
    for (int i = 0; i < MEMFS_MAX_INODES; i++) {
        memfs_inodes[i].type = MEMFS_TYPE_FREE;
        memfs_inodes[i].path[0] = '\0';
        memfs_inodes[i].size = 0;
        memfs_inodes[i].atime_sec = 0;
        memfs_inodes[i].mtime_sec = 0;
        memfs_inodes[i].ctime_sec = 0;
        for (int j = 0; j < MEMFS_PAGES_PER_FILE; j++)
            memfs_inodes[i].pages[j] = 0;
    }

    /* Create root directory */
    struct memfs_inode *root = &memfs_inodes[MEMFS_ROOT_INO];
    root->type = MEMFS_TYPE_DIR;
    root->path[0] = '/';
    root->path[1] = '\0';
    root->size = 0;
    root->atime_sec = root->mtime_sec = root->ctime_sec = memfs_now_sec();

    la_uart_puts("  memfs: initialized (");
    la_uart_put_hex(MEMFS_MAX_INODES);
    la_uart_puts(" inodes)\n");
}

int memfs_lookup(const char *path)
{
    for (int i = 0; i < MEMFS_MAX_INODES; i++) {
        if (memfs_inodes[i].type == MEMFS_TYPE_FREE)
            continue;
        if (memfs_streq(memfs_inodes[i].path, path))
            return i;
    }
    return -1;
}

int memfs_create(const char *path, int type)
{
    /* Find a free inode */
    int idx = -1;
    for (int i = 0; i < MEMFS_MAX_INODES; i++) {
        if (memfs_inodes[i].type == MEMFS_TYPE_FREE) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        la_uart_puts("  memfs: out of inodes\n");
        return -1;
    }

    /* Copy path */
    int i;
    for (i = 0; path[i] && i < MEMFS_MAX_NAME - 1; i++)
        memfs_inodes[idx].path[i] = path[i];
    memfs_inodes[idx].path[i] = '\0';

    memfs_inodes[idx].type = type;
    memfs_inodes[idx].size = 0;
    memfs_inodes[idx].atime_sec = memfs_now_sec();
    memfs_inodes[idx].mtime_sec = memfs_inodes[idx].atime_sec;
    memfs_inodes[idx].ctime_sec = memfs_inodes[idx].atime_sec;
    for (int j = 0; j < MEMFS_PAGES_PER_FILE; j++)
        memfs_inodes[idx].pages[j] = 0;

    return idx;
}

int memfs_write(int ino, uint32_t offset, const void *buf, uint32_t len)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = &memfs_inodes[ino];
    if (inode->type != MEMFS_TYPE_FILE) return -1;

    uint32_t end_off = offset + len;
    if (end_off > MEMFS_PAGES_PER_FILE * LA_PGSIZE) {
        /* File too large — truncate */
        len = (MEMFS_PAGES_PER_FILE * LA_PGSIZE) - offset;
        if (len == 0) return 0;
    }

    const uint8_t *src = (const uint8_t *)buf;
    uint32_t written = 0;

    while (written < len) {
        uint32_t cur_off = offset + written;
        uint32_t page_idx = cur_off / LA_PGSIZE;
        uint32_t page_off = cur_off % LA_PGSIZE;
        uint32_t chunk = LA_PGSIZE - page_off;
        if (chunk > len - written) chunk = len - written;

        /* Allocate page on demand */
        if (!inode->pages[page_idx]) {
            void *pg = la_pmem_alloc();
            if (!pg) {
                la_uart_puts("  memfs: out of memory\n");
                break;
            }
            /* Zero the page */
            uint8_t *zp = (uint8_t *)pg;
            for (int z = 0; z < LA_PGSIZE; z++) zp[z] = 0;
            inode->pages[page_idx] = pg;
        }

        uint8_t *dst = (uint8_t *)inode->pages[page_idx];
        for (uint32_t j = 0; j < chunk; j++)
            dst[page_off + j] = src[written + j];
        written += chunk;
    }

    /* Update file size */
    uint32_t new_end = offset + written;
    if (new_end > inode->size) inode->size = new_end;
    if (written > 0) {
        inode->mtime_sec = memfs_now_sec();
        inode->ctime_sec = inode->mtime_sec;
    }

    return (int)written;
}

int memfs_read(int ino, uint32_t offset, void *buf, uint32_t len)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = &memfs_inodes[ino];
    if (inode->type != MEMFS_TYPE_FILE) return -1;

    if (offset >= inode->size) return 0;
    if (offset + len > inode->size)
        len = inode->size - offset;

    uint8_t *dst = (uint8_t *)buf;
    uint32_t done = 0;

    while (done < len) {
        uint32_t cur_off = offset + done;
        uint32_t page_idx = cur_off / LA_PGSIZE;
        uint32_t page_off = cur_off % LA_PGSIZE;
        uint32_t chunk = LA_PGSIZE - page_off;
        if (chunk > len - done) chunk = len - done;

        if (!inode->pages[page_idx]) break; /* shouldn't happen */
        uint8_t *src = (uint8_t *)inode->pages[page_idx];
        for (uint32_t j = 0; j < chunk; j++)
            dst[done + j] = src[page_off + j];
        done += chunk;
    }
    if (done > 0)
        inode->atime_sec = memfs_now_sec();
    return (int)done;
}

int memfs_truncate(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = &memfs_inodes[ino];
    if (inode->type != MEMFS_TYPE_FILE) return -1;

    /* Free all data pages */
    for (int j = 0; j < MEMFS_PAGES_PER_FILE; j++) {
        if (inode->pages[j]) {
            la_pmem_free(inode->pages[j]);
            inode->pages[j] = 0;
        }
    }
    inode->size = 0;
    inode->mtime_sec = memfs_now_sec();
    inode->ctime_sec = inode->mtime_sec;
    return 0;
}

int memfs_unlink_inode(int ino)
{
    if (ino <= MEMFS_ROOT_INO || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = &memfs_inodes[ino];
    if (inode->type == MEMFS_TYPE_FREE) return -1;
    inode->path[0] = '\0';
    return 0;
}

int memfs_reclaim_inode(int ino)
{
    if (ino <= MEMFS_ROOT_INO || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = &memfs_inodes[ino];
    if (inode->type == MEMFS_TYPE_FREE) return -1;

    for (int j = 0; j < MEMFS_PAGES_PER_FILE; j++) {
        if (inode->pages[j]) {
            la_pmem_free(inode->pages[j]);
            inode->pages[j] = 0;
        }
    }

    inode->type = MEMFS_TYPE_FREE;
    inode->path[0] = '\0';
    inode->size = 0;
    inode->atime_sec = 0;
    inode->mtime_sec = 0;
    inode->ctime_sec = 0;
    return 0;
}

int memfs_is_unlinked(int ino)
{
    if (ino <= MEMFS_ROOT_INO || ino >= MEMFS_MAX_INODES) return 0;
    struct memfs_inode *inode = &memfs_inodes[ino];
    return inode->type != MEMFS_TYPE_FREE && inode->path[0] == '\0';
}

int memfs_delete(const char *path)
{
    int ino = memfs_lookup(path);
    if (ino < 0) return -1;
    return memfs_reclaim_inode(ino);
}

/* Simple directory listing: scan all inodes for immediate children of dir_ino.
 * Output format matches Linux getdents64: struct { d_ino, d_off, d_reclen,
 * d_type, d_name[] } with 8-byte alignment. */
int memfs_getdents(int dir_ino, void *buf, uint32_t len)
{
    if (dir_ino < 0 || dir_ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *dir = &memfs_inodes[dir_ino];
    if (dir->type != MEMFS_TYPE_DIR) return -1;

    const char *dir_path = dir->path;
    int dir_len = 0;
    while (dir_path[dir_len]) dir_len++;

    uint8_t *out = (uint8_t *)buf;
    uint32_t written = 0;

    for (int i = 0; i < MEMFS_MAX_INODES; i++) {
        if (memfs_inodes[i].type == MEMFS_TYPE_FREE) continue;
        if (i == dir_ino) continue;

        const char *child_path = memfs_inodes[i].path;

        /* Must start with dir_path */
        int j;
        for (j = 0; dir_path[j]; j++)
            if (child_path[j] != dir_path[j]) break;
        if (dir_path[j] != '\0') continue;  /* not a prefix match */

        /* If dir_path is "/", child starts at position 1.
         * Otherwise, child starts after dir_path.
         * If dir_path doesn't end with '/', it needs to. */
        const char *name_start;
        if (dir_len == 1 && dir_path[0] == '/')
            name_start = child_path + 1;
        else
            name_start = child_path + dir_len;

        /* Name must not contain '/' (immediate child only) */
        int name_len = 0;
        int has_slash = 0;
        while (name_start[name_len]) {
            if (name_start[name_len] == '/') { has_slash = 1; break; }
            name_len++;
        }
        if (has_slash) continue;  /* not an immediate child */
        if (name_len == 0) continue;

        /* Build dirent */
        uint16_t reclen = (uint16_t)(19 + name_len + 7) & ~7U;  /* 8-byte aligned */
        if (written + reclen > len) break;

        uint64_t d_ino  = (uint64_t)i;
        int64_t  d_off  = 0;
        uint8_t  d_type = (memfs_inodes[i].type == MEMFS_TYPE_DIR) ? 4 : 8;

        /* Write dirent fields (little-endian) */
        uint8_t *p = out + written;
        p[0] = (uint8_t)(d_ino);
        p[1] = (uint8_t)(d_ino >> 8);
        p[2] = (uint8_t)(d_ino >> 16);
        p[3] = (uint8_t)(d_ino >> 24);
        p[4] = (uint8_t)(d_ino >> 32);
        p[5] = (uint8_t)(d_ino >> 40);
        p[6] = (uint8_t)(d_ino >> 48);
        p[7] = (uint8_t)(d_ino >> 56);

        p[8]  = (uint8_t)(d_off);
        p[9]  = (uint8_t)(d_off >> 8);
        p[10] = (uint8_t)(d_off >> 16);
        p[11] = (uint8_t)(d_off >> 24);
        p[12] = (uint8_t)(d_off >> 32);
        p[13] = (uint8_t)(d_off >> 40);
        p[14] = (uint8_t)(d_off >> 48);
        p[15] = (uint8_t)(d_off >> 56);

        p[16] = (uint8_t)(reclen);
        p[17] = (uint8_t)(reclen >> 8);

        p[18] = d_type;

        for (int k = 0; k < name_len; k++)
            p[19 + k] = name_start[k];

        written += reclen;
    }
    return (int)written;
}

uint32_t memfs_inode_size(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return 0;
    return memfs_inodes[ino].size;
}

uint64_t memfs_inode_atime(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return 0;
    return memfs_inodes[ino].atime_sec;
}

uint64_t memfs_inode_mtime(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return 0;
    return memfs_inodes[ino].mtime_sec;
}

uint64_t memfs_inode_ctime(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return 0;
    return memfs_inodes[ino].ctime_sec;
}

int memfs_set_times(int ino, uint64_t atime_sec, uint64_t mtime_sec)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = &memfs_inodes[ino];
    if (inode->type == MEMFS_TYPE_FREE) return -1;
    inode->atime_sec = atime_sec;
    inode->mtime_sec = mtime_sec;
    inode->ctime_sec = memfs_now_sec();
    return 0;
}

const char *memfs_get_path(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return "/";
    if (memfs_inodes[ino].type == MEMFS_TYPE_FREE) return "/";
    return memfs_inodes[ino].path;
}

int memfs_path_prefix(const char *path, const char *prefix)
{
    int i;
    for (i = 0; prefix[i]; i++)
        if (path[i] != prefix[i]) return 0;
    return 1;
}
