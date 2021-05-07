// #define WRITE_EXTOP_TO_LOG_BUF(buf, op, pathname, size, requestor, outcome) \
// do {\
// sprintf(buf + strlen(buf), "REQ: %d WO: %ld - %s %s ", requestor, pthread_self(), #op, pathname); \
// if (!(outcome)) sprintf(buf + strlen(buf), "- OK (%lu bytes)\n", size);\
// else if((outcome>0)) sprintf(buf + strlen(buf), "- FAILED errno %d\n", outcome);\
// else sprintf(buf + strlen(buf), "- PUT ON WAIT (code %d)\n", outcome);\
// } while (0);

#define WRITE_EXTOP_TO_LOG_BUF(buf, op, pathname, size, requestor, outcome) ;

#define MAX_LOG_PATHNAME 1024
#include "../include/filesystemApi.h"

struct logFlusherArgs {
    char pathname[MAX_LOG_PATHNAME];
    CacheStorage_t* store;
    unsigned int interval;
};

void* logFlusher(void* args);