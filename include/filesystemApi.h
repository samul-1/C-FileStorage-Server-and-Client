#ifndef FS_API_H
#define FS_API_H

#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include "boundedbuffer.h"
#include "icl_hash.h"

struct fdNode {
    int fd;
    struct fdNode* nextPtr;
};


typedef struct fileNode {
    char* pathname;
    char* content;
    size_t contentSize;

    int lockedBy; /*< 0 if unlocked */
    struct fdNode* pendingLocks_hPtr; /*< List of fd's that are waiting to acquire lock for this file */
    struct fdNode* openDescriptors; /*< List of fd's that have called `openFile` on this file */

    bool open; // ! this will be deleted
    bool isBeingWritten;
    size_t activeReaders;

    int canDoFirstWrite; /*< Fd of the client who created the file with O_LOCK|O_CREATE and can do the first write on this file */

    pthread_mutex_t mutex;
    pthread_mutex_t ordering;

    pthread_cond_t rwCond; /*< Used to guarantee at most 1 writer at a time */

    size_t refCount; /*< # of times the file was used since last iteration of LFU algorithm */
    time_t lastRef; /*< time elapsed since the last time the file was used - used for LFU algorithm */
    time_t insertionTime; /*< time of insertion of file in cache - used for FIFO algorithm*/

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
    icl_hash_t* dictStore;

    pthread_mutex_t mutex;

    BoundedBuffer* logBuffer;

    size_t maxReachedFileNum;
    size_t maxReachedStorageSize;
    size_t numVictims;
} CacheStorage_t;


// ! remove
FileNode_t* allocFile(const char* pathname);

void printFile(const CacheStorage_t* store, const char* pathname);
void printFileptr(FileNode_t* f);
void printStore(const CacheStorage_t* store);
void addFileToStore(CacheStorage_t* store, FileNode_t* filePtr);
void printFdList(struct fdNode* h);
void freeFdList(struct fdNode* h);
// ! end remove

CacheStorage_t* allocStorage(const size_t maxFileNum, const size_t maxStorageSize, const short replacementAlgo);
int destroyStorage(CacheStorage_t* store);

int openFileHandler(CacheStorage_t* store, const char* pathname, int flags, struct fdNode** notifyList, const int requestor);

int readFileHandler(CacheStorage_t* store, const char* pathname, void** buf, size_t* size, const int requestor);
int writeToFileHandler(CacheStorage_t* store, const char* pathname, const char* newContent, struct fdNode** notifyList, const int requestor);
int lockFileHandler(CacheStorage_t* store, const char* pathname, const int requestor);
int unlockFileHandler(CacheStorage_t* store, const char* pathname, int* newLockFd, const int requestor);
int clientExitHandler(CacheStorage_t* store, struct fdNode** notifyList, const int requestor);
int closeFileHandler(CacheStorage_t* store, const char* pathname, const int requestor);
int removeFileHandler(CacheStorage_t* store, const char* pathname, struct fdNode** notifyList, const int requestor);
bool testFirstWrite(CacheStorage_t* store, const char* pathname, const int requestor);

#endif