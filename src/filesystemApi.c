/**
 * Provides wrappers for the functions defined in `filesystem.c` that guarantee
 * cache-level and file-level mutual exclusion and state consistency.
 *
 *
 * These are the functions that are directly called by the server.
 */

#include "../utils/scerrhand.h"
#include <errno.h>
#include <assert.h>
#include "../include/filesystemApi.h"
#include <stdlib.h>
#include <stdio.h>
#include "../utils/flags.h"
#include "../include/log.h"

#define MAX(a,b) (a) > (b) ? (a) : (b)


#define UPDATE_CACHE_BITS(p)\
    p->lastRef = time(0); \
    p->refCount += 1;

#define INITIALBUFSIZ 1024

#define CHECK_INPUT(storePtr, pathname, requestor)\
    if(!storePtr || !strlen(pathname) || requestor <= 0 ) { \
        errno=EINVAL;\
        return -1;\
    }


static FileNode_t* getVictim(CacheStorage_t* store) {
    // todo implement other algorithms
    return store->hPtr;
}

static void logEvent(BoundedBuffer* buffer, char* op, char* pathname, int outcome, int requestor) {
    char eventBuf[EVENT_SLOT_SIZE];
    sprintf(eventBuf, "REQ: %d WO: %ld - %s %s ", requestor, pthread_self(), op, pathname);
    if (!(outcome)) {
        sprintf(eventBuf + strlen(eventBuf), "- OK (%lu bytes)\n", 0);
    }
    else if ((outcome > 0)) {
        sprintf(eventBuf + strlen(eventBuf), " - FAILED errno % d\n", outcome);
    }
    else {
        sprintf(eventBuf + strlen(eventBuf), " - PUT ON WAIT(code % d)\n", outcome);
    }
    puts(eventBuf);
    enqueue(buffer, eventBuf);
}

// ! --------------------------------------------------------------------------
static int pushFdToPendingQueue(FileNode_t* fptr, int fd) {
    /**
     * @brief Puts a fd at the end of the waiting list for the given file.
     * @note Assumes: (1) input is valid (treats bad input as a fatal error), \n
     * (2) the caller has mutual exclusion over the file, \n
     * (3) the fd isn't already present in the queue (would go against the semantics of `lockFile`)
     *
     * @return 0 on success, -1 if memory for the node couldn't be allocated
     *
     */

    assert(fptr);
    assert(fd > 0);

    struct fdNode* newNode = malloc(sizeof(*newNode));
    if (!newNode) {
        return -1;
    }

    newNode->fd = fd;
    newNode->nextPtr = NULL;

    struct fdNode** target = &(fptr->pendingLocks_hPtr);

    fflush(NULL);

    while (*target) {
        target = &((*target)->nextPtr);
    }
    *target = newNode;
    return 0;
}

static int popNodeFromFdQueue(FileNode_t* fptr) {
    /**
     * @brief Pops a fd from the top of the waiting list for the given file.
     * @note Assumes: (1) input is valid (treats bad input as a fatal error), (2) the caller has mutual exclusion over the file
     *
     * @return the popped fd, or 0 if the list is empty
     *
     */
    assert(fptr);

    int ret = fptr->pendingLocks_hPtr ? fptr->pendingLocks_hPtr->fd : 0;

    if (fptr->pendingLocks_hPtr) {
        struct fdNode* tmp = fptr->pendingLocks_hPtr;
        fptr->pendingLocks_hPtr = fptr->pendingLocks_hPtr->nextPtr;
        free(tmp);
    }

    return ret;
}

void printFdList(struct fdNode* h) {
    struct fdNode* curr = h;
    while (curr) {
        printf("%d->", curr->fd);
        curr = curr->nextPtr;
    }
    puts("");
}

void freeFdList(struct fdNode* h) {
    struct fdNode* tmp = h;
    while (h) {
        struct fdNode* tmp = h;

        h = h->nextPtr;
        free(tmp);
    }
    puts("");
}
// ! ---------------------------------------------------------------------------


