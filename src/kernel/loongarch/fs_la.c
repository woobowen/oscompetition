/*
 * Minimal filesystem reader for LoongArch.
 *
 * Supports two formats (tried in order):
 *   1. SeaOS custom FS  (magic 0x12341234 at block 0)
 *   2. EXT4             (magic 0xEF53   at block 0 + 1024)
 *
 * All reads via bio buffer cache — LRU eviction, no locks.
 */
#include "early_boot.h"

/* ================================================================
 *  Utilities
 * ================================================================ */

static void la_memset(void *d, int c, uint32_t n)
{
    uint8_t *p = (uint8_t *)d;
    while (n--) *p++ = (uint8_t)c;
}

static void la_memmove(void *d, const void *s, uint32_t n)
{
    uint8_t *dst = (uint8_t *)d;
    const uint8_t *src = (const uint8_t *)s;
    if (dst < src) { while (n--) *dst++ = *src++; }
    else { dst += n; src += n; while (n--) *--dst = *--src; }
}

static uint32_t la_strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int la_strncmp(const char *a, const char *b, uint32_t n)
{
    while (n > 0 && *a && *a == *b) { a++; b++; n--; }
    return n == 0 ? 0 : (unsigned char)*a - (unsigned char)*b;
}

static char *la_path_elem(char *p, char *name)
{
    while (*p == '/') p++;
    if (*p == 0) { name[0] = 0; return 0; }
    char *s = p;
    while (*p != '/' && *p != 0) p++;
    int len = p - s;
    if (len > 59) len = 59;
    la_memmove(name, s, len);
    name[len] = 0;
    while (*p == '/') p++;
    return p;
}

/* ================================================================
 *  Shared constants & helpers
 * ================================================================ */

#define LA_BLKSIZE       4096
#define LA_MAXNAME       255

static uint8_t *la_blk_read(uint32_t blk)
{
    return (uint8_t *)bio_read(blk);
}

/* ================================================================
 *  SeaOS custom FS
 * ================================================================ */

#define SEA_MAGIC        0x12341234U
#define SEA_ROOT_INO     0
#define SEA_TYPE_DIR     1
#define SEA_IDX_DIRECT   10
#define SEA_IDX_L1       12          /* + 2 single-indirect */
#define SEA_IDX_L2       13          /* + 1 double-indirect */
#define SEA_IDXPERBLK    (LA_BLKSIZE / 4)  /* 1024 */

typedef struct {
    uint32_t magic_num, block_size, total_blocks, total_inodes;
    uint32_t inode_bitmap_firstblock, inode_bitmap_blocks;
    uint32_t inode_firstblock,        inode_blocks;
    uint32_t data_bitmap_firstblock,  data_bitmap_blocks;
    uint32_t data_firstblock,         data_blocks;
} sea_sb_t;

typedef struct {
    uint16_t type, major, minor, nlink;
    uint32_t size;
    uint32_t index[SEA_IDX_L2];
} sea_inode_t;  /* 64 bytes */

typedef struct {
    char name[60];
    uint32_t inode_num;
} sea_dentry_t;  /* 64 bytes */

static struct { int active; sea_sb_t sb; } sea;

/* Read inode from disk */
static int sea_read_inode(uint32_t inum, sea_inode_t *ip)
{
    if (!sea.active || inum >= sea.sb.total_inodes) return -1;
    uint32_t blk = sea.sb.inode_firstblock + inum / (LA_BLKSIZE / 64);
    uint32_t off = (inum % (LA_BLKSIZE / 64)) * sizeof(sea_inode_t);
    uint8_t *d = la_blk_read(blk);
    if (!d) return -1;
    la_memmove(ip, d + off, sizeof(*ip));
    return 0;
}

