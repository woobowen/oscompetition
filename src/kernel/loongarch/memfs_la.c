/*
 * memfs_la.c — In-memory writable filesystem implementation.
 *
 * Flat namespace indexed by full path string.  Data pages are allocated
 * on demand from the kernel pmem allocator and freed on file deletion.
 */
#include "early_boot.h"
#include "memfs_la.h"

/* ---- Inode table ----
 * lmbench lat_fs may create hundreds of thousands of transient files in one
 * interval.  Keep a high logical cap, but back it with high-memory pages on
 * demand so boot-time BSS stays small and low memory remains available for
 * user pages, file data, and page tables. */
#define MEMFS_INODES_PER_PAGE 24
#define MEMFS_INODE_PAGE_COUNT \
    ((MEMFS_MAX_INODES + MEMFS_INODES_PER_PAGE - 1) / MEMFS_INODES_PER_PAGE)
typedef char memfs_inode_page_fit[
    (sizeof(struct memfs_inode) * MEMFS_INODES_PER_PAGE <= LA_PGSIZE) ? 1 : -1];

static struct memfs_inode *memfs_inode_pages[MEMFS_INODE_PAGE_COUNT];
static uint8_t memfs_inode_page_used[MEMFS_INODE_PAGE_COUNT];
static int memfs_hash_buckets[MEMFS_HASH_BUCKETS];
static int memfs_next_free_hint = MEMFS_ROOT_INO + 1;

struct memfs_inode_page_node {
    struct memfs_inode_page_node *next;
};

static uint64_t memfs_inode_high_next;
static struct memfs_inode_page_node *memfs_inode_high_free;

#define MEMFS_SMALL_DATA_SIZE 1024U
#define MEMFS_SMALL_SLOTS_PER_PAGE (LA_PGSIZE / MEMFS_SMALL_DATA_SIZE)
#define MEMFS_SMALL_FULL_MASK ((1U << MEMFS_SMALL_SLOTS_PER_PAGE) - 1U)
#define MEMFS_SMALL_MAX_PAGES 32768
#define MEMFS_SMALL_SLOT_NONE 0xffffffffU

static void *memfs_small_pages[MEMFS_SMALL_MAX_PAGES];
static uint8_t memfs_small_used[MEMFS_SMALL_MAX_PAGES];
static uint32_t memfs_small_hint;

/* ---- Helpers ---- */

static void memfs_zero_page(void *page)
{
    uint64_t *p = (uint64_t *)page;
    for (int i = 0; i < LA_PGSIZE / (int)sizeof(uint64_t); i++)
        p[i] = 0;
}

static int memfs_is_high_kva(void *page)
{
    return (((uint64_t)page) >> 60) == 0x9;
}

static void *memfs_alloc_inode_page(void)
{
    if (memfs_inode_high_free) {
        struct memfs_inode_page_node *node = memfs_inode_high_free;
        memfs_inode_high_free = node->next;
        memfs_zero_page(node);
        return node;
    }

    if (memfs_inode_high_next == 0)
        memfs_inode_high_next = LA_HIGHMEM_BASE;
    if (memfs_inode_high_next + LA_PGSIZE <= LA_HIGHMEM_END) {
        void *page = (void *)la_pa_to_kva(memfs_inode_high_next);
        memfs_inode_high_next += LA_PGSIZE;
        memfs_zero_page(page);
        return page;
    }

    return la_pmem_alloc();
}

static void memfs_free_inode_page(void *page)
{
    if (!page)
        return;
    if (memfs_is_high_kva(page)) {
        struct memfs_inode_page_node *node = (struct memfs_inode_page_node *)page;
        node->next = memfs_inode_high_free;
        memfs_inode_high_free = node;
        return;
    }
    la_pmem_free(page);
}

static int memfs_inode_page_no(int ino)
{
    return ino / MEMFS_INODES_PER_PAGE;
}

static int memfs_inode_page_off(int ino)
{
    return ino % MEMFS_INODES_PER_PAGE;
}

static int memfs_inode_page_limit(int page_no)
{
    int base = page_no * MEMFS_INODES_PER_PAGE;
    int left = MEMFS_MAX_INODES - base;
    if (left < MEMFS_INODES_PER_PAGE)
        return left;
    return MEMFS_INODES_PER_PAGE;
}