static int destroyFile(CacheStorage_t* store, FileNode_t* fptr, struct fdNode** notifyList) {
    /**
     * @brief Handles eviction of a file from the storage.
     * @note Assumes the caller thread has mutual exclusion over the store. Returns memory allocated on the heap that \n
     * needs to be `free`d.
     *
     * @param store A pointer to the storage containing the file
     * @param fptr A pointer to the file to delete
     * @param notifyList A pointer to a list of file descriptors that were waiting to gain lock of this file. \n
     * *NB*: the caller needs to `free` the list at a later point
     *
     */

    printFileptr(fptr);
    assert(fptr);
    // !!!! can we do this without acquiring ordering?
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    //! permission checking needs to be done somewhere else

    while (fptr->activeReaders > 0) {
        DIE_ON_NZ(pthread_cond_wait(&(fptr->rwCond), &(fptr->mutex)));
    }

    // remove file from the storage list: it's now out of scope and the only way to
    // access it is to have a pointer to it
    if (fptr->prevPtr) {
        fptr->prevPtr->nextPtr = fptr->nextPtr;
    }
    else {
        store->hPtr = fptr->nextPtr;
    }

    if (fptr->nextPtr) {
        fptr->nextPtr->prevPtr = fptr->prevPtr;
    }
    else {
        store->tPtr = NULL;
    }

    // give back to caller the list of clients that were waiting to gain lock of this file
    // the list needs to be later freed by caller
    if (fptr->pendingLocks_hPtr) {
        *notifyList = fptr->pendingLocks_hPtr;
    }


    // there are no more pending requests on the file AND we've gained back mutual exclusion:
    // file is now safe to delete
    store->currFileNum -= 1;
    store->currStorageSize -= fptr->contentSize;

    DIE_ON_NZ(pthread_cond_destroy(&(fptr->rwCond)));

    //DIE_ON_NZ(pthread_mutex_destroy(&(fptr->mutex)));
    //DIE_ON_NZ(pthread_mutex_destroy(&(fptr->ordering)));

    // ? need to do this?
    free(fptr->pathname);
    free(fptr->content);

    free(fptr);
    puts("DELETED");
    fflush(NULL);
    return 0; // ? errors?

}


CacheStorage_t* allocStorage(const size_t maxFileNum, const size_t maxStorageSize, const short replacementAlgo) {
    CacheStorage_t* newStore = calloc(sizeof(*newStore), 1);
    if (!newStore) {
        errno = ENOMEM;
        return NULL;
    }
    newStore->logBuffer = allocBoundedBuffer(EVENT_BUF_CAP, EVENT_SLOT_SIZE + 1);
    if (!newStore->logBuffer) {
        errno = ENOMEM;
        return NULL;
    }

    DIE_ON_NZ(pthread_mutex_init(&(newStore->mutex), NULL));
    newStore->maxFileNum = maxFileNum;
    newStore->maxStorageSize = maxStorageSize;
    newStore->replacementAlgo = replacementAlgo;

    return newStore;
}

FileNode_t* allocFile(const char* pathname) {
    FileNode_t* newFile = calloc(sizeof(*newFile), 1);
    if (!newFile) {
        errno = ENOMEM;
        return NULL;
    }

    newFile->pathname = malloc(INITIALBUFSIZ);
    newFile->content = calloc(INITIALBUFSIZ, 1);
    if (!newFile->pathname || !newFile->content) {
        errno = ENOMEM;
        return NULL;
    }

    DIE_ON_NZ(pthread_mutex_init(&(newFile->mutex), NULL));

    // TODO handle dynamic resizing of buffers for larger data
    strncpy(newFile->pathname, pathname, INITIALBUFSIZ);


    return newFile;
}

static FileNode_t* findFile(const CacheStorage_t* store, const char* pathname) {
    /**
     * @brief Looks for a file in the storage.
     *
     * @param store A pointer to the storage to search
     * @param pathname The absolute pathname of the file to look for
     * @return A pointer to the found file, or `NULL` if no file could be found or an error occurred (sets `errno`)
     *
     * `errno` values: \n
     * `ENOENT` requested file could not be found \n
     * `EINVAL` invalid parameter(s)
     */

    if (!store || !strlen(pathname)) {
        errno = EINVAL;
        return NULL;
    }

    FileNode_t* currPtr = store->hPtr;

    while (currPtr && strcmp(currPtr->pathname, pathname)) { //? strncmp?
        currPtr = currPtr->nextPtr;
    }
    if (!currPtr) {
        errno = ENOENT;
    }
    return currPtr;
}


