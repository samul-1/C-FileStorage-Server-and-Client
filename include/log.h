

#define EVENT_SLOT_SIZE 1024
#define EVENT_BUF_CAP 500000

#define WRITE_EXTOP_TO_LOG_BUF(buf, op, pathname, size, requestor, outcome)\
    do{\
        char eventBuf[EVENT_SLOT_SIZE];\
        sprintf(eventBuf, "REQ: %d WO: %ld - %s %s ", requestor, pthread_self(), #op, pathname);\
            if (!(outcome)) {\
                sprintf(eventBuf + strlen(eventBuf), "- OK (%lu bytes)\n", size);\
            }\
            else if ((outcome > 0)) {\
                sprintf(eventBuf + strlen(eventBuf), "- FAILED errno %d\n", outcome);\
            } else {\
                sprintf(eventBuf + strlen(eventBuf), "- PUT ON WAIT (code %d)\n", outcome);\
            }puts(eventBuf);\
            enqueue(buf, eventBuf);\
    } while (0);

#define MAX_LOG_PATHNAME 1024
#include "../include/filesystemApi.h"

struct logFlusherArgs {
    char pathname[MAX_LOG_PATHNAME];
    CacheStorage_t* store;
    unsigned int interval;
};

void* logFlusher(void* args);