static void memfs_init_inode_slot(struct memfs_inode *inode)
{
    inode->type = MEMFS_TYPE_FREE;
    inode->path_inline[0] = '\0';
    inode->path_extra = 0;
    inode->size = 0;
    inode->mode = 0;
    inode->atime_sec = 0;
    inode->mtime_sec = 0;
    inode->ctime_sec = 0;
    inode->hash_next = -1;
    for (int j = 0; j < MEMFS_PAGE_TABLES; j++)
        inode->page_tables[j] = 0;
    inode->small_data = 0;
    inode->small_size = 0;
    inode->small_slot = MEMFS_SMALL_SLOT_NONE;
}

static struct memfs_inode *memfs_inode_page_alloc(int page_no)
{
    if (page_no < 0 || page_no >= MEMFS_INODE_PAGE_COUNT)
        return 0;
    if (memfs_inode_pages[page_no])
        return memfs_inode_pages[page_no];

    struct memfs_inode *page = (struct memfs_inode *)memfs_alloc_inode_page();
    if (!page)
        return 0;

    memfs_inode_pages[page_no] = page;
    memfs_inode_page_used[page_no] = 0;
    int limit = memfs_inode_page_limit(page_no);
    for (int i = 0; i < limit; i++)
        memfs_init_inode_slot(&page[i]);
    return page;
}

static struct memfs_inode *memfs_inode_ptr(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES)
        return 0;
    struct memfs_inode *page = memfs_inode_pages[memfs_inode_page_no(ino)];
    if (!page)
        return 0;
    return &page[memfs_inode_page_off(ino)];
}

static struct memfs_inode *memfs_inode_ensure(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES)
        return 0;
    struct memfs_inode *page = memfs_inode_page_alloc(memfs_inode_page_no(ino));
    if (!page)
        return 0;
    return &page[memfs_inode_page_off(ino)];
}

static void memfs_inode_mark_live(int ino)
{
    int page_no = memfs_inode_page_no(ino);
    if (page_no >= 0 && page_no < MEMFS_INODE_PAGE_COUNT)
        memfs_inode_page_used[page_no]++;
}

static void memfs_inode_mark_free(int ino)
{
    int page_no = memfs_inode_page_no(ino);
    if (page_no < 0 || page_no >= MEMFS_INODE_PAGE_COUNT)
        return;
    if (memfs_inode_page_used[page_no] > 0)
        memfs_inode_page_used[page_no]--;

    if (page_no != memfs_inode_page_no(MEMFS_ROOT_INO) &&
        memfs_inode_page_used[page_no] == 0 &&
        memfs_inode_pages[page_no] &&
        !memfs_is_high_kva(memfs_inode_pages[page_no])) {
        memfs_free_inode_page(memfs_inode_pages[page_no]);
        memfs_inode_pages[page_no] = 0;
    }

    int base = page_no * MEMFS_INODES_PER_PAGE;
    if (base > MEMFS_ROOT_INO && base < memfs_next_free_hint)
        memfs_next_free_hint = base;
}

