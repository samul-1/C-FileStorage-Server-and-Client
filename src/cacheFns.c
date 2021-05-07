#ifndef CACHE_FNS_H
#define CACHE_FNS_H

#include "../include/cacheFns.h"

#include "../include/filesystemApi.h"


/**
 * Defines comparator functions used by the cache heap to determine the files
 * to evict.
 */

int lru_cmp(const void* f1, const void* f2) {
    return ((FileNode_t*)f2)->lastRef - ((FileNode_t*)f1)->lastRef;
}

int lfu_cmp(const void* f1, const void* f2) {
    return ((FileNode_t*)f2)->refCount - ((FileNode_t*)f1)->refCount;
}

int fifo_cmp(const void* f1, const void* f2) {
    return ((FileNode_t*)f2)->insertionTime - ((FileNode_t*)f1)->insertionTime;
}

const (*cmp_fns[3])(void* f1, void* f2) = { fifo_cmp, lru_cmp, lfu_cmp };

#endif