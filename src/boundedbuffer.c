/*! \file */


#include "../include/boundedbuffer.h"
#include "../utils/scerrhand.h"
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#define MIN(a,b) (a) <= (b) ? (a) : (b)

struct _node {
    /**
    * @brief A buffer node.
    */

    void* data;
    struct _node* nextPtr;
};

struct _boundedBuffer {
    /**
    * @brief A concurrent buffer with limited capacity.
    */

    size_t capacity; /**< Maximum number of elements that can be in the buffer at once */
    size_t numElements; /**< Current number of elements in the buffer */
    size_t dataSize; /**< Size of the data type of the elements in the buffer */
    struct _node* headPtr; /**< Pointer to first element */
    struct _node* tailPtr; /**< Pointer to last element */
    pthread_mutex_t mutex; /**< A mutex for ensuring mutual exclusion access to the buffer */
    pthread_cond_t empty; /**< Condition variable used to track whether the buffer is empty */
    pthread_cond_t full; /**< Condition variable used to track whether the buffer is full */
};


static struct _node* _allocNode(void* data, size_t dataSize, size_t upTo) {
    /**
    * @brief Allocates a new node for the bounded buffer, initializing its value to
    * the given one, and returns it.
    *
    * @param data A pointer to the data the new node is going to have
    * @param dataSize The size of the data
    * @param upTo If > 0, up to `upTo` bytes of data will be copied into the new node
    *
    * @return A pointer to the new node
    * @return NULL if the node could not be allocated.
    */

    struct _node* newNode;
    if (!(newNode = malloc(sizeof(*newNode)))) {
        // malloc failed; return NULL to caller
        return NULL;
    }

    if (!(newNode->data = malloc(dataSize))) {
        // malloc failed; return NULL to caller
        return NULL;
    }

    // copy data into new node
    memcpy(newNode->data, data, (upTo ? upTo : dataSize));

    newNode->nextPtr = NULL;

    return newNode;
}


BoundedBuffer* allocBoundedBuffer(size_t capacity, size_t dataSize) {
    /**
    * @brief Initializes and returns a new empty buffer with the given capacity.
    * @param capacity Maximum capacity of the buffer
    * @param dataSize Size of the elements in the buffer
    * @return A pointer to the newly created buffer upon success, NULL on error (sets `errno`)
    *
    * Upon error, `errno` will have one of the following values:\n
    * `ENOMEM`: memory for the buffer couldn't be allocated\n
    * `EINVAL`: invalid parameter(s) were passed
    */

    if (capacity <= 0) {
        errno = EINVAL;
        return NULL;
    }

    BoundedBuffer* buf = malloc(sizeof(BoundedBuffer));
    if (!buf) { // malloc failed; return NULL to caller
        errno = ENOMEM;
        return NULL;
    }

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t full;
    pthread_cond_t empty;

    // initialize condition variables
    DIE_ON_NZ(pthread_cond_init(&full, NULL));
    DIE_ON_NZ(pthread_cond_init(&empty, NULL));

    buf->capacity = capacity;
    buf->dataSize = dataSize;
    buf->numElements = 0;
    buf->headPtr = NULL;
    buf->tailPtr = NULL;
    buf->mutex = mutex;
    buf->empty = empty;
    buf->full = full;

    return buf;
}