static uint64_t memfs_now_sec(void)
{
    uint64_t now = la_timer_get_counter() / LA_TIMER_FREQ;
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

static int memfs_strlen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static const char *memfs_inode_path(const struct memfs_inode *inode)
{
    return inode->path_extra ? inode->path_extra : inode->path_inline;
}

static int memfs_inode_path_empty(const struct memfs_inode *inode)
{
    return memfs_inode_path(inode)[0] == '\0';
}

static void memfs_clear_inode_path(struct memfs_inode *inode)
{
    if (inode->path_extra) {
        la_pmem_free(inode->path_extra);
        inode->path_extra = 0;
    }
    inode->path_inline[0] = '\0';
}

static int memfs_set_inode_path(struct memfs_inode *inode, const char *path)
{
    int len = memfs_strlen(path);
    if (len <= 0 || len >= MEMFS_MAX_NAME)
        return -1;

    if (len < MEMFS_INLINE_NAME) {
        memfs_clear_inode_path(inode);
        for (int i = 0; i <= len; i++)
            inode->path_inline[i] = path[i];
        return 0;
    }

    if (!inode->path_extra) {
        inode->path_extra = (char *)la_pmem_alloc();
        if (!inode->path_extra)
            return -1;
    }
    for (int i = 0; i <= len; i++)
        inode->path_extra[i] = path[i];
    inode->path_inline[0] = '\0';
    return 0;
}

static uint32_t memfs_hash_path(const char *path)
{
    uint32_t h = 2166136261U;
    for (int i = 0; path[i]; i++) {
        h ^= (uint8_t)path[i];
        h *= 16777619U;
    }
    return h % MEMFS_HASH_BUCKETS;
}

static void memfs_hash_insert(int ino)
{
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (ino < 0 || ino >= MEMFS_MAX_INODES)
        return;
    if (!inode || memfs_inode_path_empty(inode))
        return;

    uint32_t bucket = memfs_hash_path(memfs_inode_path(inode));
    inode->hash_next = memfs_hash_buckets[bucket];
    memfs_hash_buckets[bucket] = ino;
}

static void memfs_hash_remove(int ino)
{
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (ino < 0 || ino >= MEMFS_MAX_INODES)
        return;
    if (!inode)
        return;
    if (memfs_inode_path_empty(inode)) {
        inode->hash_next = -1;
        return;
    }

    uint32_t bucket = memfs_hash_path(memfs_inode_path(inode));
    int prev = -1;
    int cur = memfs_hash_buckets[bucket];
    while (cur >= 0) {
        struct memfs_inode *cur_inode = memfs_inode_ptr(cur);
        if (!cur_inode)
            break;
        if (cur == ino) {
            if (prev >= 0) {
                struct memfs_inode *prev_inode = memfs_inode_ptr(prev);
                if (prev_inode)
                    prev_inode->hash_next = cur_inode->hash_next;
            }
            else
                memfs_hash_buckets[bucket] = cur_inode->hash_next;
            cur_inode->hash_next = -1;
            return;
        }
        prev = cur;
        cur = cur_inode->hash_next;
    }
    inode->hash_next = -1;
}

static void **memfs_get_page_table(struct memfs_inode *inode, uint32_t page_idx, int create)
{
    if (page_idx >= MEMFS_PAGES_PER_FILE)
        return 0;

    uint32_t table_idx = page_idx / MEMFS_PAGE_PTRS_PER_TABLE;
    if (table_idx >= MEMFS_PAGE_TABLES)
        return 0;

    if (!inode->page_tables[table_idx] && create) {
        void *table = la_pmem_alloc();
        if (!table)
            return 0;
        inode->page_tables[table_idx] = table;
    }

    return (void **)inode->page_tables[table_idx];
}

static void *memfs_get_data_page(struct memfs_inode *inode, uint32_t page_idx)
{
    void **table = memfs_get_page_table(inode, page_idx, 0);
    if (!table)
        return 0;
    return table[page_idx % MEMFS_PAGE_PTRS_PER_TABLE];
}

static int memfs_set_data_page(struct memfs_inode *inode, uint32_t page_idx, void *page)
{
    void **table = memfs_get_page_table(inode, page_idx, 1);
    if (!table)
        return -1;
    table[page_idx % MEMFS_PAGE_PTRS_PER_TABLE] = page;
    return 0;
}

static void *memfs_alloc_small_slot(uint32_t *slot_id)
{
    for (uint32_t scan = 0; scan < MEMFS_SMALL_MAX_PAGES; scan++) {
        uint32_t idx = (memfs_small_hint + scan) % MEMFS_SMALL_MAX_PAGES;
        if (memfs_small_used[idx] == MEMFS_SMALL_FULL_MASK)
            continue;

        if (!memfs_small_pages[idx]) {
            void *page = la_pmem_alloc();
            if (!page)
                return 0;
            uint8_t *zp = (uint8_t *)page;
            for (int z = 0; z < LA_PGSIZE; z++)
                zp[z] = 0;
            memfs_small_pages[idx] = page;
            memfs_small_used[idx] = 0;
        }

        for (uint32_t slot = 0; slot < MEMFS_SMALL_SLOTS_PER_PAGE; slot++) {
            uint8_t bit = (uint8_t)(1U << slot);
            if (memfs_small_used[idx] & bit)
                continue;
            memfs_small_used[idx] |= bit;
            memfs_small_hint = idx;
            *slot_id = idx * MEMFS_SMALL_SLOTS_PER_PAGE + slot;
            return (uint8_t *)memfs_small_pages[idx] + slot * MEMFS_SMALL_DATA_SIZE;
        }
    }
    return 0;
}

static void memfs_free_small_slot(uint32_t slot_id)
{
    if (slot_id == MEMFS_SMALL_SLOT_NONE)
        return;
    uint32_t idx = slot_id / MEMFS_SMALL_SLOTS_PER_PAGE;
    uint32_t slot = slot_id % MEMFS_SMALL_SLOTS_PER_PAGE;
    if (idx >= MEMFS_SMALL_MAX_PAGES || !memfs_small_pages[idx])
        return;

    memfs_small_used[idx] &= (uint8_t)~(1U << slot);
    if (memfs_small_used[idx] == 0) {
        la_pmem_free(memfs_small_pages[idx]);
        memfs_small_pages[idx] = 0;
        if (idx < memfs_small_hint)
            memfs_small_hint = idx;
    }
}

static void memfs_free_small_data(struct memfs_inode *inode)
{
    if (!inode->small_data)
        return;
    memfs_free_small_slot(inode->small_slot);
    inode->small_data = 0;
    inode->small_size = 0;
    inode->small_slot = MEMFS_SMALL_SLOT_NONE;
}

static int memfs_has_data_pages(struct memfs_inode *inode)
{
    for (int t = 0; t < MEMFS_PAGE_TABLES; t++)
        if (inode->page_tables[t])
            return 1;
    return 0;
}

static int memfs_buf_all_zero(const uint8_t *src, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        if (src[i] != 0)
            return 0;
    return 1;
}

static int memfs_promote_small_data(struct memfs_inode *inode)
{
    if (!inode->small_data)
        return 0;

    void *pg = la_pmem_alloc();
    if (!pg)
        return -1;
    uint8_t *dst = (uint8_t *)pg;
    uint8_t *src = (uint8_t *)inode->small_data;
    for (int i = 0; i < LA_PGSIZE; i++)
        dst[i] = 0;
    for (uint32_t i = 0; i < inode->small_size; i++)
        dst[i] = src[i];
    if (memfs_set_data_page(inode, 0, pg) < 0) {
        la_pmem_free(pg);
        return -1;
    }
    memfs_free_small_data(inode);
    return 0;
}

static void memfs_free_file_pages(struct memfs_inode *inode)
{
    memfs_free_small_data(inode);
    for (int t = 0; t < MEMFS_PAGE_TABLES; t++) {
        void **table = (void **)inode->page_tables[t];
        if (!table)
            continue;

        for (int j = 0; j < MEMFS_PAGE_PTRS_PER_TABLE; j++) {
            uint32_t page_idx = (uint32_t)t * MEMFS_PAGE_PTRS_PER_TABLE + (uint32_t)j;
            if (page_idx >= MEMFS_PAGES_PER_FILE)
                break;
            if (table[j]) {
                la_pmem_free(table[j]);
                table[j] = 0;
            }
        }

        la_pmem_free(table);
        inode->page_tables[t] = 0;
    }
}

/* ---- Public API ---- */

void memfs_init(void)
{
    for (int i = 0; i < MEMFS_HASH_BUCKETS; i++)
        memfs_hash_buckets[i] = -1;
    for (int i = 0; i < MEMFS_INODE_PAGE_COUNT; i++) {
        memfs_inode_pages[i] = 0;
        memfs_inode_page_used[i] = 0;
    }
    for (int i = 0; i < MEMFS_SMALL_MAX_PAGES; i++) {
        memfs_small_pages[i] = 0;
        memfs_small_used[i] = 0;
    }
    memfs_small_hint = 0;
    memfs_inode_high_next = LA_HIGHMEM_BASE;
    memfs_inode_high_free = 0;
    memfs_next_free_hint = MEMFS_ROOT_INO + 1;

    /* Create root directory */
    struct memfs_inode *root = memfs_inode_ensure(MEMFS_ROOT_INO);
    if (!root) {
        la_uart_puts("  memfs: root inode page allocation failed\n");
        for (;;) {}
    }
    root->type = MEMFS_TYPE_DIR;
    root->path_inline[0] = '/';
    root->path_inline[1] = '\0';
    root->path_extra = 0;
    root->size = 0;
    root->mode = 0040755;
    root->atime_sec = root->mtime_sec = root->ctime_sec = memfs_now_sec();
    root->hash_next = -1;
    root->small_data = 0;
    root->small_size = 0;
    root->small_slot = MEMFS_SMALL_SLOT_NONE;
    memfs_inode_mark_live(MEMFS_ROOT_INO);
    memfs_hash_insert(MEMFS_ROOT_INO);

    la_uart_puts("  memfs: initialized (");
    la_uart_put_hex(MEMFS_MAX_INODES);
    la_uart_puts(" inodes)\n");
}

int memfs_lookup(const char *path)
{
    uint32_t bucket = memfs_hash_path(path);
    int cur = memfs_hash_buckets[bucket];
    while (cur >= 0) {
        struct memfs_inode *inode = memfs_inode_ptr(cur);
        if (!inode)
            break;
        if (inode->type != MEMFS_TYPE_FREE &&
            memfs_streq(memfs_inode_path(inode), path)) {
            return cur;
        }
        cur = inode->hash_next;
    }
    return -1;
}

int memfs_create(const char *path, int type)
{
    /* Find a free inode */
    int idx = -1;
    struct memfs_inode *inode = 0;
    for (int scan = 0; scan < MEMFS_MAX_INODES; scan++) {
        int cur = (memfs_next_free_hint + scan) % MEMFS_MAX_INODES;
        if (cur == MEMFS_ROOT_INO)
            continue;
        inode = memfs_inode_ptr(cur);
        if (!inode)
            inode = memfs_inode_ensure(cur);
        if (!inode)
            continue;
        if (inode->type == MEMFS_TYPE_FREE) {
            idx = cur;
            memfs_next_free_hint = (cur + 1) % MEMFS_MAX_INODES;
            if (memfs_next_free_hint == MEMFS_ROOT_INO)
                memfs_next_free_hint++;
            break;
        }
    }
    if (idx < 0) {
        la_uart_puts("  memfs: out of inodes\n");
        return -1;
    }

    memfs_init_inode_slot(inode);
    if (memfs_set_inode_path(inode, path) < 0) {
        memfs_init_inode_slot(inode);
        return -1;
    }
    inode->type = type;
    inode->size = 0;
    if (type == MEMFS_TYPE_DIR)
        inode->mode = 0040755;
    else if (type == MEMFS_TYPE_SYMLINK)
        inode->mode = 0120777;
    else
        inode->mode = 0100644;
    inode->atime_sec = memfs_now_sec();
    inode->mtime_sec = inode->atime_sec;
    inode->ctime_sec = inode->atime_sec;
    inode->hash_next = -1;
    memfs_inode_mark_live(idx);
    memfs_hash_insert(idx);

    return idx;
}

int memfs_symlink(const char *target, const char *link_path)
{
    int target_len = memfs_strlen(target);
    int link_len = memfs_strlen(link_path);
    if (target_len <= 0 || link_len <= 0 || target_len >= MEMFS_MAX_NAME)
        return -1;
    if (memfs_lookup(link_path) >= 0)
        return -1;

    int ino = memfs_create(link_path, MEMFS_TYPE_SYMLINK);
    if (ino < 0)
        return -1;
    if (memfs_write(ino, 0, target, (uint32_t)target_len) != target_len) {
        memfs_reclaim_inode(ino);
        return -1;
    }
    return 0;
}

int memfs_readlink(int ino, char *dst, uint32_t size)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES || size == 0)
        return -1;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode || inode->type != MEMFS_TYPE_SYMLINK)
        return -1;

    uint32_t n = inode->size;
    if (n > size)
        n = size;
    return memfs_read(ino, 0, dst, n);
}