/* Logical block → physical block */
static uint32_t sea_map_block(const sea_inode_t *ip, uint32_t lbn)
{
    if (lbn < SEA_IDX_DIRECT)
        return ip->index[lbn];

    /* single-indirect: LBN 10..2057 */
    if (lbn < SEA_IDX_DIRECT + 2 * SEA_IDXPERBLK) {
        uint32_t rel  = lbn - SEA_IDX_DIRECT;
        uint32_t slot = rel / SEA_IDXPERBLK;   /* 0 or 1 */
        uint32_t off  = rel % SEA_IDXPERBLK;
        uint32_t l1   = ip->index[SEA_IDX_DIRECT + slot];
        if (!l1) return 0;
        uint8_t *d = la_blk_read(l1);
        if (!d) return 0;
        uint32_t r; la_memmove(&r, d + off * 4, 4);
        return r;
    }

    /* double-indirect */
    {
        uint32_t rel  = lbn - SEA_IDX_DIRECT - 2 * SEA_IDXPERBLK;
        uint32_t l2off = rel / SEA_IDXPERBLK;
        uint32_t l1off = rel % SEA_IDXPERBLK;
        uint32_t l2 = ip->index[SEA_IDX_DIRECT + 2];
        if (!l2) return 0;
        uint8_t *d2 = la_blk_read(l2);
        if (!d2) return 0;
        uint32_t l1; la_memmove(&l1, d2 + l2off * 4, 4);
        if (!l1) return 0;
        uint8_t *d1 = la_blk_read(l1);
        if (!d1) return 0;
        uint32_t r; la_memmove(&r, d1 + l1off * 4, 4);
        return r;
    }
}

/* Read file data */
static uint32_t sea_read_file(uint32_t inum, uint32_t off,
                               void *dst, uint32_t len)
{
    sea_inode_t ip;
    if (sea_read_inode(inum, &ip) < 0) return 0;
    if (off >= ip.size) return 0;
    if (off + len > ip.size) len = ip.size - off;

    uint8_t *d = (uint8_t *)dst;
    uint32_t done = 0;

    while (done < len) {
        uint32_t cur  = off + done;
        uint32_t lbn  = cur / LA_BLKSIZE;
        uint32_t boff = cur % LA_BLKSIZE;
        uint32_t take = LA_BLKSIZE - boff;
        if (take > len - done) take = len - done;
        uint32_t pb = sea_map_block(&ip, lbn);
        if (!pb) break;
        uint8_t *blk = bio_read(pb);
        if (!blk) break;
        la_memmove(d + done, blk + boff, take);
        done += take;
    }
    return done;
}

/* Directory lookup */
static int sea_dir_lookup(uint32_t dir_ino, const char *name, uint32_t *out)
{
    sea_inode_t ip;
    if (sea_read_inode(dir_ino, &ip) < 0 || ip.type != SEA_TYPE_DIR) return -1;
    uint32_t total = ip.size / sizeof(sea_dentry_t);

    for (uint32_t i = 0; i < total; i++) {
        uint32_t pos  = i * sizeof(sea_dentry_t);
        uint32_t lbn  = pos / LA_BLKSIZE;
        uint32_t boff = pos % LA_BLKSIZE;
        uint32_t pb = sea_map_block(&ip, lbn);
        if (!pb) break;
        uint8_t *blk = bio_read(pb);
        if (!blk) break;
        sea_dentry_t de;
        la_memmove(&de, blk + boff, sizeof(de));
        if (de.inode_num == 0 || de.name[0] == 0) continue;
        if (la_strncmp(de.name, name, la_strlen(name)) == 0
            && la_strlen(de.name) == la_strlen(name)) {
            *out = de.inode_num;
            return 0;
        }
    }
    return -1;
}

/* Path resolution */
static int sea_lookup(char *path, uint32_t *out)
{
    if (!sea.active) return -1;
    uint32_t cur = SEA_ROOT_INO;
    char name[60];
    if (*path == 0 || (*path == '/' && path[1] == 0)) {
        if (out) *out = cur;
        return 0;
    }
    char *rest = path;
    while ((rest = la_path_elem(rest, name)) != 0) {
        if (name[0] == 0) break;
        uint32_t next;
        if (sea_dir_lookup(cur, name, &next) < 0) return -1;
        cur = next;
        if (*rest == 0) break;
    }
    if (out) *out = cur;
    return 0;
}

