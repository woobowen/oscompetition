/*
 * bio_la.h — Block I/O buffer cache for LoongArch.
 *
 * Caches disk blocks in memory to avoid repeated VirtIO reads.
 * Uses a simple round-robin (clock) eviction algorithm.
 */
#ifndef SEAOS_LOONGARCH_BIO_H
#define SEAOS_LOONGARCH_BIO_H

#include <stdint.h>

/* Cache 256 blocks = 1 MB of cached data */
#define BIO_CACHE_SIZE  256
#define BIO_BLOCK_SIZE  4096

void  bio_init(void);
void *bio_read(uint32_t block_num);         /* returns pointer to cached data */
void *bio_write(uint32_t block_num);        /* mark dirty, return pointer */
void  bio_sync(void);                       /* flush all dirty blocks */
void  bio_invalidate(uint32_t block_num);   /* drop a block from cache */

#endif
