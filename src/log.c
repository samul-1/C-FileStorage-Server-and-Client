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
    char buf[EVENT_SLOT_SIZE];
    // todo manage exit condition
    while (true) {
        dequeue(store->logBuffer, buf, EVENT_SLOT_SIZE);
        fputs(buf, logFile);
        sleep(tArgs->interval);
    }
    // todo close logfile
}