/* List dir for testing */
static void sea_list_dir(uint32_t dir_ino)
{
    sea_inode_t ip;
    if (sea_read_inode(dir_ino, &ip) < 0) {
        la_uart_puts("  list: can't read inode\n"); return;
    }
    uint32_t total = ip.size / sizeof(sea_dentry_t);
    la_uart_puts("  [dir ino="); la_uart_put_hex(dir_ino);
    la_uart_puts(" n="); la_uart_put_hex(total);
    la_uart_puts(" size="); la_uart_put_hex(ip.size); la_uart_puts("]\n");

    int shown = 0;
    for (uint32_t i = 0; i < total && shown < 64; i++) {
        uint32_t pos = i * sizeof(sea_dentry_t);
        uint32_t pb = sea_map_block(&ip, pos / LA_BLKSIZE);
        if (!pb) break;
        uint8_t *blk = bio_read(pb);
        if (!blk) break;
        sea_dentry_t de;
        la_memmove(&de, blk + pos % LA_BLKSIZE, sizeof(de));
        if (de.inode_num == 0 || de.name[0] == 0) continue;
        la_uart_puts("    "); la_uart_puts(de.name);
        la_uart_puts(" ino="); la_uart_put_hex(de.inode_num); la_uart_puts("\n");
        shown++;
    }
    la_uart_puts("  ["); la_uart_put_hex(shown); la_uart_puts(" shown]\n");
}

/* ================================================================
 *  EXT4 filesystem (fallback)
 * ================================================================ */

#define E4_MAGIC       0xEF53
#define E4_ROOT_INO    2
#define E4_EXT_MAGIC   0xF30A
#define E4_EXTENTS_FL  0x00080000
#define E4_MODE_DIR    0x4000
#define E4_MODE_MASK   0xF000

typedef struct {
    uint32_t inodes_count, blocks_count_lo, r_blocks_count_lo;
    uint32_t free_blocks_count_lo, free_inodes_count, first_data_block;
    uint32_t log_block_size, log_cluster_size;
    uint32_t blocks_per_group, clusters_per_group, inodes_per_group;
    uint32_t mtime, wtime;
    uint16_t mnt_count, max_mnt_count, magic, state, errors, minor_rev_level;
    uint32_t lastcheck, checkinterval, creator_os, rev_level;
    uint16_t def_resuid, def_resgid;
    uint32_t first_ino;
    uint16_t inode_size, block_group_nr;
    uint32_t feature_compat, feature_incompat, feature_ro_compat;
} e4_sb_t;

typedef struct {
    uint32_t block_bitmap_lo, inode_bitmap_lo, inode_table_lo;
    uint16_t free_blocks_count_lo, free_inodes_count_lo;
    uint16_t used_dirs_count_lo, flags;
    uint32_t exclude_bitmap_lo;
    uint16_t block_bitmap_csum_lo, inode_bitmap_csum_lo;
    uint16_t itable_unused_lo, checksum;
    uint32_t block_bitmap_hi, inode_bitmap_hi, inode_table_hi;
    uint16_t free_blocks_count_hi, free_inodes_count_hi;
    uint16_t used_dirs_count_hi, itable_unused_hi;
    uint32_t exclude_bitmap_hi;
    uint16_t block_bitmap_csum_hi, inode_bitmap_csum_hi;
    uint32_t reserved;
} e4_gd_t;

typedef struct {
    uint16_t mode; uint16_t uid_lo; uint32_t size_lo;
    uint32_t atime, ctime, mtime, dtime;
    uint16_t gid_lo, links_count;
    uint32_t blocks_lo, flags, osd1;
    uint8_t  block[60];
    uint32_t generation, file_acl_lo, size_high, obso_faddr;
} e4_inode_t;

typedef struct { uint16_t magic, entries, max, depth; uint32_t gen; } e4_eh_t;
typedef struct { uint32_t block, leaf_lo; uint16_t leaf_hi, unused; } e4_idx_t;
typedef struct { uint32_t block; uint16_t len, start_hi; uint32_t start_lo; } e4_ext_t;
typedef struct { uint32_t inode; uint16_t rec_len; uint8_t name_len, file_type; } e4_dh_t;

static struct {
    int active; uint32_t bsz, bpg, ipg, isz, fdb, dsz, ng; uint64_t bc;
} e4;

static uint64_t e4_u64(uint32_t lo, uint32_t hi) { return ((uint64_t)hi << 32) | lo; }

