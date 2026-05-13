#include "include/threading.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

int wq_init(WorkQueue* q, size_t initial_cap)
{
	q->paths = (char**) malloc(sizeof(char*) * initial_cap);
	if (!q->paths) {
		return -1;
	}
	q->cap = initial_cap;
	q->head = 0;
	q->tail = 0;
	q->count = 0;
	q->finished = false;
	pthread_mutex_init(&q->lock, NULL);
	pthread_cond_init(&q->not_empty, NULL);
	pthread_cond_init(&q->not_full, NULL);
	return 0;
}

void wq_push(WorkQueue* q, char* path)
{
	pthread_mutex_lock(&q->lock);
	while (q->count == q->cap) {
		size_t new_cap = q->cap * 2;
		char** new_buf = (char**) malloc(sizeof(char*) * new_cap);
		if (!new_buf) {
			pthread_cond_wait(&q->not_full, &q->lock);
			continue;
		}
		for (size_t i = 0; i < q->count; i++) {
			new_buf[i] = q->paths[(q->head + i) % q->cap];
		}
		free((void*) q->paths);
		q->paths = new_buf;
		q->head = 0;
		q->tail = q->count;
		q->cap = new_cap;
	}
	q->paths[q->tail] = path;
	q->tail = (q->tail + 1) % q->cap;
	q->count++;
	pthread_cond_signal(&q->not_empty);
	pthread_mutex_unlock(&q->lock);
}

char* wq_pop(WorkQueue* q)
{
	pthread_mutex_lock(&q->lock);
	while (q->count == 0 && !q->finished) {
		pthread_cond_wait(&q->not_empty, &q->lock);
	}
	if (q->count == 0) {
		pthread_cond_broadcast(&q->not_empty);
		pthread_mutex_unlock(&q->lock);
		return NULL;
	}
	char* path = q->paths[q->head];
	q->head = (q->head + 1) % q->cap;
	q->count--;
	pthread_cond_signal(&q->not_full);
	pthread_mutex_unlock(&q->lock);
	return path;
}

void wq_finish(WorkQueue* q)
{
	pthread_mutex_lock(&q->lock);
	q->finished = true;
	pthread_cond_broadcast(&q->not_empty);
	pthread_mutex_unlock(&q->lock);
}

__attribute__((cold)) void wq_destroy(WorkQueue* q)
{
	free((void*) q->paths);
	pthread_cond_destroy(&q->not_full);
	pthread_cond_destroy(&q->not_empty);
	pthread_mutex_destroy(&q->lock);
}