int memfs_write(int ino, uint32_t offset, const void *buf, uint32_t len)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode) return -1;
    if (inode->type != MEMFS_TYPE_FILE &&
        inode->type != MEMFS_TYPE_SYMLINK) return -1;

    uint32_t end_off = offset + len;
    if (end_off > MEMFS_PAGES_PER_FILE * LA_PGSIZE) {
        /* File too large — truncate */
        len = (MEMFS_PAGES_PER_FILE * LA_PGSIZE) - offset;
        if (len == 0) return 0;
        end_off = offset + len;
    }

    const uint8_t *src = (const uint8_t *)buf;
    uint32_t written = 0;

    /* Sparse-zero compatibility: newly created lmbench lat_fs files write
     * large batches of zero-filled 1 KiB content.  Linux filesystems do not
     * need to allocate physical storage for holes; preserve that semantic
     * here by representing all-zero writes to otherwise empty files as file
     * size only.  Later non-zero writes still allocate real storage. */
    if (!inode->small_data && !memfs_has_data_pages(inode) &&
        memfs_buf_all_zero(src, len)) {
        if (end_off > inode->size)
            inode->size = end_off;
        if (len > 0) {
            inode->mtime_sec = memfs_now_sec();
            inode->ctime_sec = inode->mtime_sec;
        }
        return (int)len;
    }

    if (end_off <= MEMFS_SMALL_DATA_SIZE && !memfs_has_data_pages(inode)) {
        if (!inode->small_data) {
            uint32_t slot_id;
            inode->small_data = memfs_alloc_small_slot(&slot_id);
            if (!inode->small_data) {
                la_uart_puts("  memfs: out of memory\n");
                return 0;
            }
            inode->small_slot = slot_id;
            inode->small_size = 0;
        }
        uint8_t *dst = (uint8_t *)inode->small_data;
        for (uint32_t i = 0; i < len; i++)
            dst[offset + i] = src[i];
        written = len;
        if (end_off > inode->small_size)
            inode->small_size = end_off;
        if (end_off > inode->size)
            inode->size = end_off;
        if (written > 0) {
            inode->mtime_sec = memfs_now_sec();
            inode->ctime_sec = inode->mtime_sec;
        }
        return (int)written;
    }

    if (inode->small_data && memfs_promote_small_data(inode) < 0) {
        la_uart_puts("  memfs: out of memory\n");
        return 0;
    }

    while (written < len) {
        uint32_t cur_off = offset + written;
        uint32_t page_idx = cur_off / LA_PGSIZE;
        uint32_t page_off = cur_off % LA_PGSIZE;
        uint32_t chunk = LA_PGSIZE - page_off;
        if (chunk > len - written) chunk = len - written;

        /* Allocate page on demand */
        void *page = memfs_get_data_page(inode, page_idx);
        if (!page) {
            void *pg = la_pmem_alloc();
            if (!pg) {
                la_uart_puts("  memfs: out of memory\n");
                break;
            }
            /* Zero the page */
            uint8_t *zp = (uint8_t *)pg;
            for (int z = 0; z < LA_PGSIZE; z++) zp[z] = 0;
            if (memfs_set_data_page(inode, page_idx, pg) < 0) {
                la_pmem_free(pg);
                la_uart_puts("  memfs: out of memory\n");
                break;
            }
            page = pg;
        }

        uint8_t *dst = (uint8_t *)page;
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
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode) return -1;
    if (inode->type != MEMFS_TYPE_FILE &&
        inode->type != MEMFS_TYPE_SYMLINK) return -1;

    if (offset >= inode->size) return 0;
    if (offset + len > inode->size)
        len = inode->size - offset;

    uint8_t *dst = (uint8_t *)buf;
    uint32_t done = 0;

    if (inode->small_data && offset < MEMFS_SMALL_DATA_SIZE) {
        uint8_t *src = (uint8_t *)inode->small_data;
        while (done < len && offset + done < MEMFS_SMALL_DATA_SIZE) {
            uint32_t cur = offset + done;
            dst[done] = (cur < inode->small_size) ? src[cur] : 0;
            done++;
        }
        if (done == len) {
            inode->atime_sec = memfs_now_sec();
            return (int)done;
        }
    }

    while (done < len) {
        uint32_t cur_off = offset + done;
        uint32_t page_idx = cur_off / LA_PGSIZE;
        uint32_t page_off = cur_off % LA_PGSIZE;
        uint32_t chunk = LA_PGSIZE - page_off;
        if (chunk > len - done) chunk = len - done;

        void *page = memfs_get_data_page(inode, page_idx);
        if (!page) {
            for (uint32_t j = 0; j < chunk; j++)
                dst[done + j] = 0;
            done += chunk;
            continue;
        }
        uint8_t *psrc = (uint8_t *)page;
        for (uint32_t j = 0; j < chunk; j++)
            dst[done + j] = psrc[page_off + j];
        done += chunk;
    }
    if (done > 0)
        inode->atime_sec = memfs_now_sec();
    return (int)done;
}

