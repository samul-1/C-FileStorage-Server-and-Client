#include <stdio.h> 
#include <stdlib.h>
#include "../include/filesystemApi.h"
#include <assert.h>
#include <errno.h>
#include "../utils/flags.h"
#include <unistd.h>
#include "../utils/scerrhand.h"
#include "../include/log.h"
#include "../include/boundedbuffer.h"

char* fns[11] = { "file1", "file2", "file3",  "file4", "file5", "file6", "file7", "file8",  "file9", "file10", "file11" };

void* fakeWorker(void* args) {
    CacheStorage_t* store = (CacheStorage_t*)args;
    unsigned int seedp = 0;
    //srand(pthread_self());
    int actIdx = 0;


    char* buf;
    size_t size = 0;
    //BoundedBuffer* buf = allocBoundedBuffer(1000, sizeof(int));
    while (actIdx < 1600) {

        int fileIdx = rand_r(&seedp) % 10;
        struct fdNode* list = NULL;
        int newlock = 0;

        printf("%p\n", &list);
        char* myfile = fns[fileIdx];
        switch (actIdx++ % 7) {
        case 0:
            printf("OPEN %s- OP no %d\n", myfile, actIdx);
            fflush(NULL);
            openFileHandler(store, myfile, O_CREATE | O_LOCK, &list, ((actIdx + 1) % 3 + 1));
            writeToFileHandler(store, myfile, "hifgrrwqoijerwoepqfejwqoeasfwmqdsfl", &list, 1);

            break;
        case 1:
            //break;
            printf("READ %s- OP no %d\n", myfile, actIdx);
            fflush(NULL);
            readFileHandler(store, myfile, (void*)&buf, &size, fileIdx + 1);
            break;
        case 2:
            //break;
            printf("WRITE %s- OP no %d\n", myfile, actIdx);
            fflush(NULL);
            writeToFileHandler(store, myfile, "hifgrrwqoijerwoepqfejwqoeasfwmqdsfl", &list, fileIdx + 1);
            break;
        case 3:
            //break;
            printf("LOCK %s- OP no %d\n", myfile, actIdx);
            fflush(NULL);
            lockFileHandler(store, myfile, fileIdx + 1);
            break;
        case 4:
            //break;
            printf("UNLOCK %s- OP no %d\n", myfile, actIdx);
            fflush(NULL);
            unlockFileHandler(store, myfile, &newlock, fileIdx + 1);
            break;
        case 5:
            //break;
            printf("CLOSE %s- OP no %d\n", myfile, actIdx);
            fflush(NULL);
            closeFileHandler(store, myfile, fileIdx + 1);
            break;
        case 6:
            //break;
            printf("REMOVE %s- OP no %d\n", myfile, actIdx);
            fflush(NULL);
            removeFileHandler(store, myfile, &list, fileIdx + 1);
            break;
        }
        usleep(1000);
        //sleep(1);
    }
}

int main() {
    // store creation
    CacheStorage_t* store = allocStorage(20, 100000000, 0);
    assert(store);
    int nthreads = 1;
    pthread_t tids[nthreads];
    struct logFlusherArgs logArgs = { "logs.txt", store, 0 };
    pthread_t logTid;

    for (size_t i = 0; i < nthreads; i++) {
        DIE_ON_NZ(pthread_create(&tids[i], NULL, fakeWorker, (void*)store));
    }

    DIE_ON_NZ(pthread_create(&logTid, NULL, logFlusher, (void*)&logArgs));
    puts("all threads created");
    sleep(1);
    for (size_t i = 0; i < nthreads; i++) {
        DIE_ON_NZ(pthread_join(tids[i], NULL));
    }

    //DIE_ON_NZ(pthread_join(logTid, NULL));
    puts("SUCCESS");
    return EXIT_SUCCESS;
}