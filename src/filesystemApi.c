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
#include "../include/cacheFns.h"
#include "../include/clientServerProtocol.h"

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


static FileNode_t* getVictim(CacheStorage_t* store, FileNode_t* spare) {
    /**
     * @brief Runs the replacement algorithm that `store` is using to find a file eligible to be evicted.
     *
     * @param spare Pointer to a file that should never be chosen as the victim
     * @note Assumes the caller has mutual exclusion over the store
     *
     * @return A pointer to the victim
     *
     */
    FileNode_t* victim = (store->hPtr != spare) ? store->hPtr : store->hPtr->nextPtr;
    if (store->replacementAlgo != FIFO_ALGO) {
        FileNode_t* currPtr = store->hPtr;
        while (currPtr) {
            if (currPtr != spare && (*cmp_fns[store->replacementAlgo % 3])(currPtr, victim) > 0) {
                victim = currPtr;
            }
            currPtr = currPtr->nextPtr;
        }
    }
    store->numVictims += 1;
    return victim;

}

// !!
// todo remove `static` and add logging for `openFile`, `closeFile`, `removeFile`, `getVictim`, and in server.c for open connection and close connection
static int logEvent(BoundedBuffer* buffer, const char* op, const char* pathname, int outcome, int requestor, size_t processedSize) {
    char eventBuf[EVENT_SLOT_SIZE];
    time_t current_time;
    struct tm* time_info;
    char timeString[9];  // space for "HH:MM:SS\0"

    time(&current_time);
    // ! not re-entrant: fix with localtime_r
    time_info = localtime(&current_time);
    // ! investigate this too: asctime_r? is `time` re-entrant?
    strftime(timeString, sizeof(timeString), "%H:%M:%S", time_info);

    sprintf(
        eventBuf, "\t{\n\t\t\"timestamp\": \"%s\",\n\t\t\"clientFd\": %d,\n\t\t\"workerTid\": %ld,\n\t\t\"operationType\": \"%s\",\n\t\t\"filePath\": \"%s\",",
        timeString, requestor, pthread_self(), op, pathname
    );
    if (!(outcome)) {
        sprintf(
            eventBuf + strlen(eventBuf), "\n\t\t\"outcome\": \"OK\",\n\t\t\"bytesProcessed\": %zu\n\t},\n",
            processedSize
        );
    }
    else if ((outcome > 0)) {
        sprintf(eventBuf + strlen(eventBuf), "\n\t\t\"outcome\": \"failed\",\n\t\t\"errorCode\": %d\n\t},\n", outcome);
    }
    else {
        sprintf(eventBuf + strlen(eventBuf), "\n\t\t\"outcome\": \"client put on wait (code % d)\"\n\t},\n", outcome);
    }
    return enqueue(buffer, eventBuf, strlen(eventBuf) + 1);
}

// ! --------------------------------------------------------------------------
static void concatenateFdLists(struct fdNode** dest, struct fdNode* src) {
    /**
     * @brief Makes the last element of list `dest` point to the head of list `src`
     *
     * @note Assumes: (1) input is valid (treats bad input as a fatal error), \n
     * (2) the caller has mutual exclusion over the file to which the list belongs, \n
     *
     * @param dest The list to which `src` will be concatenated
     * @param stc The list that will be added at the tail of `dest`
     *
     */
    if (*dest == NULL) {
        *dest = src;
        return;
    }

    while ((*dest)->nextPtr) {
        dest = &((*dest)->nextPtr);
    }
    (*dest)->nextPtr = src;
}

static int pushFdToList(struct fdNode** listPtr, int fd) {
    /**
     * @brief Puts a fd at the end of the given list.
     * @note Assumes: (1) input is valid (treats bad input as a fatal error), \n
     * (2) the caller has mutual exclusion over the file to which the list belongs, \n
     * (3) the fd isn't already present in the queue
     *
     * @return 0 on success, -1 if memory for the node couldn't be allocated
     *
     */

    assert(fd > 0);

    struct fdNode* newNode = malloc(sizeof(*newNode));
    if (!newNode) {
        return -1;
    }

    newNode->fd = fd;
    newNode->nextPtr = NULL;

    struct fdNode** target = (listPtr);
    while (*target) {
        target = &((*target)->nextPtr);
    }
    *target = newNode;
    return 0;
}

bool isFdInList(struct fdNode* listPtr, int fd) {
    struct fdNode* currPtr = listPtr;
    while (currPtr) {
        if (currPtr->fd == fd) {
            return true;
        }
        currPtr = currPtr->nextPtr;
    }
    return false;
}