static uint32_t e4_read_bytes(uint64_t off, void *dst, uint32_t len)
{
    uint8_t *d = (uint8_t *)dst;
    uint32_t done = 0;
    while (done < len) {
        uint32_t blk  = (uint32_t)((off + done) / LA_BLKSIZE);
        uint32_t boff = (uint32_t)((off + done) % LA_BLKSIZE);
        uint32_t take = LA_BLKSIZE - boff;
        if (take > len - done) take = len - done;
        uint8_t *blk_data = bio_read(blk);
        if (!blk_data) break;
        la_memmove(d + done, blk_data + boff, take);
        done += take;
    }
    return done;
}

static int e4_read_gd(uint32_t g, e4_gd_t *gd)
{
    if (!e4.active || g >= e4.ng) return -1;
    /* Sanity check: e4 struct should not be corrupted */
    if (e4.bsz != 4096 || e4.isz == 0) {
        la_uart_puts("  e4 CORRUPTED: bsz=");
        la_uart_put_hex(e4.bsz);
        la_uart_puts(" isz=");
        la_uart_put_hex(e4.isz);
        la_uart_puts(" active=");
        la_uart_put_hex(e4.active);
        la_uart_puts("\n");
        return -1;
    }
    uint64_t base = (e4.bsz == 1024 ? 2 : 1) * (uint64_t)e4.bsz;
    return e4_read_bytes(base + (uint64_t)g * e4.dsz, gd, sizeof(*gd)) == sizeof(*gd) ? 0 : -1;
}

static int e4_read_inode(uint32_t inum, e4_inode_t *ip)
{
    if (!e4.active || inum == 0) return -1;
    uint32_t i = inum - 1, g = i / e4.ipg, l = i % e4.ipg;
    e4_gd_t gd;
    if (e4_read_gd(g, &gd) < 0) return -1;
    uint64_t tbl = e4_u64(gd.inode_table_lo, gd.inode_table_hi);
    uint64_t off = tbl * (uint64_t)e4.bsz + (uint64_t)l * e4.isz;
    la_memset(ip, 0, sizeof(*ip));
    uint32_t nr = e4_read_bytes(off, ip, sizeof(*ip));
    return nr == sizeof(*ip) ? 0 : -1;
}

static uint64_t e4_isize(const e4_inode_t *ip) { return ((uint64_t)ip->size_high << 32) | ip->size_lo; }