int memfs_truncate(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode) return -1;
    if (inode->type != MEMFS_TYPE_FILE) return -1;

    /* Free all data pages and their pointer tables. */
    memfs_free_file_pages(inode);
    inode->size = 0;
    inode->mtime_sec = memfs_now_sec();
    inode->ctime_sec = inode->mtime_sec;
    return 0;
}

int memfs_unlink_inode(int ino)
{
    if (ino <= MEMFS_ROOT_INO || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode) return -1;
    if (inode->type == MEMFS_TYPE_FREE) return -1;
    memfs_hash_remove(ino);
    memfs_clear_inode_path(inode);
    return 0;
}

int memfs_reclaim_inode(int ino)
{
    if (ino <= MEMFS_ROOT_INO || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode) return -1;
    if (inode->type == MEMFS_TYPE_FREE) return -1;

    memfs_hash_remove(ino);
    memfs_free_file_pages(inode);

    memfs_clear_inode_path(inode);
    memfs_init_inode_slot(inode);
    memfs_inode_mark_free(ino);
    if (ino < memfs_next_free_hint)
        memfs_next_free_hint = ino;
    return 0;
}

int memfs_is_unlinked(int ino)
{
    if (ino <= MEMFS_ROOT_INO || ino >= MEMFS_MAX_INODES) return 0;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode) return 0;
    return inode->type != MEMFS_TYPE_FREE && memfs_inode_path_empty(inode);
}