static int popNodeFromFdQueue(struct fdNode** listPtr, int fd) {
    /**
     * @brief Pops `fd` from the list, or the element at the head of the list if `fd` is -1.
     * @note Assumes: (1) input is valid (treats bad input as a fatal error), (2) the caller has mutual exclusion over the file
     *
     * @return the popped fd, or 0 if the list is empty or the requested fd isn't present
     *
     */

    if (!(*listPtr)) {
        return 0;
    }

    struct fdNode* ret = NULL;
    if (fd == -1) {
        ret = *listPtr;
        *listPtr = (*listPtr)->nextPtr;
    }
    else {
        struct fdNode* currPtr = *listPtr, * prevPtr = NULL;
        while (currPtr) {
            if (currPtr->fd == fd) {
                ret = currPtr;
                if (prevPtr) {
                    prevPtr->nextPtr = currPtr->nextPtr;
                }
                else {
                    *listPtr = currPtr->nextPtr;
                }
                break;
            }
            prevPtr = currPtr;
            currPtr = currPtr->nextPtr;
        }
    }
    if (!ret) {
        return 0;
    }

    int retval = ret->fd;
    free(ret);

    return retval;
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
    while (h) {
        struct fdNode* tmp = h;

        h = h->nextPtr;
        free(tmp);
    }
    puts("");
}
// ! ---------------------------------------------------------------------------

void deallocFile(FileNode_t* fptr) {
    assert(fptr);

    free(fptr->pathname);
    free(fptr->content);

    free(fptr);
}

static void destroyFile(CacheStorage_t* store, FileNode_t* fptr, struct fdNode** notifyList, bool deallocMem) {
    /**
     * @brief Handles eviction of a file from the storage.
     * @note Assumes the caller thread has mutual exclusion over the store. Returns memory allocated on the heap that \n
     * needs to be `free`d.
     *
     * @param store A pointer to the storage containing the file
     * @param fptr A pointer to the file to delete
     * @param notifyList A pointer to a list of file descriptors that were waiting to gain lock of this file. \n
     * *NB*: the caller needs to `free` the list at a later point
     * @param deallocMem If `false`, the file will be removed from the storage but it won't be `free`d. This allows \n
     * the caller to retain a pointer to the file and `free` it later. This is used for sending evicted files back to the client
     *
     */

    assert(fptr);
    assert(store);

    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    while (fptr->activeReaders > 0 || fptr->isBeingWritten) {
        DIE_ON_NZ(pthread_cond_wait(&(fptr->rwCond), &(fptr->mutex)));
    }

    // remove file from the storage list: it's now out of scope and the only
    // way to access it is by having a pointer to it
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
        store->tPtr = fptr->prevPtr;
    }

    // give back to caller the list of clients that were waiting to gain lock of this file;
    // the list needs to be later freed by caller
    if (fptr->pendingLocks_hPtr && notifyList) {
        *notifyList = fptr->pendingLocks_hPtr;
    }

    // destroy list of clients that opened this file
    while (fptr->openDescriptors) {
        struct fdNode* tmp = fptr->openDescriptors;
        fptr->openDescriptors = fptr->openDescriptors->nextPtr;
        free(tmp);
    }

    // there are no more pending requests on the file: file is now safe to delete
    store->currFileNum -= 1;
    store->currStorageSize -= fptr->contentSize;

    DIE_ON_NZ(pthread_cond_destroy(&(fptr->rwCond)));

    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    DIE_ON_NZ(pthread_mutex_destroy(&(fptr->mutex)));
    DIE_ON_NZ(pthread_mutex_destroy(&(fptr->ordering)));

    // delete from dictionary
    assert(icl_hash_delete(store->dictStore, fptr->pathname, NULL, NULL) == 0); // must succeed

    if (deallocMem) {
        deallocFile(fptr);
    }
}


CacheStorage_t* allocStorage(const size_t maxFileNum, const size_t maxStorageSize, const short replacementAlgo) {
    CacheStorage_t* newStore = calloc(sizeof(*newStore), 1);
    if (!newStore) {
        errno = ENOMEM;
        return NULL;
    }
    newStore->logBuffer = allocBoundedBuffer(EVENT_BUF_CAP, EVENT_SLOT_SIZE + 1);
    if (!newStore->logBuffer) {
        free(newStore);
        errno = ENOMEM;
        return NULL;
    }
    newStore->dictStore = icl_hash_create(maxFileNum / 10 + 1, NULL, NULL);
    if (!newStore->dictStore) {
        free(newStore->logBuffer);
        free(newStore);
        errno = ENOMEM;
        return NULL;
    }

    DIE_ON_NZ(pthread_mutex_init(&(newStore->mutex), NULL));
    newStore->maxFileNum = maxFileNum;
    newStore->maxStorageSize = maxStorageSize;
    newStore->replacementAlgo = replacementAlgo;

    return newStore;
}

