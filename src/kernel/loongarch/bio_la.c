/*
 * bio_la.c — Block buffer cache implementation.
 *
 * Single static array of BIO_CACHE_SIZE entries, each holding one 4 KB block.
 * Eviction uses a round-robin clock hand — picks the next unreferenced slot
 * and flushes it if dirty before recycling.
 */
#include "early_boot.h"
#include "bio_la.h"

/* ---- Cache entry ---- */
#define BIO_FLAG_VALID  0x01
#define BIO_FLAG_DIRTY  0x02

struct bio {
    uint32_t block_num;   /* which block is cached (0 = empty) */
    uint8_t  flags;       /* BIO_FLAG_VALID | BIO_FLAG_DIRTY */
    uint8_t  ref;         /* pin count */
    uint8_t  data[BIO_BLOCK_SIZE] __attribute__((aligned(64)));
};

static struct bio bio_cache[BIO_CACHE_SIZE];
static int bio_hand;      /* clock hand for eviction */

/* ---- Initialise ---- */
void bio_init(void)
{
    for (int i = 0; i < BIO_CACHE_SIZE; i++) {
        bio_cache[i].block_num = 0;
        bio_cache[i].flags     = 0;
        bio_cache[i].ref       = 0;
    }
    bio_hand = 0;
    la_uart_puts("  bio: buffer cache (");
    la_uart_put_hex(BIO_CACHE_SIZE);
    la_uart_puts(" blocks, ");
    la_uart_put_hex(BIO_CACHE_SIZE * BIO_BLOCK_SIZE / 1024);
    la_uart_puts(" KB)\n");
}

/* ---- Find an entry in the cache ---- */
static int bio_lookup(uint32_t block_num)
{
    for (int i = 0; i < BIO_CACHE_SIZE; i++) {
        if ((bio_cache[i].flags & BIO_FLAG_VALID)
            && bio_cache[i].block_num == block_num)
            return i;
    }
    return -1;
}

/* ---- Evict one entry using round-robin clock ---- */
static int bio_evict(void)
{
    for (int tries = 0; tries < BIO_CACHE_SIZE * 2; tries++) {
        int idx = bio_hand;
        bio_hand = (bio_hand + 1) % BIO_CACHE_SIZE;

        struct bio *b = &bio_cache[idx];

        /* Skip pinned entries */
        if (b->ref > 0) continue;

        /* If dirty, write back before recycling */
        if (b->flags & BIO_FLAG_DIRTY) {
            if (la_virtio_blk_write(b->block_num, b->data) != 0)
                continue;  /* write failed — skip this slot */
        }

        return idx;
    }

    /* Everything is pinned or failing — should not happen */
    la_uart_puts("  bio: evict failed (all pinned?)\n");
    return -1;
}

/* ---- Read a block into the cache ----
 * Returns a pointer to the cached data, or NULL on I/O error.
 * The pointer is valid until the next bio_read / bio_write call (these
 * may evict the entry).  Callers that need data across multiple bio
 * operations must copy it to a local buffer first. */
void *bio_read(uint32_t block_num)
{
    /* 1. Lookup */
    int idx = bio_lookup(block_num);
    if (idx >= 0)
        return bio_cache[idx].data;

    /* 2. Evict a slot if cache is full */
    int free_idx = -1;
    for (int i = 0; i < BIO_CACHE_SIZE; i++) {
        if (!(bio_cache[i].flags & BIO_FLAG_VALID)) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        free_idx = bio_evict();
        if (free_idx < 0) return 0;
    }

    /* 3. Read from disk */
    struct bio *b = &bio_cache[free_idx];
    if (la_virtio_blk_read(block_num, b->data) != 0)
        return 0;

    b->block_num = block_num;
    b->flags     = BIO_FLAG_VALID;
    b->ref       = 0;
    return b->data;
}

/* ---- Get a writable reference to a block ----
 * Mark the block dirty so bio_sync or eviction will write it back. */
void *bio_write(uint32_t block_num)
{
    void *data = bio_read(block_num);
    if (!data) return 0;

    int idx = bio_lookup(block_num);
    if (idx >= 0)
        bio_cache[idx].flags |= BIO_FLAG_DIRTY;
    return data;
}

/* ---- Release a pin on a cached block (optional — for long-lived refs) ---- */
void bio_release(uint32_t block_num)
{
    int idx = bio_lookup(block_num);
    if (idx >= 0 && bio_cache[idx].ref > 0)
        bio_cache[idx].ref--;
}

/* ---- Flush all dirty blocks to disk ---- */
void bio_sync(void)
{
    for (int i = 0; i < BIO_CACHE_SIZE; i++) {
        if ((bio_cache[i].flags & BIO_FLAG_DIRTY) == 0)
            continue;
        if (la_virtio_blk_write(bio_cache[i].block_num,
                                bio_cache[i].data) == 0)
            bio_cache[i].flags &= ~BIO_FLAG_DIRTY;
    }
}

/* ---- Drop a specific block from the cache ---- */
void bio_invalidate(uint32_t block_num)
{
    int idx = bio_lookup(block_num);
    if (idx >= 0) {
        bio_cache[idx].flags  = 0;
        bio_cache[idx].block_num = 0;
        bio_cache[idx].ref    = 0;
    }
}
