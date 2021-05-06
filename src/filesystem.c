#include "../include/filesystem.h"
#include "../include/scerrhand.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#define UPDATE_CACHE_BITS(p)\
    p->lastRef = time(0); \
    p->refCount += 1;

#define INITIALBUFSIZ 1024

#define IS_O_CREATE_SET(i) true; // TODO implement
#define IS_O_LOCK_SET(i) true; // TODO implement


int createOpenHandler(CacheStorage_t* store, const char* pathname, const int mode, const pid_t requestor) {
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

    int ret;
    const bool create = IS_O_CREATE_SET(mode);
    const bool lock = IS_O_LOCK_SET(mode);

    DIE_ON_NZ(pthread_mutex_lock(&(store->mutex))); // ! investigate whether this is needed

    FileNode_t* fPtr = findFile(store, pathname);
    const bool alreadyExists = (fPtr != NULL);

    if (alreadyExists == create) {
        errno = EPERM;
        return -1;
    }

    if (alreadyExists) {
        ret = openFile(fPtr, lock, requestor);
    }
    else {
        fPtr = allocFile(pathname);
        if (!fPtr) {
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

    DIE_ON_NZ(pthread_mutex_unlock(&(store->mutex))); // ! investigate whether this is needed

    return errno ? -1 : 0
}

void printFile(const CacheStorage_t* store, const char* pathname) {
    FileNode_t* f = findFile(store, pathname);
    if (!f) {
        puts("file not found");
    }
    else {
        printf("File: %s\nContent: %s\nLocked by: %d\nRefCount: %zu\nLastRef: %d\n", f->pathname, f->content, f->lockedBy, f->refCount, f->lastRef);
    }
}

static FileNode_t* allocFile(const char* pathname) {
    FileNode_t* newFile = calloc(sizeof(*newFile), 1);
    if (!newFile) {
        errno = ENOMEM;
        return NULL;
    }

    newFile->pathname = malloc(INITIALBUFSIZ);
    newFile->content = malloc(INITIALBUFSIZ);
    if (!newFile->pathname || !newFile->content) {
        errno = ENOMEM;
        return NULL;
    }

    DIE_ON_NZ(pthread_mutex_init(&(newFile->mutex), NULL));
    DIE_ON_NZ(pthread_cond_init(&(newFile->lockedCond), NULL));

    // TODO handle dynamic resizing of buffers for larger data
    strncpy(newFile->pathname, pathname, INITIALBUFSIZ);


    return newFile;
}

FileNode_t* findFile(const CacheStorage_t* store, const char* pathname) {
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

    while (currPtr && strncmp(currPtr->pathname, pathname, strlen(currPtr->pathname))) {
        currPtr = currPtr->nextPtr;
    }
    if (!currPtr) {
        errno = ENOENT;
    }
    return currPtr;
}

int readfile(FileNode_t* filePtr, char* dest, const pid_t requestor) {
    /**
     * @brief Reads the contents of the given file and outputs them to the given buffer.
     * @details This function *allocates a buffer on the heap*, which later needs to be `free`d by the caller.
     *
     * @param filePtr A pointer to the file whose contents are to be read.
     * @param dest A char pointer which, after running the function, will point to a buffer containing the contents of the file
     * @param requestor Pid of the process who wants to read the file
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `EINVAL` invalid parameter(s) \n
     * `EACCES` the file was locked by a different process than the requestor \n
     * `ENOMEM` memory for the output buffer couldn't be allocated
     */
    if (!filePtr || requestor <= 0) {
        errno = EINVAL;
        return -1;
    }

    DIE_ON_NZ(pthread_mutex_lock(&(filePtr->mutex)));

    if (!filePtr->open) {
        errno = EINVAL;
    }
    else if (filePtr->lockedBy && filePtr->lockedBy != requestor) {
        errno = EACCES;
    }

    if (errno) {
        return -1;
    }

    // preliminary checks passed -- onto the actual read now

    char* ret = malloc(filePtr->contentSize + 1);
    if (!ret) {
        errno = ENOMEM;
        return -1;
    }

    strncpy(ret, filePtr->content, filePtr->contentSize);
    ret[filePtr->contentSize] = '\0'; // ? necessary?

    dest = ret;

    UPDATE_CACHE_BITS(filePtr);

    DIE_ON_NZ(pthread_mutex_unlock(&(filePtr->mutex)));

    return 0;
}


int lockFile(FileNode_t* filePtr, const pid_t requestor) {
    /**
     * @brief Locks the requested file under the given pid. If the file is already locked by another process, \n
     * waits until the lock is released.
     *
     * @param filePtr A pointer to the file to lock
     * @param requestor The pid of the process who requested the lock
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `EINVAL` for invalid parameter(s) \n
     *
     */
    if (!filePtr || requestor <= 0) {
        errno = EINVAL;
        return -1;
    }

    DIE_ON_NZ(pthread_mutex_lock(&(filePtr->mutex)));

    if (!filePtr->open) {
        errno = EINVAL; // TODO come up with a better error

        DIE_ON_NZ(pthread_mutex_unlock(&(filePtr->mutex)));
        return -1;

    }

    while (filePtr->lockedBy && filePtr->lockedBy != requestor) {
        DIE_ON_NZ(pthread_cond_wait(&(filePtr->lockedCond), &(filePtr->mutex)));
    }
    filePtr->lockedBy = requestor;

    UPDATE_CACHE_BITS(filePtr);

    DIE_ON_NZ(pthread_mutex_unlock(&(filePtr->mutex)));

    return 0;
}

int unlockFile(FileNode_t* filePtr, const pid_t requestor) {
    /**
     * @brief Unlocks the requested file if the requestor process has previously locked it.
     *
     * @param filePtr A pointer to the file to unlock
     * @param requestor The pid of the process who requested the lock
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `EINVAL` for invalid parameter(s) \n
     * `EACCES` the file was locked by a different process than the requestor
     *
     */
    if (!filePtr || requestor <= 0) {
        errno = EINVAL;
        return -1;
    }

    DIE_ON_NZ(pthread_mutex_lock(&(filePtr->mutex)));

    if (!filePtr->open) {
        errno = EINVAL; // TODO come up with a better error

        DIE_ON_NZ(pthread_mutex_unlock(&(filePtr->mutex)));
        return -1;

    }

    if (!filePtr->lockedBy || filePtr->lockedBy == requestor) {
        filePtr->lockedBy = 0;

        DIE_ON_NZ(pthread_cond_signal(&(filePtr->lockedCond)));

        UPDATE_CACHE_BITS(filePtr);
    }
    else {
        errno = EACCES;
    }

    DIE_ON_NZ(pthread_mutex_unlock(&(filePtr->mutex)));

    return errno ? -1 : 0;
}

int closeFile(FileNode_t* filePtr, const pid_t requestor) {
    /**
     * @brief Closed the requested file if it isn't locked by a different process.
     *
     * @param filePtr A pointer to the file to close
     * @param requestor The pid of the process who requested the closure
     *
     * @return 0 on success, -1 on error (sets `errno`)
     *
     * `errno` values: \n
     * `EINVAL` for invalid parameter(s) \n
     * `EACCES` the file was locked by a different process than the requestor
     *
     */
    if (!filePtr || requestor <= 0) {
        errno = EINVAL;
        return -1;
    }

    DIE_ON_NZ(pthread_mutex_lock(&(filePtr->mutex)));

    if (!filePtr->open) {
        errno = EINVAL; // TODO come up with a better error

        DIE_ON_NZ(pthread_mutex_unlock(&(filePtr->mutex)));
        return -1;

    }

    if (!filePtr->lockedBy || filePtr->lockedBy == requestor) {
        filePtr->open = false;
    }
    else {
        errno = EACCES;
    }

    DIE_ON_NZ(pthread_mutex_lock(&(filePtr->mutex)));

    return errno ? -1 : 0;
}