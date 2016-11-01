/*
 * queue.c
 *
 *  Created on: 18 de jul. de 2016
 *      Author: pepe
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <sys/eventfd.h>

#include "queue.h"
#include "utlist.h"

typedef struct queue_item_s {
	void *data;
	struct queue_item_s *prev;
	struct queue_item_s *next;
} queue_item_t;

typedef struct queue_s {
	queue_item_t *items;
	size_t max_size;
	size_t count;
	int refcount;
	int read_fd;
	int write_fd;
	bool closed;
	queue_freefn_t freefn;
	pthread_mutex_t mux;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
} queue_t;

static int deadline_ms(int64_t ms, struct timespec *tout){
	clock_gettime(CLOCK_MONOTONIC, tout);
	tout->tv_sec += (ms / 1000);
	tout->tv_nsec += ((ms % 1000) * 1000000);
	if (tout->tv_nsec > 1000000000){
		tout->tv_sec ++;
		tout->tv_nsec -= 1000000000;
	}
	return 0;
}

queue_t *queue_new(size_t max_size, queue_freefn_t freefn) {
	pthread_condattr_t cond_attr;
	queue_t *q = calloc(1, sizeof(queue_t));
	q->refcount = 1;
	q->read_fd = -1;
	q->max_size = max_size;
	pthread_mutex_init(&q->mux, NULL);
	pthread_condattr_init(&cond_attr);
	pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
	pthread_cond_init(&q->not_empty, &cond_attr);
	pthread_cond_init(&q->not_full, &cond_attr);
	pthread_condattr_destroy(&cond_attr);
	return q;
}

int queue_lock(queue_t *q) {
	return pthread_mutex_lock(&q->mux);
}

int queue_unlock(queue_t *q) {
	return pthread_mutex_unlock(&q->mux);
}

void queue_unref(queue_t *q) {
	pthread_mutex_lock(&q->mux);
	assert(q->refcount > 0);
	if ((--q->refcount) == 0){
		// queue_close_nl(q); // Must close ??
		queue_purge_nl(q);
		if (q->read_fd >= 0){
			close(q->read_fd);
			q->read_fd = -1;
		}
		if (q->write_fd >= 0){
			close(q->write_fd);
			q->write_fd = -1;
		}
		pthread_mutex_unlock(&q->mux);
		pthread_mutex_destroy(&q->mux);
		pthread_cond_destroy(&q->not_empty);
		pthread_cond_destroy(&q->not_full);
		free(q);
		return;
	}
	pthread_mutex_unlock(&q->mux);
}

void queue_ref(queue_t *q) {
	pthread_mutex_lock(&q->mux);
	assert(q->refcount > 0);
	q->refcount++;
	pthread_mutex_unlock(&q->mux);
}

int queue_readfd(queue_t *q){
	int r;
	pthread_mutex_lock(&q->mux);
	if (q->read_fd == -1) {
		int initval = q->count ? 1 : 0;
		q->read_fd = eventfd(initval, EFD_CLOEXEC | EFD_NONBLOCK);
	}
	r = q->read_fd;
	pthread_mutex_unlock(&q->mux);
	return r;
}

int queue_witefd(queue_t *q){
	int r;
	pthread_mutex_lock(&q->mux);
	if (q->write_fd == -1) {
		int initval = (!q->max_size) || (q->count < q->max_size) ? 1 : 0;
		q->write_fd = eventfd(initval, EFD_CLOEXEC | EFD_NONBLOCK);
	}
	r = q->write_fd;
	pthread_mutex_unlock(&q->mux);
	return r;
}


void queue_purge_nl(queue_t *q) {
	void *data;
	while(queue_pull_nl(q, &data, 0 ) == QUEUE_ERR_OK) {
		if (q->freefn != NULL && data != NULL) {
			q->freefn(data);
		}
	}
}

void queue_purge(queue_t *q) {
	pthread_mutex_lock(&q->mux);
	queue_purge_nl(q);
	pthread_mutex_unlock(&q->mux);
	return;
}

size_t queue_elements_nl(queue_t *q){
	return q->count;
}

size_t queue_elements(queue_t *q) {
	size_t r;
	pthread_mutex_lock(&q->mux);
	r = queue_elements_nl(q);
	pthread_mutex_unlock(&q->mux);
	return r;
}

void queue_close_nl(queue_t *q){
	q->closed = true;
	pthread_cond_broadcast(&q->not_empty); // Wake up all threads
	pthread_cond_broadcast(&q->not_full);  //      on close
}

void queue_close(queue_t *q){
	pthread_mutex_lock(&q->mux);
	queue_close_nl(q);
	pthread_mutex_unlock(&q->mux);
	return;
}

bool queue_is_closed_nl(queue_t *q){
	return q->closed;
}

bool queue_is_closed(queue_t *q) {
	 bool r;
	 pthread_mutex_lock(&q->mux);
	 r = queue_is_closed_nl(q);
	 pthread_mutex_unlock(&q->mux);
	 return r;
}

bool queue_is_empty_nl(queue_t *q){
	return q->count == 0;
}

bool queue_is_empty(queue_t *q) {
	 bool r;
	 pthread_mutex_lock(&q->mux);
	 r = queue_is_empty_nl(q);
	 pthread_mutex_unlock(&q->mux);
	 return r;
}

bool queue_is_full_nl(queue_t *q){
	 bool r = false;
 	 if (q->max_size > 0){
 		 r = q->count >= q->max_size;
 	 }
 	 return r;
}

bool queue_is_full(queue_t *q) {
 	 bool r;
 	 pthread_mutex_lock(&q->mux);
 	 r = queue_is_full_nl(q);
 	 pthread_mutex_unlock(&q->mux);
 	 return r;
 }

int queue_push_nl(queue_t *q, void *data, int prio, int64_t timeout) {
	struct timespec tout;
	bool init_tout = false;
	queue_item_t *qi;

	if (queue_is_closed_nl(q)) return QUEUE_ERR_CLOSED;
	while (queue_is_full_nl(q)){
		if (timeout == 0){
			return QUEUE_ERR_TIMEOUT;
		} else if (timeout < 0){
			pthread_cond_wait(&q->not_full, &q->mux);
		} else {
			if (!init_tout){
				init_tout = true;
				deadline_ms(timeout, &tout);
			}
			if (pthread_cond_timedwait(&q->not_full, &q->mux, &tout) != 0) return QUEUE_ERR_TIMEOUT;
			if (queue_is_closed_nl(q)) return QUEUE_ERR_CLOSED;
		}
	}
	if ( (qi = (queue_item_t*)malloc(sizeof(queue_item_t))) == NULL) exit(-1);
	qi->data = data;
	if (prio > 0){
		DL_PREPEND(q->items, qi);
	} else {
		DL_APPEND(q->items, qi);
	}
	q->count ++;
	pthread_cond_signal(&q->not_empty);
	if (q->read_fd >= 0 && q->count == 1) {
		eventfd_write(q->read_fd, 1);
	}
	if (q->write_fd >= 0 && q->max_size && q->count == q->max_size) {
		eventfd_t val;
		eventfd_read(q->write_fd, &val);
		assert(val == 1);
	}
	return QUEUE_ERR_OK;
}

int queue_push(queue_t *q, void *data, int prio, int64_t timeout){
	int r;
	pthread_mutex_lock(&q->mux);
	r = queue_push_nl(q, data, prio, timeout);
	pthread_mutex_unlock(&q->mux);
	return r;
}

int queue_pull_nl(queue_t *q, void **data, int64_t timeout) {
	struct timespec tout;
	bool init_tout = false;
	queue_item_t *qi;

	while (queue_is_empty_nl(q)){
		if (queue_is_closed_nl(q)) return QUEUE_ERR_CLOSED;
		if (timeout == 0){
			return QUEUE_ERR_TIMEOUT;
		} else if (timeout < 0){
			pthread_cond_wait(&q->not_empty, &q->mux);
		} else {
			if (!init_tout){
				init_tout = true;
				deadline_ms(timeout, &tout);
			}
			if (pthread_cond_timedwait(&q->not_empty, &q->mux, &tout) != 0) return QUEUE_ERR_TIMEOUT;
		}
	}
	qi = q->items;
	*data = qi->data;
	DL_DELETE(q->items, qi);
	free(qi);
	q->count --;
	pthread_cond_signal(&q->not_full);
	if (q->read_fd >= 0 && q->count == 0) {
		eventfd_t val;
		eventfd_read(q->read_fd, &val);
		assert(val == 1);
	}
	if (q->write_fd >= 0 && q->max_size && q->count == q->max_size - 1) {
		eventfd_write(q->write_fd, 1);
	}
	return QUEUE_ERR_OK;
}

int queue_pull(queue_t *q, void **data, int64_t timeout){
	int r;
	pthread_mutex_lock(&q->mux);
	r = queue_pull_nl(q, data, timeout);
	pthread_mutex_unlock(&q->mux);
	return r;
}
