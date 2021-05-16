#ifndef LOG_H
#define LOG_H

#define EVENT_SLOT_SIZE 1024
#define EVENT_BUF_CAP 5000

#define MAX_LOG_PATHNAME 1024
#include "../include/filesystemApi.h"

struct logFlusherArgs {
    char pathname[MAX_LOG_PATHNAME];
    CacheStorage_t* store;
};

void* logFlusher(void* args);

#endif
