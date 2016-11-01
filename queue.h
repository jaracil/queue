/*
 * queue.h
 *
 *  Created on: 18 de jul. de 2016
 *      Author: pepe
 */

#ifndef QUEUE_H_
#define QUEUE_H_

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct queue_s queue_t;

typedef void (*queue_freefn_t)(void *);

enum {
	QUEUE_ERR_OK = 0,
	QUEUE_ERR_TIMEOUT = 1,
	QUEUE_ERR_CLOSED = 2
};

// Creates new queue, if max_size is 0 size is unlimited.
// If freefn isn't NULL freefn is called on every element purged (queue_purge/unref function).
// Returns the new created queue. (it must be freed with queue_free function)
queue_t *queue_new(size_t max_size, queue_freefn_t freefn);

// Acquires queue lock.
// You must acquire lock before call queue_*_nl functions
int queue_lock(queue_t *q);

// Releases queue lock.
// You must release lock after call queue_*_nl functions
int queue_unlock(queue_t *q);

// Decrements queue ref counter.
// When ref counter reach 0 all pending elements are purged and all queue resources are released.
void queue_unref(queue_t *q);

// Increments queue ref counter.
// Call this function before pass this queue to another thread.
void queue_ref(queue_t *q);

// Returns file descriptor suitable for polling (select/poll/epoll)
// File descriptor is set to readable state when queue is not empty.
int queue_readfd(queue_t *q);

// Returns file descriptor suitable for polling (select/poll/epoll)
// File descriptor is set to readable state when queue is not full.
// On unlimited queues (max_size == 0) file descriptor is always in readable state.
int queue_writefd(queue_t *q);

// Purge all pending elements.
// If freefn isn't NULL freefn is called on every element purged.
void queue_purge(queue_t *q);
void queue_purge_nl(queue_t *q); // Non locking version

// Returns number of elements into queue.
size_t queue_elements(queue_t *q);
size_t queue_elements_nl(queue_t *q); // Non locking version

// Closes queue.
// Next pushes after close returns QUEUE_ERR_CLOSED.
// Next pulls after close returns pending items until queue gets empty, after that pulls returns QUEUE_ERR_CLOSED.
void queue_close(queue_t *q);
void queue_close_nl(queue_t *q); // Non locking version

// Returns true if queue is closed.
bool queue_is_closed(queue_t *q);
bool queue_is_closed_nl(queue_t *q); // Non locking version

// Returns true if queue is empty.
bool queue_is_empty(queue_t *q);
bool queue_is_empty_nl(queue_t *q); // Non locking version

// Returns true id queue is full.
bool queue_is_full(queue_t *q);
bool queue_is_full_nl(queue_t *q); // Non locking version

// Pushes one element into the queue.
// data is the pointer to be pushed.
// prio is the priority (use 0 as default).
// if timeout < 0 queue_push waits until there are free space on the queue.
// if timeout = 0 queue_push don't waits for free space.
// if timeout > 0 queue_push waits for free space until timeout milliseconds.
// Returns QUEUE_ERR_TIMEOUT if no space were available or QUEUE_ERR_CLOSED if queue was closed.
int queue_push(queue_t *q, void *data, int prio, int64_t timeout);
int queue_push_nl(queue_t *q, void *data, int prio, int64_t timeout); // Non locking version

// Pulls one element from the queue.
// data is a pointer to the pointer that will store the pulled element.
// if timeout < 0 queue_pull waits until available elements.
// if timeout = 0 queue_pull don't waits for available elements.
// if timeout > 0 queue_pull waits for available elements until timeout milliseconds.
// Returns QUEUE_ERR_TIMEOUT if no elements were available or QUEUE_ERR_CLOSED if queue was closed and empty.
int queue_pull(queue_t *q, void **data, int64_t timeout);
int queue_pull_nl(queue_t *q, void **data, int64_t timeout); // Non locking version

#endif /* QUEUE_H_ */

