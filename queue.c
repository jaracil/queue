/*
 * queue.c
 *
 *  Created on: 18 de jul. de 2016
 *      Author: pepe
 */

#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

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
	bool closed;
	pthread_mutex_t mux;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
} queue_t;

// static function declarations
static void _queue_close(queue_t *q);
static void _queue_purge(queue_t *q, queue_freefn_t freefn);
static int _queue_pull(queue_t *q, void **data, int64_t timeout);
// end of static function declarations

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

queue_t *queue_new(size_t max_size) {
	pthread_condattr_t cond_attr;
	queue_t *q = calloc(1, sizeof(queue_t));
	q->max_size = max_size;
	pthread_mutex_init(&q->mux, NULL);
	pthread_condattr_init(&cond_attr);
	pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
	pthread_cond_init(&q->not_empty, &cond_attr);
	pthread_cond_init(&q->not_full, &cond_attr);
	pthread_condattr_destroy(&cond_attr);
	return q;
}

void queue_free(queue_t *q, queue_freefn_t freefn) {
	pthread_mutex_lock(&q->mux);
	_queue_close(q);
	_queue_purge(q, freefn);
	pthread_mutex_unlock(&q->mux);
	pthread_mutex_destroy(&q->mux);
	pthread_cond_destroy(&q->not_empty);
	pthread_cond_destroy(&q->not_full);
	free(q);
}

static void _queue_purge(queue_t *q, queue_freefn_t freefn) {
	void *data;
	while(_queue_pull(q, &data, 0 ) == QUEUE_ERR_OK) {
		if (freefn != NULL && data != NULL) {
			freefn(data);
		}
	}
}

void queue_purge(queue_t *q, queue_freefn_t freefn) {
	pthread_mutex_lock(&q->mux);
	_queue_purge(q, freefn);
	pthread_mutex_unlock(&q->mux);
	return;
}

static size_t _queue_elements(queue_t *q){
	return q->count;
}

size_t queue_elements(queue_t *q) {
	size_t r;
	pthread_mutex_lock(&q->mux);
	r = _queue_elements(q);
	pthread_mutex_unlock(&q->mux);
	return r;
}

static void _queue_close(queue_t *q){
	q->closed = true;
	pthread_cond_broadcast(&q->not_empty); // Wake up all threads
	pthread_cond_broadcast(&q->not_full);  //      on close
}

void queue_close(queue_t *q){
	pthread_mutex_lock(&q->mux);
	_queue_close(q);
	pthread_mutex_unlock(&q->mux);
	return;
}

static bool _queue_is_closed(queue_t *q){
	return q->closed;
}

bool queue_is_closed(queue_t *q) {
	 bool r;
	 pthread_mutex_lock(&q->mux);
	 r = _queue_is_closed(q);
	 pthread_mutex_unlock(&q->mux);
	 return r;
}

static bool _queue_is_empty(queue_t *q){
	return q->count == 0;
}

bool queue_is_empty(queue_t *q) {
	 bool r;
	 pthread_mutex_lock(&q->mux);
	 r = _queue_is_empty(q);
	 pthread_mutex_unlock(&q->mux);
	 return r;
}

static bool _queue_is_full(queue_t *q){
	 bool r = false;
 	 if (q->max_size > 0){
 		 r = q->count >= q->max_size;
 	 }
 	 return r;
}

bool queue_is_full(queue_t *q) {
 	 bool r;
 	 pthread_mutex_lock(&q->mux);
 	 r = _queue_is_full(q);
 	 pthread_mutex_unlock(&q->mux);
 	 return r;
 }

static int _queue_push(queue_t *q, void *data, int prio, int64_t timeout) {
	struct timespec tout;
	bool init_tout = false;
	queue_item_t *qi;

	if (_queue_is_closed(q)) return QUEUE_ERR_CLOSED;
	while (_queue_is_full(q)){
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
			if (_queue_is_closed(q)) return QUEUE_ERR_CLOSED;
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
	return QUEUE_ERR_OK;
}

int queue_push(queue_t *q, void *data, int prio, int64_t timeout){
	int r;
	pthread_mutex_lock(&q->mux);
	r = _queue_push(q, data, prio, timeout);
	pthread_mutex_unlock(&q->mux);
	return r;
}

static int _queue_pull(queue_t *q, void **data, int64_t timeout) {
	struct timespec tout;
	bool init_tout = false;
	queue_item_t *qi;

	while (_queue_is_empty(q)){
		if (_queue_is_closed(q)) return QUEUE_ERR_CLOSED;
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
	return 0;
}

int queue_pull(queue_t *q, void **data, int64_t timeout){
	int r;
	pthread_mutex_lock(&q->mux);
	r = _queue_pull(q, data, timeout);
	pthread_mutex_unlock(&q->mux);
	return r;
}