int destroyStorage(CacheStorage_t* store) {
    /**
     * @note: Assumes it will be called once all but one threads have die (therefore no mutex required)
     */
    if (!store) {
        errno = EINVAL;
        return -1;
    }
    // free all files
    FileNode_t* tmp;

    while (store->hPtr) {
        tmp = store->hPtr;
        store->hPtr = store->hPtr->nextPtr;
        destroyFile(store, tmp, NULL, true);
    }

    // destroy data structures and mutex
    destroyBoundedBuffer(store->logBuffer);
    icl_hash_destroy(store->dictStore, NULL, NULL);
    DIE_ON_NEG_ONE(pthread_mutex_destroy(&(store->mutex)));

    free(store);
    return 0;
}


FileNode_t* allocFile(const char* pathname) {
    FileNode_t* newFile = calloc(sizeof(*newFile), 1);
    if (!newFile) {
        errno = ENOMEM;
        return NULL;
    }

    newFile->pathname = malloc(INITIALBUFSIZ);
    newFile->content = calloc(INITIALBUFSIZ, 1);
    newFile->insertionTime = time(0);

    if (!newFile->pathname || !newFile->content) {
        errno = ENOMEM;
        return NULL;
    }

    DIE_ON_NZ(pthread_mutex_init(&(newFile->mutex), NULL));

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


    void* ret = icl_hash_find(store->dictStore, (void*)pathname);
    if (!ret) {
        errno = ENOENT;
    }
    return (FileNode_t*)ret;
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
        printf("open:\n");
        printFdList(f->openDescriptors);
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

     // add file to list structure
    if (!store->hPtr) {
        store->hPtr = filePtr;

    }
    else {
        filePtr->prevPtr = store->tPtr;
        if (store->tPtr) {
            store->tPtr->nextPtr = filePtr;
        }
    }
    store->tPtr = filePtr;

    // add file to dict structure
    icl_hash_insert(store->dictStore, filePtr->pathname, filePtr); // todo manage error

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
     * @param requestor Fd of the process requesting the operation
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

    int errnosave = 0;

    const bool create = IS_SET(O_CREATE, flags);
    const bool lock = IS_SET(O_LOCK, flags);

    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    FileNode_t* fPtr = findFile(store, pathname);
    const bool alreadyExists = (fPtr != NULL);

    errno = 0; // we don't care about the error code of findFile here as we are managing both EINVAL and ENOENT directly
    if (alreadyExists == create) {
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        errno = alreadyExists ? EPERM : ENOENT;
        return -1;
    }

    if (!alreadyExists) {
        if (store->currFileNum == store->maxFileNum) {
            FileNode_t* victim = getVictim(store, NULL);
            destroyFile(store, victim, notifyList, true);
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
            fPtr->canDoFirstWrite = requestor;
        }

        DIE_ON_NEG_ONE(pushFdToList(&(fPtr->openDescriptors), requestor));

        // nothing else will set `errno` from here on if everything is successful, so we can
        // omit error checking here as the return statement will check for errors
        addFileToStore(store, fPtr);
    }
    else {
        DIE_ON_NZ(pthread_mutex_lock(&(fPtr->ordering)));
        DIE_ON_NZ(pthread_mutex_lock(&(fPtr->mutex)));
        if (lock) {
            if (!fPtr->lockedBy) {
                fPtr->lockedBy = requestor;
            }
            else { // file is already locked: it can't be opened with a lock
                errnosave = EACCES;
            }
        }
        if (!errnosave) {
            DIE_ON_NEG_ONE(pushFdToList(&(fPtr->openDescriptors), requestor));
        }
        DIE_ON_NZ(pthread_mutex_unlock(&(fPtr->ordering)));
        DIE_ON_NZ(pthread_mutex_unlock(&(fPtr->mutex)));

    }
    // add requestor to the list of clients that opened this file

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
    errno = errnosave;
    return errno ? -1 : 0;
}
int readFileHandler(CacheStorage_t* store, const char* pathname, void** buf, size_t* size, const int requestor) {
    CHECK_INPUT(store, pathname, requestor);
    int errnosave = 0;
    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    FileNode_t* fptr = findFile(store, pathname);

    // handle file not found
    if (!fptr) {
        errnosave = errno;
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

        logEvent(store->logBuffer, "READ", pathname, errnosave, requestor, 0);
        errno = errnosave;
        return -1;
    }

    // first critical section: ensures no writers will access the file but grants access to other readers
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));


    // handle file locked by another client or file hasn't been opened by the client
    if ((fptr->lockedBy && fptr->lockedBy != requestor) || !isFdInList(fptr->openDescriptors, requestor)) {
        errnosave = EACCES;
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

        logEvent(store->logBuffer, "READ", pathname, errnosave, requestor, 0);

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
        memcpy(ret, fptr->content, fptr->contentSize);
        //ret[fptr->contentSize] = '\0'; // ? necessary with bin?

        *buf = ret;
        *size = fptr->contentSize;
    }
    // end actual read operation

    logEvent(store->logBuffer, "READ", pathname, errnosave, requestor, (errnosave ? 0 : fptr->contentSize));

    // second critical section: we're done reading so, if no more readers are active, we can let a writer in
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    fptr->activeReaders -= 1;

    fptr->canDoFirstWrite = 0; // last operation on this file isn't `openFile` with `O_LOCK|O_CREATE` anymore because a successful operation was done on it

    if (!fptr->activeReaders) {
        DIE_ON_NZ(pthread_cond_signal(&(fptr->rwCond)));
    }

    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    errno = errnosave;

    return errno ? -1 : 0;
}

