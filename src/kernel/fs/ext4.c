#include "mod.h"

static ext4_info_t ext4_info;

static uint32 ext4_div_round_up(uint64 x, uint32 y)
{
    return (uint32)((x + y - 1) / y);
}

static uint64 ext4_make_u64(uint32 lo, uint32 hi)
{
    return ((uint64)hi << 32) | lo;
}

static uint32 ext4_block_to_fsblk(uint64 block)
{
    uint64 byte_off = block * (uint64)ext4_info.block_size;
    return (uint32)(byte_off / BLOCK_SIZE);
}

static uint32 ext4_byte_to_fsblk(uint64 byte_off)
{
    return (uint32)(byte_off / BLOCK_SIZE);
}

static uint32 ext4_block_offset_in_fsblk(uint64 block)
{
    uint64 byte_off = block * (uint64)ext4_info.block_size;
    return (uint32)(byte_off % BLOCK_SIZE);
}

static uint32 ext4_read_bytes(uint64 byte_off, void *dst, uint32 len)
{
    uint32 done = 0;
    while (done < len) {
        uint32 fsblk = ext4_byte_to_fsblk(byte_off + done);
        uint32 boff = (uint32)((byte_off + done) % BLOCK_SIZE);
        uint32 take = BLOCK_SIZE - boff;
        if (take > len - done)
            take = len - done;

        buffer_t *buf = buffer_get(fsblk);
        memmove((uint8 *)dst + done, buf->data + boff, take);
        buffer_put(buf);
        done += take;
    }
    return done;
}

static int ext4_read_group_desc(uint32 group, ext4_group_desc_t *gd)
{
    if (!ext4_info.active || group >= ext4_info.groups_count)
        return -1;

    uint64 desc_table_block = ext4_info.block_size == 1024 ? 2 : 1;
    uint64 desc_off = desc_table_block * (uint64)ext4_info.block_size +
        (uint64)group * ext4_info.desc_size;
    if (ext4_read_bytes(desc_off, gd, sizeof(*gd)) != sizeof(*gd))
        return -1;
    return 0;
}

static int ext4_read_inode_raw(uint32 inode_num, ext4_inode_disk_t *inode)
{
    if (!ext4_info.active || inode_num == 0)
        return -1;

    uint32 idx = inode_num - 1;
    uint32 group = idx / ext4_info.inodes_per_group;
    uint32 local = idx % ext4_info.inodes_per_group;
    ext4_group_desc_t gd;
    if (ext4_read_group_desc(group, &gd) < 0)
        return -1;

    uint64 inode_table = ext4_make_u64(gd.inode_table_lo, gd.inode_table_hi);
    uint64 inode_off = inode_table * (uint64)ext4_info.block_size +
        (uint64)local * ext4_info.inode_size;
    memset(inode, 0, sizeof(*inode));
    if (ext4_read_bytes(inode_off, inode, sizeof(*inode)) != sizeof(*inode))
        return -1;
    return 0;
}

static uint64 ext4_inode_size(const ext4_inode_disk_t *inode)
{
    return ((uint64)inode->size_high << 32) | inode->size_lo;
}

static int ext4_inode_type(const ext4_inode_disk_t *inode)
{
    switch (inode->mode & EXT4_INODE_MODE_MASK) {
    case EXT4_INODE_MODE_DIR:
        return INODE_TYPE_DIR;
    case EXT4_INODE_MODE_REG:
        return INODE_TYPE_DATA;
    default:
        return -1;
    }
}

static uint64 ext4_extent_start(const ext4_extent_t *ext)
{
    return ((uint64)ext->start_hi << 32) | ext->start_lo;
}

