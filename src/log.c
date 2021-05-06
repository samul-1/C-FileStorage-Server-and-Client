#include "../include/log.h"
#include "../utils/scerrhand.h"

#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>

void* logFlusher(void* args) {
    struct logFlusherArgs* tArgs = (struct logFlusherArgs*)args;
    CacheStorage_t* store = tArgs->store;
    FILE* logFile;
    DIE_ON_NULL((logFile = fopen(tArgs->pathname, "w")));
    // todo manage exit condition
    while (true) {
        DIE_ON_NZ(pthread_mutex_lock(&(store->bufferLock)));
        DIE_ON_NEG_ONE(fputs(store->logBuffer, logFile));

        // might be deleting something without actually writing it to file
        memset(store->logBuffer, 0, store->logBufferSize);
        DIE_ON_NZ(pthread_mutex_unlock(&(store->bufferLock)));
        sleep(tArgs->interval);
    }
    // todo close logfile
}