int readNFilesHandler(CacheStorage_t* store, const long upperLimit, void** buf, size_t* size) {
    if (!store) {
        errno = EINVAL;
        return -1;
    }
    int errnosave = 0;

    int readCount = 0;
    size_t
        retMaxSize = INITIALBUFSIZ,
        retCurrSize = 0;

    char* ret = calloc(INITIALBUFSIZ, 1);
    if (!ret) {
        errno = ENOMEM;
        return -1;
    }

    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));
    FileNode_t* currPtr = store->hPtr;

    while (currPtr && (readCount != upperLimit || upperLimit <= 0)) {
        size_t retNewSize = retCurrSize + currPtr->contentSize + strlen(currPtr->pathname) + 2 * METADATA_SIZE;
        if (retNewSize > retMaxSize) {
            void* tmp = realloc(ret, 2 * retNewSize);
            if (!tmp) {
                errnosave = ENOMEM;
                free(ret);
                goto cleanup;
            }
            ret = tmp;
            retMaxSize = 2 * retMaxSize;
        }
        sprintf(
            ret + retCurrSize,
            "%08ld%s%08ld",
            strlen(currPtr->pathname), currPtr->pathname, currPtr->contentSize
        );
        memcpy((ret + retNewSize - (currPtr->contentSize)), currPtr->content, currPtr->contentSize);
        retCurrSize = retNewSize;

        currPtr = currPtr->nextPtr;
        readCount += 1;
    }

cleanup:
    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    *buf = ret;
    *size = retCurrSize;

    //printf("return %s\n return length %zu\n actual length %zu\n", ret, retCurrSize, strlen(ret));

    errno = errnosave ? errnosave : errno;
    return errno ? -1 : readCount;
}