static int e4_lbn2pb(const e4_inode_t *ip, uint32_t lbn, uint64_t *pb)
{
    /* ---- Inode-block-map path (no extents flag) ----
     * Traditional ext4 layout for older/small files:
     *   block[0..11]  : direct block pointers (12 entries)
     *   block[12]     : single-indirect (block of uint32_t block numbers)
     *   block[13]     : double-indirect
     *   block[14]     : triple-indirect
     * For 4KB blocks each indirect block holds 1024 entries. */
    if (!(ip->flags & E4_EXTENTS_FL)) {
        const uint32_t *bp = (const uint32_t *)ip->block;
        if (lbn < 12) {
            if (bp[lbn] == 0) return -1;
            *pb = bp[lbn];
            return 0;
        }
        uint32_t ptrs_per_blk = e4.bsz / 4;
        lbn -= 12;
        /* single indirect */
        if (lbn < ptrs_per_blk) {
            if (bp[12] == 0) return -1;
            const uint32_t *tbl = (const uint32_t *)bio_read(bp[12]);
            if (!tbl || tbl[lbn] == 0) return -1;
            *pb = tbl[lbn];
            return 0;
        }
        lbn -= ptrs_per_blk;
        /* double indirect */
        if (lbn < (uint32_t)ptrs_per_blk * ptrs_per_blk) {
            if (bp[13] == 0) return -1;
            const uint32_t *l1 = (const uint32_t *)bio_read(bp[13]);
            if (!l1) return -1;
            uint32_t i1 = lbn / ptrs_per_blk;
            uint32_t i2 = lbn % ptrs_per_blk;
            if (l1[i1] == 0) return -1;
            const uint32_t *l2 = (const uint32_t *)bio_read(l1[i1]);
            if (!l2 || l2[i2] == 0) return -1;
            *pb = l2[i2];
            return 0;
        }
        lbn -= (uint32_t)ptrs_per_blk * ptrs_per_blk;
        /* triple indirect */
        if (bp[14] == 0) return -1;
        const uint32_t *t1 = (const uint32_t *)bio_read(bp[14]);
        if (!t1) return -1;
        uint32_t j1 = lbn / ((uint32_t)ptrs_per_blk * ptrs_per_blk);
        uint32_t rem = lbn % ((uint32_t)ptrs_per_blk * ptrs_per_blk);
        uint32_t j2 = rem / ptrs_per_blk;
        uint32_t j3 = rem % ptrs_per_blk;
        if (t1[j1] == 0) return -1;
        const uint32_t *t2 = (const uint32_t *)bio_read(t1[j1]);
        if (!t2 || t2[j2] == 0) return -1;
        const uint32_t *t3 = (const uint32_t *)bio_read(t2[j2]);
        if (!t3 || t3[j3] == 0) return -1;
        *pb = t3[j3];
        return 0;
    }
    uint8_t *sc = 0;
    const uint8_t *nd = ip->block;
    int r = -1, cnt = 0;
    for (;;) {
        if (++cnt > 10) break;
        const e4_eh_t *eh = (const e4_eh_t *)nd;
        if (eh->magic != E4_EXT_MAGIC) break;
        if (eh->depth == 0) {
            const e4_ext_t *ex = (const e4_ext_t *)(nd + sizeof(*eh));
            for (uint32_t i = 0; i < eh->entries; i++) {
                uint32_t s = ex[i].block, n = ex[i].len & 0x7fff;
                if (lbn >= s && lbn < s + n) {                    *pb = ((uint64_t)ex[i].start_hi << 32) | ex[i].start_lo | (lbn - s);
                    /* Note: extent start is the full physical block number */
                    *pb = ((uint64_t)ex[i].start_hi << 32) | ex[i].start_lo;
                    *pb += (lbn - s);
                    r = 0; break;
                }
            }
            break;
        }
        const e4_idx_t *ix = (const e4_idx_t *)(nd + sizeof(*eh));
        int c = -1;
        for (uint32_t i = 0; i < eh->entries; i++) { if (lbn < ix[i].block) break; c = (int)i; }
        if (c < 0) break;
        uint64_t child = ((uint64_t)ix[c].leaf_hi << 32) | ix[c].leaf_lo;
        sc = bio_read((uint32_t)child);
        if (!sc) break;
        nd = sc;
    }
    /* Fallback: if extents flag was set but the extent tree is invalid
     * (magic mismatch, corrupted nodes), try the traditional block-map
     * path.  This handles inodes that were created with mixed metadata
     * (e.g. extents flag set but i_block still holds direct pointers). */
    if (r < 0) {
        const uint32_t *bp = (const uint32_t *)ip->block;
        if (lbn < 12) {
            if (bp[lbn] != 0) { *pb = bp[lbn]; r = 0; }
            return r;
        }
        uint32_t ptrs_per_blk = e4.bsz / 4;
        lbn -= 12;
        if (lbn < ptrs_per_blk) {
            if (bp[12] == 0) return -1;
            const uint32_t *tbl = (const uint32_t *)bio_read(bp[12]);
            if (tbl && tbl[lbn] != 0) { *pb = tbl[lbn]; r = 0; }
            return r;
        }
        lbn -= ptrs_per_blk;
        if (lbn < (uint32_t)ptrs_per_blk * ptrs_per_blk) {
            if (bp[13] == 0) return -1;
            const uint32_t *l1 = (const uint32_t *)bio_read(bp[13]);
            if (!l1) return -1;
            uint32_t i1 = lbn / ptrs_per_blk;
            uint32_t i2 = lbn % ptrs_per_blk;
            if (l1[i1] == 0) return -1;
            const uint32_t *l2 = (const uint32_t *)bio_read(l1[i1]);
            if (l2 && l2[i2] != 0) { *pb = l2[i2]; r = 0; }
            return r;
        }
        return -1;
    }
    return r;
}