int dequeue(BoundedBuffer* buf, void* dest, size_t destSize) {
    /**
    * @brief Pops the node at the head of the buffer and returns its value.
    * If the buffer is empty, waits until there is at least one element in it.
    *
    * @param buf A pointer to the buffer from which to dequeue the node
    * @param dest A pointer to a location to save the popped data. Can be NULL if data isn't to
    * be saved but rather just destroyed
    *
    * @return 0 on success, -1 on error (sets `errno`)
    *
    * Upon error, `errno` will have one of the following values:\n
    * `EINVAL`: invalid parameter(s) were passed
    */

    if (!buf || (!dest && destSize <= 0)) {
        // NULL can be passed as dest if we just want to pop the element without saving it; however
        // if we want to save it somewhere, destSize must be greater than 0 bytes
        errno = EINVAL;
        return -1;
    }


    DIE_ON_NZ(pthread_mutex_lock(&(buf->mutex))); // gain mutual exclusion access

    while (buf->numElements == 0) { // buffer is empty: wait
        DIE_ON_NZ(pthread_cond_wait(&(buf->empty), &(buf->mutex)));
    }

    struct _node* node = buf->headPtr; // get buffer head node
    if (dest) {
        // copy node data to destination
        memcpy(dest, node->data, MIN(buf->dataSize, destSize));
    }
    buf->headPtr = node->nextPtr;
    buf->numElements--;

    if (buf->numElements == 0) {
        buf->tailPtr = NULL;
    }

    if (buf->numElements == buf->capacity - 1) { // buffer was full before we dequeued
        DIE_ON_NZ(pthread_cond_broadcast(&(buf->full))); // wake up a producer thread (if any)
    }

    DIE_ON_NZ(pthread_mutex_unlock(&(buf->mutex))); // waive mutual exclusion access

    // done outside of critical section to avoid doing costly syscalls in mutual exclusion uselessly
    free(node->data);
    free(node);
    return 0;
}

int destroyBoundedBuffer(BoundedBuffer* buf) {
    /**
    * @brief Frees every remaining element in the buffer, then frees the buffer.
    * @param buf Pointer to the buffer to free
    *
    * @return 0 on success, -1 on error (sets `errno`)
    *
    * Upon error, `errno` will have one of the following values:\n
    * `EINVAL`: invalid parameter(s) were passed
    */

    if (!buf) {
        errno = EINVAL;
        return -1;
    }

    while (buf->numElements) {
        dequeue(buf, NULL, 0);
    }

    assert(&buf->empty);
    DIE_ON_NZ(pthread_cond_destroy(&buf->empty));

    assert(&buf->full);
    DIE_ON_NZ(pthread_cond_destroy(&buf->full));

    assert(&buf->mutex);
    DIE_ON_NZ(pthread_mutex_destroy(&buf->mutex));

    free(buf);
    return 0;
}

int enqueue(BoundedBuffer* buf, void* data, size_t upTo) {
    /**
    * @brief Allocates a new node with the given value and pushes it to the tail
    * of the bounded buffer. If the buffer is full, waits until there is at least one free spot.
    *
    * @param buf is the buffer the data is going to be pushed to
    * @param data is a pointer to the data to be pushed
    * @param upTo If > 0, up to `upTo` bytes of data will be copied into the new node
    *
    * @return 0 on success, -1 on error (sets `errno`)
    *
    * Upon error, `errno` will have one of the following values:\n
    * `ENOMEM`: memory for the new node couldn't be allocated\n
    * `EINVAL`: invalid parameter(s) were passed
    */


    if (!buf || !data) {
        errno = EINVAL;
        return -1;
    }

    // allocate new node outside of critical section to keep it as short as possible
    struct _node* newNode = _allocNode(data, buf->dataSize, upTo);

    if (!newNode) { // malloc failed; return -1 to caller
        errno = ENOMEM;
        return -1;
    }
    DIE_ON_NZ(pthread_mutex_lock(&(buf->mutex))); // gain mutual exclusion access

    while (buf->numElements == buf->capacity) { // buffer is full: wait
        DIE_ON_NZ(pthread_cond_wait(&(buf->full), &(buf->mutex)));
    }

    if (buf->numElements) {
        buf->tailPtr->nextPtr = newNode;
    }
    else {
        buf->headPtr = newNode;
    }
    buf->tailPtr = newNode;
    buf->numElements++;

    if (buf->numElements == 1) { // buffer was empty before we enqueued
        DIE_ON_NZ(pthread_cond_broadcast(&(buf->empty))); // wake up a consumer thread (if any)
    }

    DIE_ON_NZ(pthread_mutex_unlock(&(buf->mutex))); // waive mutual exclusion access

    return 0;
}
