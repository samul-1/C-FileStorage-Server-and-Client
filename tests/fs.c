#include <stdio.h>
#include <stdlib.h>
#include "../include/filesystemApi.h"
#include <assert.h>
#include <errno.h>
#include "../utils/flags.h"
#include <unistd.h>
#include "../utils/scerrhand.h"


int main() {
    CacheStorage_t* store = allocStorage(1, 100, 0);

    char* fn1 = "file1";
    char* fn2 = "file2";
    char* fn3 = "file3";


    // no files
    //printStore(store);

    // FileNode_t* f1 = allocFile("/usr/1");
    // FileNode_t* f2 = allocFile("/usr/2");
    // FileNode_t* f3 = allocFile("/usr/3");

    // printFileptr(f2);
    // printFileptr(f3);
    // puts("--------------------------");
    // addFileToStore(store, f1);
    // addFileToStore(store, f2);
    // addFileToStore(store, f3);
    // printStore(store);
    // writeToFileHandler(store, "/usr/1", "abc", 22);
    // printFileptr(f1);
    // printStore(store);

    char* buf;
    size_t size;
    int res;
    readFileHandler(store, "", (void**)&buf, &size, 22);
    //assert(errno == EINVAL);

    openFileHandler(store, fn1, O_CREATE | O_LOCK, 1234);
    openFileHandler(store, fn2, O_CREATE | O_LOCK, 1234);
    /// printFile(store, fn1);
    puts("----------GOING TO DO WRITE 1---------");
    writeToFileHandler(store, fn1, "aaaaaaaaa", 1234);
    assert(errno == ENOENT);
    puts("----------DONE WRITE 1---------");

    puts("--- STORE INFO ----");
    printStore(store);

    puts("----------GOING TO DO WRITE 2---------");
    writeToFileHandler(store, fn2, "bbb", 1234);
    puts("----------DONE WRITE 2 ---------");

    puts("--- STORE INFO ----");
    printStore(store);

    //printFile(store, fn1);
    // sleep(1);
    // char badstr[5] = "abcd";
    // badstr[4] = 'e';
    // badstr[5] = 'f';
    // writeToFileHandler(store, fn1, badstr, 1234);
    // puts("---");
    // printFile(store, fn1);

   // free(buf);
   // free(store);

    //printStore(store);

}