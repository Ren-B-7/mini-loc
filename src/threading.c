#include "include/threading.h"

#include <stdlib.h>
#include <string.h>

int wq_init(WorkQueue* q, size_t initial_cap)
{
    q->paths = (char**)malloc(sizeof(char*) * initial_cap);
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
        char** new_buf = (char**)malloc(sizeof(char*) * new_cap);
        if (!new_buf) {
            pthread_cond_wait(&q->not_full, &q->lock);
            continue;
        }
        for (size_t i = 0; i < q->count; i++) {
            new_buf[i] = q->paths[(q->head + i) % q->cap];
        }
        free((void*)q->paths);
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

int wq_pop_batch(WorkQueue* q, char** batch, int batch_size)
{
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->finished) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    if (q->count == 0) {
        pthread_cond_broadcast(&q->not_empty);
        pthread_mutex_unlock(&q->lock);
        return 0;
    }

    int n_popped = 0;
    while (n_popped < batch_size && q->count > 0) {
        batch[n_popped] = q->paths[q->head];
        q->head = (q->head + 1) % q->cap;
        q->count--;
        n_popped++;
    }

    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return n_popped;
}

void wq_finish(WorkQueue* q)
{
    pthread_mutex_lock(&q->lock);
    q->finished = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

COLD_ATTR void wq_destroy(WorkQueue* q)
{
    free((void*)q->paths);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->lock);
}

int dq_init(DirQueue* q, size_t initial_cap)
{
    q->paths = (char**)malloc(sizeof(char*) * initial_cap);
    q->depths = (int*)malloc(sizeof(int) * initial_cap);
    if (!q->paths || !q->depths) {
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

void dq_push(DirQueue* q, const char* path, int depth)
{
    pthread_mutex_lock(&q->lock);
    while (q->count == q->cap) {
        size_t new_cap = q->cap * 2;
        char** new_paths = (char**)malloc(sizeof(char*) * new_cap);
        int* new_depths = (int*)malloc(sizeof(int) * new_cap);
        for (size_t i = 0; i < q->count; i++) {
            new_paths[i] = q->paths[(q->head + i) % q->cap];
            new_depths[i] = q->depths[(q->head + i) % q->cap];
        }
        free((void*)q->paths);
        free((void*)q->depths);
        q->paths = new_paths;
        q->depths = new_depths;
        q->head = 0;
        q->tail = q->count;
        q->cap = new_cap;
    }
    q->paths[q->tail] = strdup(path);
    q->depths[q->tail] = depth;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

bool dq_pop(DirQueue* q, char* path_buf, int* depth)
{
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->finished) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    if (q->count == 0) {
        pthread_cond_broadcast(&q->not_empty);
        pthread_mutex_unlock(&q->lock);
        return false;
    }
    char* path = q->paths[q->head];
    strcpy(path_buf, path);
    free(path);
    *depth = q->depths[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return true;
}

void dq_finish(DirQueue* q)
{
    pthread_mutex_lock(&q->lock);
    q->finished = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

COLD_ATTR void dq_destroy(DirQueue* q)
{
    free((void*)q->paths);
    free((void*)q->depths);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->lock);
}