void printFile(const CacheStorage_t* store, const char* pathname) {
    FileNode_t* f = findFile(store, pathname);
    puts("-----------");
    if (!f) {
        printf("%s: file not found\n", pathname);
    }
    else {
        printf("File: %s\nContent: %s\nSize: %zu\nLocked by: %d\nRefCount: %zu\nLastRef: %ld\n", f->pathname, f->content, f->contentSize, f->lockedBy, f->refCount, f->lastRef);
        printf("locked on it:\n");
        printFdList(f->pendingLocks_hPtr);
    }
    puts("-----------");
}

void printFileptr(FileNode_t* f) {
    printf("File: %s\nContent: %s\nLocked by: %d\nRefCount: %zu\nLastRef: %ld\n", f->pathname, f->content, f->lockedBy, f->refCount, f->lastRef);

}

void printStore(const CacheStorage_t* store) {
    printf("curr # files: %zu\n curr storage size: %zu\n Files: \n", store->currFileNum, store->currStorageSize);
    FileNode_t* currptr = store->hPtr;
    while (currptr) {
        puts(currptr->pathname);
        currptr = currptr->nextPtr;
    }
}

void addFileToStore(CacheStorage_t* store, FileNode_t* filePtr) {
    /**
     * @note Operates under the assumption that the caller has mutual exclusion over the store
     *
     */
    if (!store->hPtr) {
        store->hPtr = filePtr;
        //store->tPtr = filePtr; //?

    }
    else {
        filePtr->prevPtr = store->tPtr;
        if (store->tPtr) //?
            store->tPtr->nextPtr = filePtr;
    }
    store->tPtr = filePtr;

    // ? all good? mutex?

    store->currFileNum += 1;
    store->currStorageSize += filePtr->contentSize;

    store->maxReachedFileNum = MAX(store->maxReachedFileNum, store->currFileNum);
    store->maxReachedStorageSize = MAX(store->maxReachedStorageSize, store->currStorageSize);
}



