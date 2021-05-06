#include <stdio.h> 
#include <stdlib.h>
#include "../include/filesystemApi.h"
#include <assert.h>
#include <errno.h>
#include "../utils/flags.h"
#include <unistd.h>
#include "../utils/scerrhand.h"


int main() {
    char* fn1 = "file1";
    char* fn2 = "file2";
    char* fn3 = "file3";
    char* fn4 = "file3";
    char* fn5 = "file3";

    struct fdNode* list = NULL;
    int newlock = 0;

    // store creation
    CacheStorage_t* store = allocStorage(2, 10, 0);
    assert(store);
    assert((store->currFileNum == 0));

    // file creation
    assert(openFileHandler(store, fn1, O_CREATE, &list, 1) == 0);
    assert(store->currFileNum == 1);
    assert(store->currStorageSize == 0);

    errno = 0;
    // file creation fail (no O_CREATE)
    assert(openFileHandler(store, fn2, 0, &list, 1) == -1);
    assert(errno == EPERM);

    // write to file (without exceeding limit)
    assert(writeToFileHandler(store, fn1, "abcdefg", &list, 124) == 0);
    assert(store->currStorageSize == 7);

    // second file creation
    assert(openFileHandler(store, fn2, O_CREATE, &list, 1) == 0);
    assert(store->currFileNum == 2);
    assert(store->currStorageSize == 7);

    // third file creation -- with eviction of file 1
    assert(openFileHandler(store, fn3, O_CREATE, &list, 1) == 0);
    assert(store->currFileNum == 2);
    assert(store->currStorageSize == 0);

    // write to file (without exceeding limit)
    assert(writeToFileHandler(store, fn2, "abcdefg", &list, 124) == 0);
    assert(store->currStorageSize == 7);

    // write to file -- limit exceeded, eviction of file 2
    assert(writeToFileHandler(store, fn3, "abcd", &list, 124) == 0);
    assert(store->currStorageSize == 4);
    assert(store->currFileNum == 1);

    // delete fails because file is unlocked
    assert(removeFileHandler(store, fn3, &list, 123) == -1);
    assert(errno == EACCES);

    // lock file
    assert(lockFileHandler(store, fn3, 3000) == 0);
    // delete still fails because the file is locked by another pid
    assert(removeFileHandler(store, fn3, &list, 123) == -1);
    assert(errno == EACCES);

    // unlock fails because the file is locked by another pid
    assert(unlockFileHandler(store, fn3, &newlock, 3001) == -1);
    assert(errno == EACCES);

    errno = 0;
    // unlock from same pid as owner succeeds
    assert(unlockFileHandler(store, fn3, &newlock, 3000) == 0);

    // lock file again
    assert(lockFileHandler(store, fn3, 3000) == 0);

    // delete now succeeds
    assert(removeFileHandler(store, fn3, &list, 3000) == 0);
    assert(store->currFileNum == 0);

    // create file with lock
    assert(openFileHandler(store, fn1, O_CREATE | O_LOCK, &list, 22) == 0);
    // delete fails because file is blocked by another client
    assert(removeFileHandler(store, fn1, &list, 21) == -1);
    assert(errno == EACCES);

    // try to lock file that's locked by another client
    assert(lockFileHandler(store, fn1, 21) == -2);
    assert(lockFileHandler(store, fn1, 20) == -2);
    assert(lockFileHandler(store, fn1, 19) == -2);
    // waiting list is now 21->20->19

    // unlock file and make the first in line gain lock
    assert(unlockFileHandler(store, fn1, &newlock, 22) == 0);
    assert(newlock == 21);

    // now delete file and get the list of clients blocked on it
    assert(removeFileHandler(store, fn1, &list, 21) == 0);
    // list contains 20->19
    freeFdList(list);
    free(store);
}
