#ifndef BOUNDED_BUFFER_H
#define BOUNDED_BUFFER_H

#include <stdlib.h>

typedef struct _boundedBuffer BoundedBuffer;

BoundedBuffer* allocBoundedBuffer(size_t capacity, size_t dataSize);
int destroyBoundedBuffer(BoundedBuffer* buf);

int dequeue(BoundedBuffer* buf, void* dest, size_t destSize);
/*!
Allocates a new node with the given value and pushes it to the tail
of the bounded buffer.

\param buf is the buffer the data is going to be pushed to
\param data is a pointer to the data to be pushed

If the buffer is full, waits until there is at least one free spot.

@return 0 on success, -1 if it is unable to allocate memory for the new node.
*/

int enqueue(BoundedBuffer* buf, void* data);

#endif