static int ext4_inode_lbn_to_pblock(const ext4_inode_disk_t *inode, uint32 lbn, uint64 *pblock)
{
    if (!(inode->flags & EXT4_EXTENTS_FL))
        return -1;

    uint8 *scratch = (uint8 *)pmem_alloc(true);
    if (scratch == NULL)
        return -1;

    const uint8 *node = inode->block;
    buffer_t *held_buf = NULL;
    int ret = -1;

    while (1) {
        const ext4_extent_header_t *eh = (const ext4_extent_header_t *)node;
        if (eh->magic != EXT4_EXT_MAGIC)
            break;

        if (eh->depth == 0) {
            const ext4_extent_t *ext = (const ext4_extent_t *)(node + sizeof(*eh));
            for (uint32 i = 0; i < eh->entries; i++) {
                uint32 start = ext[i].block;
                uint32 len = ext[i].len & 0x7fff;
                if (lbn >= start && lbn < start + len) {
                    *pblock = ext4_extent_start(&ext[i]) + (lbn - start);
                    ret = 0;
                    break;
                }
            }
            break;
        }

        const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(node + sizeof(*eh));
        int chosen = -1;
        for (uint32 i = 0; i < eh->entries; i++) {
            if (lbn < idx[i].block)
                break;
            chosen = (int)i;
        }
        if (chosen < 0)
            break;

        uint64 child = ((uint64)idx[chosen].leaf_hi << 32) | idx[chosen].leaf_lo;
        uint32 fsblk = ext4_block_to_fsblk(child);
        uint32 off = ext4_block_offset_in_fsblk(child);

        if (held_buf != NULL) {
            buffer_put(held_buf);
            held_buf = NULL;
        }

        buffer_t *buf = buffer_get(fsblk);
        if (off + ext4_info.block_size <= BLOCK_SIZE) {
            node = buf->data + off;
            held_buf = buf;
            continue;
        }

        buffer_put(buf);
        if (ext4_read_bytes(child * (uint64)ext4_info.block_size, scratch, ext4_info.block_size) != ext4_info.block_size)
            break;
        node = scratch;
    }

    if (held_buf != NULL)
        buffer_put(held_buf);
    pmem_free((uint64)scratch, true);
    return ret;
}

uint32 ext4_read_inode_data(uint32 inode_num, uint32 offset, uint32 len, void *dst)
{
    ext4_inode_disk_t inode;
    if (ext4_read_inode_raw(inode_num, &inode) < 0)
        return 0;

    uint64 fsize = ext4_inode_size(&inode);
    if ((uint64)offset >= fsize)
        return 0;
    if ((uint64)offset + len > fsize)
        len = (uint32)(fsize - offset);

    uint32 done = 0;
    while (done < len) {
        uint32 off = offset + done;
        uint32 lbn = off / ext4_info.block_size;
        uint32 boff = off % ext4_info.block_size;
        uint32 take = ext4_info.block_size - boff;
        if (take > len - done)
            take = len - done;

        uint64 pblock;
        if (ext4_inode_lbn_to_pblock(&inode, lbn, &pblock) < 0)
            break;
        if (ext4_read_bytes(pblock * (uint64)ext4_info.block_size + boff, (uint8 *)dst + done, take) != take)
            break;
        done += take;
    }
    return done;
}

static int ext4_dir_lookup(uint32 dir_ino, const char *name, uint32 *child_ino, uint8 *file_type)
{
    ext4_inode_disk_t dir;
    if (ext4_read_inode_raw(dir_ino, &dir) < 0)
        return -1;
    if (ext4_inode_type(&dir) != INODE_TYPE_DIR)
        return -1;

    uint32 target_len = (uint32)strlen(name);
    uint64 size = ext4_inode_size(&dir);
    uint32 pos = 0;

    while ((uint64)pos + 8 <= size) {
        ext4_dirent_t hdr;
        if (ext4_read_inode_data(dir_ino, pos, 8, &hdr) != 8)
            return -1;
        if (hdr.rec_len < 8)
            return -1;

        if ((uint64)pos + hdr.rec_len > size)
            return -1;

        if (hdr.inode != 0 && hdr.name_len > 0) {
            uint32 cmp_len = hdr.name_len;
            if (cmp_len <= EXT4_NAME_LEN && cmp_len == target_len) {
                char entry_name[EXT4_NAME_LEN + 1];
                memset(entry_name, 0, sizeof(entry_name));
                if (ext4_read_inode_data(dir_ino, pos + 8, cmp_len, entry_name) != cmp_len)
                    return -1;
                entry_name[cmp_len] = 0;

                if (strncmp(entry_name, name, cmp_len) == 0) {
                    *child_ino = hdr.inode;
                    if (file_type)
                        *file_type = hdr.file_type;
                    return 0;
                }
            }
        }

        pos += hdr.rec_len;
    }

    return -1;
}

static char *ext4_get_element(char *path, char *name)
{
    while (*path == '/')
        path++;
    if (*path == 0) {
        name[0] = 0;
        return NULL;
    }
    char *start = path;
    while (*path != '/' && *path != 0)
        path++;
    int len = path - start;
    if (len > MAXLEN_FILENAME - 1)
        len = MAXLEN_FILENAME - 1;
    memmove(name, start, len);
    name[len] = 0;
    while (*path == '/')
        path++;
    return path;
}

