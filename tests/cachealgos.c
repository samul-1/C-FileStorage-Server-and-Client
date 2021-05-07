#include <stdio.h> 
#include <stdlib.h>
#include "../include/filesystemApi.h"
#include <assert.h>
#include <errno.h>
#include "../utils/flags.h"
#include <unistd.h>
#include "../utils/scerrhand.h"
#include "../include/log.h"
#include "../include/cacheFns.h"


int main() {
    char* fn1 = "file1";
    char* fn2 = "file2";
    char* fn3 = "file3";
    char* fn4 = "file4";
    char* fn5 = "file5";

    struct fdNode* list = NULL;
    int newlock = 0;

    char* buf;
    int cnt;

    puts("\n\nBattery 1- FIFO");
    CacheStorage_t* store1 = allocStorage(3, 10, FIFO_ALGO);
    assert(store1);

    // file creation
    assert(openFileHandler(store1, fn1, O_CREATE, &list, 1) == 0);
    assert(store1->currFileNum == 1);
    assert(store1->currStorageSize == 0);

    assert(openFileHandler(store1, fn2, O_CREATE, &list, 1) == 0);
    assert(store1->currFileNum == 2);
    assert(store1->currStorageSize == 0);

    assert(openFileHandler(store1, fn3, O_CREATE, &list, 1) == 0);
    assert(store1->currFileNum == 3);
    assert(store1->currStorageSize == 0);

    readFileHandler(store1, fn1, &buf, &cnt, 1);
    readFileHandler(store1, fn1, &buf, &cnt, 1);
    readFileHandler(store1, fn2, &buf, &cnt, 1);

    assert(openFileHandler(store1, fn4, O_CREATE, &list, 1) == 0);

    // file 1 deleted according to FIFO
    printStore(store1);

    free(store1);

    /*----------------------------------------------------------------*/
    puts("\n\nBattery 2 - LRU");
    CacheStorage_t* store2 = allocStorage(3, 10, LRU_ALGO);
    assert(store2);

    // file creation
    assert(openFileHandler(store2, fn1, O_CREATE, &list, 1) == 0);
    assert(store2->currFileNum == 1);
    assert(store2->currStorageSize == 0);

    assert(openFileHandler(store2, fn2, O_CREATE, &list, 1) == 0);
    assert(store2->currFileNum == 2);
    assert(store2->currStorageSize == 0);

    assert(openFileHandler(store2, fn3, O_CREATE, &list, 1) == 0);
    assert(store2->currFileNum == 3);
    assert(store2->currStorageSize == 0);

    readFileHandler(store2, fn1, &buf, &cnt, 1);
    readFileHandler(store2, fn1, &buf, &cnt, 1);
    readFileHandler(store2, fn2, &buf, &cnt, 1);

    assert(openFileHandler(store2, fn4, O_CREATE, &list, 1) == 0);

    // file 3 deleted according to LRU
    printStore(store2);

    free(store2);

    /*----------------------------------------------------------------*/
    puts("\n\nBattery 3 - LFU");
    CacheStorage_t* store3 = allocStorage(3, 10, LFU_ALGO);
    assert(store3);

    assert(openFileHandler(store3, fn1, O_CREATE, &list, 1) == 0);
    assert(store3->currFileNum == 1);
    assert(store3->currStorageSize == 0);

    assert(openFileHandler(store3, fn2, O_CREATE, &list, 1) == 0);
    assert(store3->currFileNum == 2);
    assert(store3->currStorageSize == 0);

    assert(openFileHandler(store3, fn3, O_CREATE, &list, 1) == 0);
    assert(store3->currFileNum == 3);
    assert(store3->currStorageSize == 0);

    readFileHandler(store3, fn1, &buf, &cnt, 1);
    readFileHandler(store3, fn1, &buf, &cnt, 1);
    readFileHandler(store3, fn2, &buf, &cnt, 1);
    readFileHandler(store3, fn3, &buf, &cnt, 1);
    readFileHandler(store3, fn3, &buf, &cnt, 1);

    assert(openFileHandler(store3, fn4, O_CREATE, &list, 1) == 0);

    // file 2 deleted according to LRU
    printStore(store3);

    free(store3);
}