/* EXT4 file data read */
static uint32_t e4_read_file(uint32_t inum, uint32_t off, void *dst, uint32_t len)
{
    e4_inode_t ip;
    if (e4_read_inode(inum, &ip) < 0) return 0;
    uint64_t sz = e4_isize(&ip);
    if ((uint64_t)off >= sz) return 0;
    if ((uint64_t)off + len > sz) len = (uint32_t)(sz - off);
    uint8_t *d = (uint8_t *)dst;
    uint32_t done = 0;
    while (done < len) {
        uint32_t cur = off + done;
        uint32_t lbn = cur / e4.bsz, boff = cur % e4.bsz;
        uint32_t take = e4.bsz - boff;
        if (take > len - done) take = len - done;
        uint64_t pb;
        if (e4_lbn2pb(&ip, lbn, &pb) < 0) {
            /* Sparse hole: fill the rest of the buffer with zeros. */
            for (uint32_t z = 0; z < take; z++) d[done + z] = 0;
            done += take;
            continue;
        }
        if (e4_read_bytes(pb * (uint64_t)e4.bsz + boff, d + done, take) != take) {
            la_uart_puts("  e4rf: read_bytes FAIL inum=");
            la_uart_put_hex(inum);
            la_uart_puts(" pb=");
            la_uart_put_hex(pb);
            la_uart_puts("\n");
            break;
        }
        done += take;
    }
    return done;
}

/* EXT4 dir lookup */
static int e4_dir_lookup(uint32_t dir, const char *name, uint32_t *out)
{
    e4_inode_t ip;
    if (e4_read_inode(dir, &ip) < 0) return -1;
    if ((ip.mode & E4_MODE_MASK) != E4_MODE_DIR) return -1;
    uint32_t tlen = la_strlen(name);
    uint64_t sz = e4_isize(&ip);
    uint32_t pos = 0;
    while ((uint64_t)pos + 8 <= sz) {
        e4_dh_t h;
        if (e4_read_file(dir, pos, &h, 8) != 8) return -1;
        if (h.rec_len < 8) return -1;
        if ((uint64_t)pos + h.rec_len > sz) return -1;
        if (h.inode && h.name_len == tlen && h.name_len <= LA_MAXNAME) {
            char en[LA_MAXNAME + 1]; la_memset(en, 0, sizeof(en));
            if (e4_read_file(dir, pos + 8, en, h.name_len) == h.name_len
                && la_strncmp(en, name, h.name_len) == 0) {
                *out = h.inode; return 0;
            }
        }
        pos += h.rec_len;
    }
    return -1;
}

/* EXT4 path resolution.
 * Relative paths (not starting with '/') resolve against the current
 * process's cwd, carried in la_fs_cwd_ino (set by the syscall dispatcher
 * from current->cwd_ino).  This is what lets scripts run "./cyclictest"
 * after chdir-ing into the test directory.  SeaFS has no cwd support and
 * keeps resolving from its root. */
uint32_t la_fs_cwd_ino = 0;

static int e4_lookup(char *path, uint32_t *out)
{
    if (!e4.active) return -1;
    uint32_t cur = (*path == '/' || la_fs_cwd_ino == 0) ? E4_ROOT_INO
                                                        : la_fs_cwd_ino;
    char name[60];
    if (*path == 0 || (*path == '/' && path[1] == 0)) { if (out) *out = cur; return 0; }
    char *rest = path;
    while ((rest = la_path_elem(rest, name)) != 0) {
        if (name[0] == 0) break;
        uint32_t next;
        if (e4_dir_lookup(cur, name, &next) < 0) return -1;
        cur = next;
        if (*rest == 0) break;
    }
    if (out) *out = cur;
    return 0;
}

/* ================================================================
 *  Public API: mount / lookup / read / list
 * ================================================================ */

