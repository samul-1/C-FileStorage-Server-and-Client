/**
 * Author: Samuele Bonini
 *
 * Defines a set of low-level operations to handle a simple virtual filesystem.
 * These functions are *not* inherently thread-safe: therefore, the server will wrap
 * them up in the functions provided in `filesystemApi.c` rather than calling them
 * directly.
 *
**/

#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>


typedef struct fileNode {
    char* pathname;
    char* content;
    size_t contentSize;

    pid_t lockedBy; /*< 0 if unlocked */

    bool open;
    bool valid;

    bool isBeingWritten;
    size_t activeReaders;
    size_t pendingLockCount;

    pthread_mutex_t mutex;
    pthread_mutex_t ordering;

    pthread_cond_t rwCond; /*< Used to guarantee at most 1 writer at a time */
    pthread_cond_t lockedCond; /*< Used to wait on a locked file by another process */

    size_t refCount; /*< # of times the file was used since last iteration of LFU algorithm */
    time_t lastRef; /*< time elapsed since the last time the file was used */
    struct fileNode* prevPtr;
    struct fileNode* nextPtr;
} FileNode_t;


typedef struct cacheStorage {
    size_t maxFileNum;
    size_t maxStorageSize;
    size_t currFileNum;
    size_t currStorageSize;

    short replacementAlgo; /*< 0 FIFO; 1 LRU; 2 LFU */

    FileNode_t* hPtr;
    FileNode_t* tPtr;

    pthread_mutex_t mutex;
} CacheStorage_t;


FileNode_t* findFile(const CacheStorage_t* store, const char* pathname);
int addFileToStore(CacheStorage_t* store, FileNode_t* filePtr);
int createOpenHandler(const char* pathname, int mode, const pid_t requestor);


int openFile(FileNode_t* filePtr, bool lock, const pid_t requestor);
int readfile(FileNode_t* filePtr, char* dest, const pid_t requestor);
// writefile
// appendtofile
int lockFile(FileNode_t* filePtr, const pid_t requestor);
int unlockFile(FileNode_t* filePtr, const pid_t requestor);
int closeFile(FileNode_t* filePtr, const pid_t requestor);
int removeFile(FileNode_t* filePtr);
