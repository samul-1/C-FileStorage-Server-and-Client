#ifndef CACHE_FNS_H
#define CACHE_FNS_H


#define FIFO_ALGO 0
#define LRU_ALGO 1
#define LFU_ALGO 2

/**
 * Defines comparator functions used by the cache heap to determine the files
 * to evict.
 */

int lru_cmp(const void* f1, const void* f2);

int lfu_cmp(const void* f1, const void* f2);

int fifo_cmp(const void* f1, const void* f2);

extern int (*cmp_fns[3])(void* f1, void* f2);

#endif