int la_fs_init(void)
{
    la_memset(&sea, 0, sizeof(sea));
    la_memset(&e4,  0, sizeof(e4));

    la_uart_puts("  fs: mounting...\n");
    uint8_t *blk = la_blk_read(0);
    if (!blk) { la_uart_puts("  fs: can't read block 0\n"); return -1; }

    /* Try SeaOS custom FS */
    sea_sb_t *sb = (sea_sb_t *)blk;
    if (sb->magic_num == SEA_MAGIC && sb->block_size == LA_BLKSIZE) {
        sea.sb = *sb;
        sea.active = 1;
        la_uart_puts("  fs: SeaOS FS mounted! inodes=");
        la_uart_put_hex(sb->total_inodes);
        la_uart_puts(" data=");
        la_uart_put_hex(sb->data_firstblock);
        la_uart_puts("+");
        la_uart_put_hex(sb->data_blocks);
        la_uart_puts("\n");
        return 0;
    }

    /* Try EXT4 */
    e4_sb_t *es = (e4_sb_t *)(blk + 1024);
    if (es->magic == E4_MAGIC) {
        e4.bsz = 1024U << es->log_block_size;
        e4.bpg = es->blocks_per_group;
        e4.ipg = es->inodes_per_group;
        e4.isz = es->inode_size ? es->inode_size : 128;
        e4.fdb = es->first_data_block;
        e4.dsz = sizeof(e4_gd_t);
        e4.bc  = e4_u64(es->blocks_count_lo, 0);
        if (e4.bpg > 0 && e4.ipg > 0) {
            e4.ng = (uint32_t)((e4.bc - es->first_data_block + e4.bpg - 1) / e4.bpg);
            e4.active = 1;
            la_uart_puts("  fs: EXT4 mounted!\n");
            return 0;
        }
    }

    la_uart_puts("  fs: unknown filesystem\n");
    return -1;
}

int la_fs_lookup(char *path, uint32_t *out)
{
    if (sea.active) return sea_lookup(path, out);
    if (e4.active)  return e4_lookup(path, out);
    return -1;
}

uint32_t la_fs_read_file(uint32_t ino, uint32_t off, void *dst, uint32_t len)
{
    if (sea.active) return sea_read_file(ino, off, dst, len);
    if (e4.active)  return e4_read_file(ino, off, dst, len);
    return 0;
}

void la_fs_list_dir(uint32_t dir_ino)
{
    if (sea.active) { sea_list_dir(dir_ino); return; }
    /* EXT4 listing not needed for now */
    la_uart_puts("  list_dir: no SeaOS FS\n");
}

/* ================================================================
 *  Inode metadata queries
 * ================================================================ */

/* Return file type: 0 = regular file, 1 = directory, -1 = error */
int la_fs_inode_type(uint32_t ino)
{
    if (sea.active) {
        sea_inode_t ip;
        if (sea_read_inode(ino, &ip) < 0) return -1;
        return (ip.type == SEA_TYPE_DIR) ? 1 : 0;
    }
    if (e4.active) {
        e4_inode_t ip;
        if (e4_read_inode(ino, &ip) < 0) return -1;
        return ((ip.mode & E4_MODE_MASK) == E4_MODE_DIR) ? 1 : 0;
    }
    return -1;
}

/* Return file size in bytes, 0 on error */
uint32_t la_fs_inode_size(uint32_t ino)
{
    if (sea.active) {
        sea_inode_t ip;
        if (sea_read_inode(ino, &ip) < 0) return 0;
        return ip.size;
    }
    if (e4.active) {
        e4_inode_t ip;
        if (e4_read_inode(ino, &ip) < 0) return 0;
        return (uint32_t)e4_isize(&ip);
    }
    return 0;
}

/* ================================================================
 *  Directory entry enumeration (Linux dirent64 format)
 *
 *  Format per entry:
 *    uint64_t d_ino      (8 bytes)  inode number
 *    uint64_t d_off      (8 bytes)  offset of next entry
 *    uint16_t d_reclen   (2 bytes)  record length
 *    uint8_t  d_type     (1 byte)   DT_REG=8, DT_DIR=4
 *    char     d_name[]   (variable) null-terminated, padded to 8 bytes
 *
 *  Returns total bytes written to dst, or 0 on error/end.
 * ================================================================ */

#define LA_DT_UNKNOWN  0
#define LA_DT_DIR      4
#define LA_DT_REG      8

