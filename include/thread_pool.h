#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stdbool.h>
#include <pthread.h>

typedef enum {
    TP_OK = 0,

    TP_ERR_INVALID,
    TP_ERR_NOMEM,
    TP_ERR_THREAD,
    TP_ERR_SYNC,
    TP_ERR_SHUTDOWN
} tp_error_t;

struct tp_task;

typedef struct tp_pool
{
    pthread_t *threads;
    size_t nthreads;

    struct tp_task *head;
    struct tp_task *tail;

    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    bool shutting_down;
} tp_pool_t;

typedef void (*tp_fn)(void *arg);

tp_error_t tp_pool_init(tp_pool_t *pool, size_t nthreads);
tp_error_t tp_pool_submit(tp_pool_t *pool, tp_fn fn, void *arg);
void tp_pool_destroy(tp_pool_t *pool);

#endif