int openFileHandler(CacheStorage_t* store, const char* pathname, int flags, struct fdNode** notifyList, const int requestor) {
    /**
     * @brief Handler for open/create operations, as per the API spec.
     *
     * @param store A pointer to the file storage to use
     * @param pathname Absolute path of the file to open or create
     * @param mode See flags
     * @param requestor Pid of the process requesting the operation
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `EINVAL` for invalid parameter(s) \n
     * `EPERM` if the operation is forbidden: this happens if the requestor is trying to \n
     * open a nonexistent file without O_CREATE flag or if they're trying to open an extisting \n
     * file with O_CREATE flag \n
     * `ENOMEM` memory for the ne file couldn't be allocated
     *
     */
    if (!strlen(pathname) || requestor <= 0) {
        errno = EINVAL;
        return -1;
    }
    assert(notifyList);

    int ret;
    const bool create = IS_SET(O_CREATE, flags); // IS_O_CREATE_SET(mode);
    const bool lock = IS_SET(O_LOCK, flags); //  IS_O_LOCK_SET(mode);
    // printf("O_CREATE %d O_LOCK %d\n", create, lock);

    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    FileNode_t* fPtr = findFile(store, pathname);
    const bool alreadyExists = (fPtr != NULL);

    errno = 0; // we don't care about the error code of findFile here as we are managing both EINVAL and ENOENT directly
    if (alreadyExists == create) {
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        errno = EPERM;
        return -1;
    }

    if (alreadyExists) {
        // ! ret = openFile(fPtr, lock, requestor);
    }
    else {
        if (store->currFileNum == store->maxFileNum) {
            FileNode_t* victim = getVictim(store);
            printf("will delete %s\n", victim->pathname);
            fflush(NULL);
            destroyFile(store, victim, notifyList);
        }
        fPtr = allocFile(pathname);
        if (!fPtr) {
            DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
            return -1;
        }
        // file hasn't yet been linked to the storage; therefore we can modify it without
        // acquiring mutual exclusion because no other thread has access to it yet
        if (lock) {
            fPtr->lockedBy = requestor;
        }
        fPtr->open = true;

        // nothing else will set `errno` from here on if everything is successful, so we can
        // omit error checking here as the return statement will check for errors
        addFileToStore(store, fPtr);
    }

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    return errno ? -1 : 0;
}
int readFileHandler(CacheStorage_t* store, const char* pathname, void** buf, size_t* size, const int requestor) {
    CHECK_INPUT(store, pathname, requestor);
    int errnosave = 0;
    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));
    // ! here was buffer mutex (un)lock

    FileNode_t* fptr = findFile(store, pathname);

    // handle file not found
    if (!fptr) {
        errnosave = errno;
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

        logEvent(store->logBuffer, "READ", pathname, errnosave, requestor);
        errno = errnosave;
        return -1;
    }

    // first critical section: ensures no writers will access the file but grants access to other readers
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));


    // handle file locked by another client
    if (fptr->lockedBy && fptr->lockedBy != requestor) {
        errnosave = EACCES;
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

        logEvent(store->logBuffer, "READ", pathname, errnosave, requestor);

        errno = errnosave;
        return -1;
    }

    while (fptr->isBeingWritten) {
        DIE_ON_NZ(pthread_cond_wait(&(fptr->rwCond), &(fptr->mutex)));
    }

    fptr->activeReaders += 1;

    UPDATE_CACHE_BITS(fptr);

    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    // actual read operation
    char* ret = malloc(fptr->contentSize + 1);
    if (!ret) {
        errnosave = ENOMEM;
    }
    else {
        strncpy(ret, fptr->content, fptr->contentSize);
        ret[fptr->contentSize] = '\0'; // ? necessary?

        *buf = ret;
        size = fptr->contentSize;
    }
    // end actual read operation

    logEvent(store->logBuffer, "READ", pathname, 0, requestor);

    // second critical section: we're done reading so, if no more readers are active, we can let a writer in
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    fptr->activeReaders -= 1;

    if (!fptr->activeReaders) {
        DIE_ON_NZ(pthread_cond_signal(&(fptr->rwCond)));
    }

    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    errno = errnosave ? errnosave : errno;

    return errno ? -1 : 0;
}