int ext4_lookup_path(char *path, uint32 *inode_num, uint16 *inode_type)
{
    if (!ext4_info.active || path == NULL)
        return -1;

    uint32 cur = EXT4_ROOT_INO;
    char name[MAXLEN_FILENAME];
    char *rest = path;

    if (*rest == 0 || (*rest == '/' && rest[1] == 0)) {
        if (inode_num)
            *inode_num = cur;
        if (inode_type)
            *inode_type = INODE_TYPE_DIR;
        return 0;
    }

    while ((rest = ext4_get_element(rest, name)) != NULL) {
        uint32 next;
        uint8 file_type = EXT4_FT_UNKNOWN;
        if (ext4_dir_lookup(cur, name, &next, &file_type) < 0)
            return -1;
        cur = next;
        if (*rest == 0)
            break;
    }

    ext4_inode_disk_t inode;
    if (ext4_read_inode_raw(cur, &inode) < 0)
        return -1;
    int type = ext4_inode_type(&inode);
    if (type < 0)
        return -1;
    if (inode_num)
        *inode_num = cur;
    if (inode_type)
        *inode_type = (uint16)type;
    return 0;
}

int ext4_fill_inode(uint32 inode_num, inode_t *ip)
{
    ext4_inode_disk_t raw;
    if (ext4_read_inode_raw(inode_num, &raw) < 0)
        return -1;

    int type = ext4_inode_type(&raw);
    if (type < 0)
        return -1;

    memset(&ip->disk_info, 0, sizeof(ip->disk_info));
    ip->inode_num = inode_num;
    ip->disk_info.type = (uint16)type;
    ip->disk_info.major = INODE_MAJOR_DEFAULT;
    ip->disk_info.minor = INODE_MINOR_DEFAULT;
    ip->disk_info.nlink = raw.links_count;
    uint64 size = ext4_inode_size(&raw);
    if (size > INODE_MAX_SIZE)
        ip->disk_info.size = INODE_MAX_SIZE;
    else
        ip->disk_info.size = (uint32)size;
    ip->valid_info = true;
    return 0;
}

bool ext4_mount_from_super(const ext4_super_preview_t *sb)
{
    memset(&ext4_info, 0, sizeof(ext4_info));
    ext4_info.sb = *sb;
    ext4_info.block_size = 1024U << sb->log_block_size;
    ext4_info.blocks_per_group = sb->blocks_per_group;
    ext4_info.inodes_per_group = sb->inodes_per_group;
    ext4_info.inode_size = sb->inode_size ? sb->inode_size : 128;
    ext4_info.first_data_block = sb->first_data_block;
    ext4_info.desc_size = sb->desc_size >= 32 ? sb->desc_size : 32;
    ext4_info.blocks_count = ext4_make_u64(sb->blocks_count_lo, sb->blocks_count_hi);
    if (ext4_info.blocks_per_group == 0 || ext4_info.inodes_per_group == 0)
        return false;
    ext4_info.groups_count = ext4_div_round_up(ext4_info.blocks_count - sb->first_data_block,
        ext4_info.blocks_per_group);

    ext4_info.unsupported_incompat = sb->feature_incompat &
        ~(EXT4_INCOMPAT_FILETYPE | EXT4_INCOMPAT_EXTENTS | EXT4_INCOMPAT_64BIT | EXT4_INCOMPAT_FLEX_BG);
    ext4_info.unsupported_ro_compat = sb->feature_ro_compat &
        ~(EXT4_RO_COMPAT_SPARSE_SUPER | EXT4_RO_COMPAT_LARGE_FILE | EXT4_RO_COMPAT_HUGE_FILE |
          EXT4_RO_COMPAT_DIR_NLINK | EXT4_RO_COMPAT_EXTRA_ISIZE | EXT4_RO_COMPAT_METADATA_CSUM |
          EXT4_RO_COMPAT_READONLY | EXT4_RO_COMPAT_PROJECT | EXT4_RO_COMPAT_GDT_CSUM);

    if (ext4_info.unsupported_incompat != 0)
        return false;
    if (!(sb->feature_incompat & EXT4_INCOMPAT_EXTENTS))
        return false;

    ext4_info.active = true;
    return true;
}

bool ext4_is_active()
{
    return ext4_info.active;
}

const ext4_info_t *ext4_get_info()
{
    return &ext4_info;
}