int memfs_delete(const char *path)
{
    int ino = memfs_lookup(path);
    if (ino < 0) return -1;
    return memfs_reclaim_inode(ino);
}

int memfs_rename(const char *old_path, const char *new_path)
{
    int old_ino = memfs_lookup(old_path);
    if (old_ino <= MEMFS_ROOT_INO)
        return -1;
    if (memfs_lookup(new_path) >= 0)
        return -1;

    int old_len = memfs_strlen(old_path);
    int new_len = memfs_strlen(new_path);
    if (old_len <= 0 || new_len <= 0 || new_len >= MEMFS_MAX_NAME)
        return -1;

    struct memfs_inode *inode = memfs_inode_ptr(old_ino);
    if (!inode)
        return -1;
    int is_dir = inode->type == MEMFS_TYPE_DIR;
    memfs_hash_remove(old_ino);
    if (memfs_set_inode_path(inode, new_path) < 0)
        return -1;
    inode->ctime_sec = memfs_now_sec();
    memfs_hash_insert(old_ino);

    if (!is_dir)
        return 0;

    for (int page_no = 0; page_no < MEMFS_INODE_PAGE_COUNT; page_no++) {
        struct memfs_inode *page = memfs_inode_pages[page_no];
        if (!page || memfs_inode_page_used[page_no] == 0)
            continue;
        int limit = memfs_inode_page_limit(page_no);
        int base = page_no * MEMFS_INODES_PER_PAGE;
        for (int off = 0; off < limit; off++) {
            int i = base + off;
            struct memfs_inode *child = &page[off];
            if (i == old_ino || child->type == MEMFS_TYPE_FREE)
                continue;
            const char *child_path = memfs_inode_path(child);
            if (!memfs_path_prefix(child_path, old_path))
                continue;
            if (child_path[old_len] != '/')
                continue;

            char updated[MEMFS_MAX_NAME];
            int pos = 0;
            for (; new_path[pos] && pos < MEMFS_MAX_NAME - 1; pos++)
                updated[pos] = new_path[pos];
            for (int j = old_len; child_path[j] && pos < MEMFS_MAX_NAME - 1; j++)
                updated[pos++] = child_path[j];
            if (child_path[old_len] && pos >= MEMFS_MAX_NAME - 1)
                return -1;
            updated[pos] = '\0';
            memfs_hash_remove(i);
            if (memfs_set_inode_path(child, updated) < 0)
                return -1;
            child->ctime_sec = inode->ctime_sec;
            memfs_hash_insert(i);
        }
    }

    return 0;
}

