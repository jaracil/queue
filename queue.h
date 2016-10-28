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
// Returns the new created queue. (it must be freed with queue_free function)
queue_t *queue_new(size_t max_size);

// Purge all pending elements and frees queue resources.
// If freefn isn't NULL freefn is called on every element purged.
void queue_free(queue_t *q, queue_freefn_t freefn);

// Returns number of elements into queue.
size_t queue_elements(queue_t *q);

// Closes queue.
// Next pushes after close returns QUEUE_ERR_CLOSED.
// Next pulls after close returns pending items until queue gets empty, after that pulls returns QUEUE_ERR_CLOSED.
void queue_close(queue_t *q);

// Returns true if queue is closed.
bool queue_is_closed(queue_t *q);

// Returns true if queue is empty.
bool queue_is_empty(queue_t *q);

// Returns true id queue is full.
bool queue_is_full(queue_t *q);

// Pushes one element into the queue.
// data is the pointer to be pushed.
// prio is the priority (use 0 as default).
// if timeout < 0 queue_push waits until there are free space on the queue.
// if timeout = 0 queue_push don't waits for free space.
// if timeout > 0 queue_push waits for free space until timeout milliseconds.
// Returns QUEUE_ERR_TIMEOUT if no space were available or QUEUE_ERR_CLOSED if queue was closed.
int queue_push(queue_t *q, void *data, int prio, int64_t timeout);

// Pulls one element from the queue.
// data is a pointer to the pointer that will store the pulled element.
// if timeout < 0 queue_pull waits until available elements.
// if timeout = 0 queue_pull don't waits for available elements.
// if timeout > 0 queue_pull waits for available elements until timeout milliseconds.
// Returns QUEUE_ERR_TIMEOUT if no elements were available or QUEUE_ERR_CLOSED if queue was closed and empty.
int queue_pull(queue_t *q, void **data, int64_t timeout);
#endif /* QUEUE_H_ */

