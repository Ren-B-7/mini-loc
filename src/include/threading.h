#ifndef LOC_THREADING_H
#define LOC_THREADING_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "types.h"

#define QUEUE_INIT_CAP 4096
#define MAX_THREADS 64
#define LOCAL_INIT_CAP 1024

typedef struct {
	char** paths;
	size_t cap;
	size_t head;
	size_t tail;
	size_t count;
	bool finished;
	pthread_mutex_t lock;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
} WorkQueue;

typedef struct {
	WorkQueue* queue;
	FileResult* files;
	int n_files;
	int capacity;
} ThreadState;

int wq_init(WorkQueue* q, size_t initial_cap);
void wq_push(WorkQueue* q, char* path);
char* wq_pop(WorkQueue* q);
void wq_finish(WorkQueue* q);
void wq_destroy(WorkQueue* q);

#endif