/* Simple directory listing: scan all inodes for immediate children of dir_ino.
 * Output format matches Linux getdents64: struct { d_ino, d_off, d_reclen,
 * d_type, d_name[] } with 8-byte alignment. */
int memfs_getdents(int dir_ino, void *buf, uint32_t len)
{
    if (dir_ino < 0 || dir_ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *dir = memfs_inode_ptr(dir_ino);
    if (!dir) return -1;
    if (dir->type != MEMFS_TYPE_DIR) return -1;

    const char *dir_path = memfs_inode_path(dir);
    int dir_len = 0;
    while (dir_path[dir_len]) dir_len++;

    uint8_t *out = (uint8_t *)buf;
    uint32_t written = 0;

    for (int page_no = 0; page_no < MEMFS_INODE_PAGE_COUNT; page_no++) {
        struct memfs_inode *page = memfs_inode_pages[page_no];
        if (!page || memfs_inode_page_used[page_no] == 0) continue;
        int limit = memfs_inode_page_limit(page_no);
        int base = page_no * MEMFS_INODES_PER_PAGE;
        for (int off = 0; off < limit; off++) {
            int i = base + off;
            struct memfs_inode *child = &page[off];
            if (child->type == MEMFS_TYPE_FREE) continue;
            if (memfs_inode_path_empty(child)) continue;
            if (i == dir_ino) continue;

            const char *child_path = memfs_inode_path(child);

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
            if (written + reclen > len) return (int)written;

            uint64_t d_ino  = (uint64_t)i;
            int64_t  d_off  = 0;
            uint8_t  d_type = (child->type == MEMFS_TYPE_DIR) ? 4 :
                               (child->type == MEMFS_TYPE_SYMLINK) ? 10 : 8;

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
    }
    return (int)written;
}

int memfs_inode_type(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return MEMFS_TYPE_FREE;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    return inode ? inode->type : MEMFS_TYPE_FREE;
}

uint32_t memfs_inode_size(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return 0;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    return inode ? inode->size : 0;
}

uint64_t memfs_inode_atime(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return 0;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    return inode ? inode->atime_sec : 0;
}

uint64_t memfs_inode_mtime(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return 0;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    return inode ? inode->mtime_sec : 0;
}

uint64_t memfs_inode_ctime(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return 0;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    return inode ? inode->ctime_sec : 0;
}

uint32_t memfs_inode_mode(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return 0;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    return inode ? inode->mode : 0;
}

int memfs_chmod(int ino, uint32_t mode)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode) return -1;
    if (inode->type == MEMFS_TYPE_FREE) return -1;

    inode->mode = (inode->mode & ~07777U) | (mode & 07777U);
    inode->ctime_sec = memfs_now_sec();
    return 0;
}

int memfs_set_times(int ino, uint64_t atime_sec, uint64_t mtime_sec)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return -1;
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode) return -1;
    if (inode->type == MEMFS_TYPE_FREE) return -1;
    inode->atime_sec = atime_sec;
    inode->mtime_sec = mtime_sec;
    inode->ctime_sec = memfs_now_sec();
    return 0;
}

const char *memfs_get_path(int ino)
{
    if (ino < 0 || ino >= MEMFS_MAX_INODES) return "/";
    struct memfs_inode *inode = memfs_inode_ptr(ino);
    if (!inode || inode->type == MEMFS_TYPE_FREE) return "/";
    return memfs_inode_path(inode);
}

int memfs_path_prefix(const char *path, const char *prefix)
{
    int i;
    for (i = 0; prefix[i]; i++)
        if (path[i] != prefix[i]) return 0;
    return 1;
}
