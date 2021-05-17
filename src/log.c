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
    char buf[EVENT_SLOT_SIZE + 1] = "";
    snprintf(buf, 3, "[\n");
    fputs(buf, logFile);
    while (true) {
        dequeue(store->logBuffer, buf, EVENT_SLOT_SIZE);
        if (!strncmp(buf, LOGGER_EXIT_MSG, strlen(LOGGER_EXIT_MSG))) {
            break;
        }
        fputs(buf, logFile);
        fflush(logFile);

    }
    snprintf(buf, 2, "]");
    fputs(buf, logFile);
    fclose(logFile);

    return NULL;
}