int writeToFileHandler(CacheStorage_t* store, const char* pathname, const char* newContent, struct fdNode** notifyList, const int requestor) {
    /**
     * @brief Handles write-to-file requests from client. These can be appends on existing files or a whole new file
     *
     * @param store A pointer to the storage containing the file
     * @param pathname Absolute pathname of the file
     * @param content Content to write or append to file
     * @param requestor Pid of the requesting client process
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `ENOENT' file not found \n
     * `EINVAL` invalid parameters
     */

     // todo add parameter of list of files to hold the evicted files

     // todo check the last operation was open file

    CHECK_INPUT(store, pathname, requestor);

    int errnosave = 0;

    // gain mutual exclusion over the whole store because files might end up being deleted
    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    FileNode_t* fptr = findFile(store, pathname);
    // ! here was buffer mutex (un)lock

    if (!fptr) {
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        errnosave = errno;
        logEvent(store->logBuffer, "WRITE", pathname, errnosave, requestor);
        errno = errnosave;
        return -1;
    }


    // first critical section: ensures no writers or readers will access the file
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    // this can't be true at this time because it would mean two writers both have mutex over the store
    assert(!fptr->isBeingWritten);

    if (fptr->lockedBy && fptr->lockedBy != requestor) {
        errnosave = EACCES;
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        logEvent(store->logBuffer, "WRITE", pathname, errnosave, requestor);
        errno = errnosave;
        return -1;
    }

    while (fptr->activeReaders > 0) {
        DIE_ON_NZ(pthread_cond_wait(&(fptr->rwCond), &(fptr->mutex)));
    }

    fptr->isBeingWritten = true;
    UPDATE_CACHE_BITS(fptr);

    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    // actual write operation
    size_t newContentLen = strlen(newContent); // ? what if it's not nul-terminated?

    // ? what happens if the new content alone is larger than the size of the storage?
    if (newContentLen > store->maxStorageSize) {
        // file cannot be stored because it is too large
        errnosave = E2BIG;
        logEvent(store->logBuffer, "WRITE", pathname, errnosave, requestor);
        goto cleanup;
    }
    // ? what happens if the file evicted to make room for new content is this file itself?
    while (store->currStorageSize + newContentLen > store->maxStorageSize) {
        FileNode_t* victim = getVictim(store);
        assert(victim);
        destroyFile(store, victim, notifyList);
        // todo send file content somehow and log eviction of file
    }

    store->currStorageSize += newContentLen;

    store->maxReachedStorageSize = MAX(store->maxReachedStorageSize, store->currStorageSize);
    size_t oldLen = strlen(fptr->content);
    void* tmp = realloc(fptr->content, fptr->contentSize + newContentLen + 1);
    if (tmp) {
        fptr->content = tmp;
        fptr->content[oldLen] = '\0';
        strncat(fptr->content, newContent, newContentLen);
        fptr->contentSize += newContentLen;
    }
    else {
        errnosave = ENOMEM;
    }
    // end actual write operation
cleanup:
    // second critical section: we're done writing, we can wake up any pending readers and also release the lock over the store
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));
    fptr->isBeingWritten = false;
    DIE_ON_NZ(pthread_cond_broadcast(&(fptr->rwCond))); // wake up pending readers
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));
    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    logEvent(store->logBuffer, "WRITE", pathname, errnosave, requestor);
    errno = errnosave;
    return errno ? -1 : 0;
}
int lockFileHandler(CacheStorage_t* store, const char* pathname, const int requestor) {
    /**
     * @brief Handles lock-file requests from client.
     * @details If the file had previously been locked by another process and hasn't been unlocked yet, \n
     * the requestor is placed in the file's pending lock queue. When the file is eventually unlocked, \n
     * the handler for the unlock will take care of giving the lock to the first requestor on the queue, \n
     * in FIFO order.
     *
     * @param store A pointer to the storage containing the file
     * @param pathname Absolute pathname of the file
     * @param requestor Pid of the requesting client process
     *
     * @return 0 on success, -1 on error (sets `errno`), -2 if the file could not be locked at the moment and the requestor \n
     * has been placed on the pending lock queue of the file.
     *
     * `errno` values: \n
     * `ENOENT' file not found \n
     * `EINVAL` invalid parameters
     */
    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));
    // ! here was buffer mutex (un)lock
    int errnosave = 0;

    FileNode_t* fptr = findFile(store, pathname);

    if (!fptr) {
        errnosave = errno;
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        logEvent(store->logBuffer, "LOCK", pathname, errnosave, requestor);
        errno = errnosave;
        return -1;
    }

    // first critical section: ensures no writers or readers will access the file
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    while (fptr->activeReaders > 0 || fptr->isBeingWritten) {
        DIE_ON_NZ(pthread_cond_wait(&(fptr->rwCond), &(fptr->mutex)));
    }
    if (fptr->lockedBy && fptr->lockedBy != requestor) {
        // lock cannot be gained at the moment: place requestor on waiting queue and return
        DIE_ON_NEG_ONE(pushFdToPendingQueue(fptr, requestor));
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));

        logEvent(store->logBuffer, "LOCK", pathname, -2, requestor);
        return -2;
    }

    fptr->isBeingWritten = true;
    UPDATE_CACHE_BITS(fptr);
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    fptr->lockedBy = requestor;
    logEvent(store->logBuffer, "LOCK", pathname, 0, requestor);

    // second critical section: we're done writing, we can wake up any pending readers and also release the lock over the store
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));
    fptr->isBeingWritten = false;
    DIE_ON_NZ(pthread_cond_broadcast(&(fptr->rwCond))); // wake up pending readers or writers
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    return 0;
}