int writeToFileHandler(CacheStorage_t* store, const char* pathname, const char* newContent, const size_t newContentLen, struct fdNode** notifyList, FileNode_t** evictedList, const int requestor) {
    /**
     * @brief Handles write-to-file requests from client. These can be appends on existing files or a whole new file
     *
     * @param store A pointer to the storage containing the file
     * @param pathname Absolute pathname of the file
     * @param content Content to write or append to file
     * @param requestor Fd of the requesting client process
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `ENOENT' file not found \n
     * `EINVAL` invalid parameters
     */

    CHECK_INPUT(store, pathname, requestor);

    int errnosave = 0;

    // gain mutual exclusion over the whole store because files might end up being deleted
    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    FileNode_t* fptr = findFile(store, pathname);

    if (!fptr) {
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        errnosave = errno;
        logEvent(store->logBuffer, "WRITE", pathname, errnosave, requestor, 0);
        errno = errnosave;
        return -1;
    }

    // first critical section: ensures no writers or readers will access the file
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    // this can't be true at this time because it would mean two writers both have mutex over the store
    assert(!fptr->isBeingWritten);

    if ((fptr->lockedBy && fptr->lockedBy != requestor) || !isFdInList(fptr->openDescriptors, requestor)) {
        errnosave = EACCES;
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        logEvent(store->logBuffer, "WRITE", pathname, errnosave, requestor, 0);
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
    // ! here you would do the compression and do everything with the compressed size
    if (fptr->contentSize + newContentLen > store->maxStorageSize) {
        // file cannot be stored because it is too large
        errnosave = E2BIG;
        logEvent(store->logBuffer, "WRITE", pathname, errnosave, requestor, 0);
        goto cleanup;
    }
    while (store->currStorageSize + newContentLen > store->maxStorageSize) {
        // we pass `fptr` as the second param to `getVictim` to prevent the file we're writing to from being chosen as the victim
        FileNode_t* victim = getVictim(store, fptr);
        assert(victim);
        struct fdNode* tmpList = NULL; // will hold a list of fd's that were waiting on this file before it got deleted
        destroyFile(store, victim, &tmpList, false);

        // build a list of evicted files
        victim->nextPtr = *evictedList;
        *evictedList = victim;

        // make a single list with all the clients that need to be notified that a file they were blocked on doesn't exist (anymore)
        concatenateFdLists(notifyList, tmpList);
        // todo log eviction of file
    }

    //size_t oldLen = fptr->contentSize;

    store->currStorageSize += newContentLen;
    store->maxReachedStorageSize = MAX(store->maxReachedStorageSize, store->currStorageSize);

    void* tmp = realloc(fptr->content, fptr->contentSize + newContentLen + 1); //? +1 needed with bin?
    if (tmp) {
        fptr->content = tmp;
        // !!!!! commented because we shouldn't need it with binaries
        //fptr->content[oldLen] = '\0';
        // append new content to file
        memcpy((fptr->content + fptr->contentSize), newContent, newContentLen);
        fptr->contentSize += newContentLen;
    }
    else {
        errnosave = ENOMEM;
    }
    // end actual write operation
    logEvent(store->logBuffer, "WRITE", pathname, errnosave, requestor, (errnosave ? 0 : newContentLen));
    fptr->canDoFirstWrite = 0; // last operation on this file isn't `openFile` with `O_LOCK|O_CREATE` anymore because a successful operation was done on it

cleanup:
    // second critical section: we're done writing, we can wake up any pending readers and also release the lock over the store
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));
    fptr->isBeingWritten = false;
    DIE_ON_NZ(pthread_cond_broadcast(&(fptr->rwCond)));
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));
    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

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
     * @param requestor Fd of the requesting client process
     *
     * @return 0 on success, -1 on error (sets `errno`), -2 if the file could not be locked at the moment and the requestor \n
     * has been placed on the pending lock queue of the file.
     *
     * `errno` values: \n
     * `ENOENT' file not found \n
     * `EINVAL` invalid parameters
     */
    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));
    int errnosave = 0;

    FileNode_t* fptr = findFile(store, pathname);

    if (!fptr) {
        errnosave = errno;
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        logEvent(store->logBuffer, "LOCK", pathname, errnosave, requestor, 0);
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
        DIE_ON_NEG_ONE(pushFdToList(&(fptr->pendingLocks_hPtr), requestor));
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));
        DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));

        logEvent(store->logBuffer, "LOCK", pathname, -2, requestor, 0);
        return -2;
    }

    fptr->isBeingWritten = true;
    UPDATE_CACHE_BITS(fptr);
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    fptr->lockedBy = requestor;
    logEvent(store->logBuffer, "LOCK", pathname, 0, requestor, 0);

    // second critical section: we're done writing, we can wake up any pending readers and also release the lock over the store
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));
    fptr->isBeingWritten = false;
    fptr->canDoFirstWrite = 0; // last operation on this file isn't `openFile` with `O_LOCK|O_CREATE` anymore because a successful operation was done on it
    DIE_ON_NZ(pthread_cond_broadcast(&(fptr->rwCond))); // wake up pending readers or writers
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    return 0;
}

int clientExitHandler(CacheStorage_t* store, struct fdNode** notifyList, const int requestor) {
    /**
     * @brief Routine called each time a client closes connection. Releases lock from all files that the client \n
     * had locked and notifies the first clients in line to acquire the lock over those files.
     *
     * @param store A pointer to the storage containing the file
     * @param notifyList output parameter: pointer to a list of fd's who are blocked waiting to acquire lock on a file \n
     * whose lock was just released by the client's exit
     * @param requestor Fd of the requesting client process
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `EINVAL` invalid parameters
     */
    if (!store || requestor <= 0) {
        errno = EINVAL;
        return -1;
    }

    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    FileNode_t* currPtr = store->hPtr;
    while (currPtr) {
        DIE_ON_NZ(pthread_mutex_lock(&(currPtr->ordering)));
        DIE_ON_NZ(pthread_mutex_lock(&(currPtr->mutex)));

        while (currPtr->activeReaders > 0 || currPtr->isBeingWritten) {
            DIE_ON_NZ(pthread_cond_wait(&(currPtr->rwCond), &(currPtr->mutex)));
        }

        if (currPtr->lockedBy == requestor) {
            // will be 0 if no clients are waiting to lock this file; otherwise it'll be the fd of the
            // first client that is stuck waiting to lock
            int newLock = popNodeFromFdQueue(&(currPtr->pendingLocks_hPtr), -1);

            // communicate new lock's fd back to caller
            if (newLock > 0) {
                pushFdToList(notifyList, newLock);
            }

            currPtr->lockedBy = newLock;
        }

        // if client was blocked on a file waiting to lock it, remove it from the waiting list
        popNodeFromFdQueue(&(currPtr->pendingLocks_hPtr), requestor);

        DIE_ON_NZ(pthread_mutex_unlock(&(currPtr->ordering)));
        DIE_ON_NZ(pthread_mutex_unlock(&(currPtr->mutex)));

        currPtr = currPtr->nextPtr;
    }

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
    return 0;
}

int unlockFileHandler(CacheStorage_t* store, const char* pathname, int* newLockFd, const int requestor) {
    /**
     * @brief Handles unlock-file requests from client. Wakes up a thread (if any) that was waiting for \n
     * the file to be unlocked.
     *
     * @param store A pointer to the storage containing the file
     * @param pathname Absolute pathname of the file
     * @param newLockFd output parameter: pointer to int that will contain the fd of the new client that got the lock \n
     * or 0 if no clients were waiting to lock the file
     * @param requestor Fd of the requesting client process
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
        logEvent(store->logBuffer, "UNLOCK", pathname, errnosave, requestor, 0);
        errno = errnosave;
        return -1;
    }

    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    while (fptr->activeReaders > 0 || fptr->isBeingWritten) {
        DIE_ON_NZ(pthread_cond_wait(&(fptr->rwCond), &(fptr->mutex)));
    }
    UPDATE_CACHE_BITS(fptr);

    if (fptr->lockedBy == requestor) {
        // will be 0 if no clients are waiting to lock this file; otherwise it'll be the fd of the first client
        // that is stuck waiting to lock
        int newLock = popNodeFromFdQueue(&(fptr->pendingLocks_hPtr), -1);

        // communicate new lock's fd back to caller
        *newLockFd = newLock; //? write log event LOCK for new lock

        fptr->lockedBy = newLock;
        fptr->canDoFirstWrite = 0; // last operation on this file isn't `openFile` with `O_LOCK|O_CREATE` anymore because a successful operation was done on it
    }
    else {
        errnosave = EACCES;
    }
    logEvent(store->logBuffer, "UNLOCK", pathname, errnosave, requestor, 0);

    DIE_ON_NZ(pthread_cond_broadcast(&(fptr->rwCond))); // wake up pending readers or writers
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
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
     * @param requestor Fd of the requesting client process
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
    // remove requestor from list of fd's that opened this file
    popNodeFromFdQueue(&(fptr->openDescriptors), requestor);
    // end actual close operation

    // second critical section: we're done writing, we can wake up any pending readers and also release the lock over the store
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));
    fptr->isBeingWritten = false;
    fptr->canDoFirstWrite = 0; // last operation on this file isn't `openFile` with `O_LOCK|O_CREATE` anymore because a successful operation was done on it
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
     * @param requestor Fd of the requesting client process
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

    destroyFile(store, fptr, notifyList, true);

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    return 0;

}

bool testFirstWrite(CacheStorage_t* store, const char* pathname, const int requestor) {
    if (!store || !strlen(pathname) || requestor <= 0) {
        errno = EINVAL;
        return false;
    }
    bool ret;

    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex)));

    FileNode_t* fptr = findFile(store, pathname);

    if (!fptr) {
        DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));
        return -1;
    }

    DIE_ON_NZ(pthread_mutex_lock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_lock(&(fptr->mutex)));

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex)));

    ret = (fptr->canDoFirstWrite == requestor);

    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->ordering)));
    DIE_ON_NZ(pthread_mutex_unlock(&(fptr->mutex)));

    return ret;
}