static uint32_t sea_get_dentries(uint32_t dir_ino, void *dst, uint32_t len)
{
    sea_inode_t ip;
    if (sea_read_inode(dir_ino, &ip) < 0 || ip.type != SEA_TYPE_DIR) return 0;
    uint32_t total = ip.size / sizeof(sea_dentry_t);

    uint8_t *out = (uint8_t *)dst;
    uint32_t written = 0;

    for (uint32_t i = 0; i < total; i++) {
        uint32_t pos = i * sizeof(sea_dentry_t);
        uint32_t lbn = pos / LA_BLKSIZE;
        uint32_t boff = pos % LA_BLKSIZE;
        uint32_t pb = sea_map_block(&ip, lbn);
        if (!pb) break;
        uint8_t *blk = bio_read(pb);
        if (!blk) break;
        sea_dentry_t de;
        la_memmove(&de, blk + boff, sizeof(de));
        if (de.inode_num == 0 || de.name[0] == 0) continue;

        /* Compute name length */
        uint32_t namelen = 0;
        while (namelen < 59 && de.name[namelen]) namelen++;

        /* dirent64 record: 19 header + namelen + 1(NUL), padded to 8 */
        uint16_t reclen = (uint16_t)((19 + namelen + 1 + 7) & ~7U);
        if (written + reclen > len) break;

        /* Check file type */
        int ftype = la_fs_inode_type(de.inode_num);
        uint8_t dtype = (ftype == 1) ? LA_DT_DIR : LA_DT_REG;

        /* Build the record */
        uint64_t d_ino = de.inode_num;
        uint64_t d_off = written + reclen;
        la_memmove(out + written, &d_ino, 8);
        la_memmove(out + written + 8, &d_off, 8);
        la_memmove(out + written + 16, &reclen, 2);
        out[written + 18] = dtype;
        la_memmove(out + written + 19, de.name, namelen);
        out[written + 19 + namelen] = 0; /* NUL terminate */
        /* Pad remaining bytes to 8-byte boundary */
        for (uint32_t p = 19 + namelen + 1; p < reclen; p++)
            out[written + p] = 0;

        written += reclen;
    }
    return written;
}

static uint32_t e4_get_dentries(uint32_t dir_ino, void *dst, uint32_t len)
{
    e4_inode_t ip;
    if (e4_read_inode(dir_ino, &ip) < 0) return 0;
    if ((ip.mode & E4_MODE_MASK) != E4_MODE_DIR) return 0;
    uint64_t sz = e4_isize(&ip);

    uint8_t *out = (uint8_t *)dst;
    uint32_t written = 0;
    uint32_t pos = 0;

    while ((uint64_t)pos + 8 <= sz) {
        e4_dh_t h;
        if (e4_read_file(dir_ino, pos, &h, 8) != 8) break;
        if (h.rec_len < 8) break;
        if ((uint64_t)pos + h.rec_len > sz) break;

        if (h.inode != 0 && h.name_len > 0 && h.name_len <= LA_MAXNAME) {
            char name[LA_MAXNAME + 1];
            la_memset(name, 0, sizeof(name));
            if (e4_read_file(dir_ino, pos + 8, name, h.name_len) == h.name_len) {
                uint32_t namelen = h.name_len;

                /* dirent64 record */
                uint16_t reclen = (uint16_t)((19 + namelen + 1 + 7) & ~7U);
                if (written + reclen > len) break;

                /* EXT4 file_type: 1=REG→DT_REG(8), 2=DIR→DT_DIR(4) */
                uint8_t dtype = LA_DT_UNKNOWN;
                if (h.file_type == 1) dtype = LA_DT_REG;
                else if (h.file_type == 2) dtype = LA_DT_DIR;
                else {
                    /* Fallback: check inode */
                    int ft = la_fs_inode_type(h.inode);
                    dtype = (ft == 1) ? LA_DT_DIR : LA_DT_REG;
                }

                uint64_t d_ino = h.inode;
                uint64_t d_off = written + reclen;
                la_memmove(out + written, &d_ino, 8);
                la_memmove(out + written + 8, &d_off, 8);
                la_memmove(out + written + 16, &reclen, 2);
                out[written + 18] = dtype;
                la_memmove(out + written + 19, name, namelen);
                out[written + 19 + namelen] = 0;
                for (uint32_t p = 19 + namelen + 1; p < reclen; p++)
                    out[written + p] = 0;

                written += reclen;
            }
        }
        pos += h.rec_len;
    }
    return written;
}

uint32_t la_fs_get_dentries(uint32_t dir_ino, void *dst, uint32_t len)
{
    if (sea.active) return sea_get_dentries(dir_ino, dst, len);
    if (e4.active)  return e4_get_dentries(dir_ino, dst, len);
    return 0;
}

/* Return 1 if SeaFS is the active filesystem */
int la_fs_is_sea(void)
{
    return sea.active;
}