int unlockFileHandler(CacheStorage_t* store, const char* pathname, int* newLockFd, const int requestor) {
    /**
     * @brief Handles unlock-file requests from client. Wakes up a thread (if any) that was waiting for \n
     *  the file to be unlocked.
     *
     * @param store A pointer to the storage containing the file
     * @param pathname Absolute pathname of the file
     * @param newLockFd output parameter: pointer to int that will contain the fd of the new client that got the lock \n
     * or 0 if no clients were waiting to lock the file
     * @param requestor Pid of the requesting client process
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `ENOENT' file not found \n
     * `EINVAL` invalid parameters
     */
    CHECK_INPUT(store, pathname, requestor);
    int errnosave = 0;
    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    FileNode_t* fptr = findFile(store, pathname);
    if (!fptr) {
        errnosave = errno;
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        logEvent(store->logBuffer, "UNLOCK", pathname, errnosave, requestor);
        errno = errnosave;
        return -1;
    }

    // !! DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    while (fptr->activeReaders > 0 || fptr->isBeingWritten) {
        DIE_ON_NZ(pthread_cond_wait(&(fptr->rwCond), &(fptr->mutex)));
    }
    UPDATE_CACHE_BITS(fptr);

    if (!fptr->lockedBy || fptr->lockedBy == requestor) {
        // will be 0 if no clients are waiting to lock this file; otherwise it'll be the fd of the first client
        // that is stuck waiting to lock
        int newLock = popNodeFromFdQueue(fptr);

        // communicate new lock's fd back to caller
        *newLockFd = newLock;

        fptr->lockedBy = newLock;

    }
    else {
        errnosave = EACCES;
    }
    logEvent(store->logBuffer, "LOCK", pathname, errnosave, requestor);

    DIE_ON_NZ(pthread_cond_broadcast(&(fptr->rwCond))); // wake up pending readers or writers
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    errno = errnosave;
    return errno ? -1 : 0;
}

int closeFileHandler(CacheStorage_t* store, const char* pathname, const int requestor) {
    /**
     * @brief Handles close-file requests from client.
     *
     * @param store A pointer to the storage containing the file
     * @param pathname Absolute pathname of the file
     * @param requestor Pid of the requesting client process
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `ENOENT' file not found \n
     * `EINVAL` invalid parameters
     */
    CHECK_INPUT(store, pathname, requestor);
    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    FileNode_t* fptr = findFile(store, pathname);

    if (!fptr) {
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        return -1;
    }

    // first critical section: ensures no writers or readers will access the file
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));


    while (fptr->activeReaders > 0 || fptr->isBeingWritten) {
        DIE_ON_NZ(pthread_cond_wait(&(fptr->rwCond), &(fptr->mutex)));
    }

    fptr->isBeingWritten = true;
    UPDATE_CACHE_BITS(fptr);

    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    // actual close operation
    fptr->open = false;
    // end actual close operation

    // second critical section: we're done writing, we can wake up any pending readers and also release the lock over the store
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));
    fptr->isBeingWritten = false;
    DIE_ON_NZ(pthread_cond_broadcast(&(fptr->rwCond))); // wake up pending readers or writers
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    return 0;
}
int removeFileHandler(CacheStorage_t* store, const char* pathname, struct fdNode** notifyList, const int requestor) {
    /**
     * @brief Removes a file from the storage
     *
     * @param store A pointer to the storage containing the file
     * @param pathname Absolute pathname of the file
     * @param requestor Pid of the requesting client process
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `ENOENT' file not found \n
     * `EINVAL` invalid parameters \n
     * `EACCES` trying to delete a file that's unlocked or locked by another process
     */
    CHECK_INPUT(store, pathname, requestor);

    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    errno = 0;
    FileNode_t* fptr = findFile(store, pathname);

    if (!fptr || fptr->lockedBy != requestor) {
        errno = !fptr ? ENOENT : EACCES;
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        return -1;
    }

    destroyFile(store, fptr, notifyList);

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    return 0;

}
