#include <pthread.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

#include "thread_pool.h"

struct tp_task
{
    tp_fn fn;
    void *arg;
    struct tp_task *next;
};

static void tp_init_cleanup_partial(struct tp_pool *pool, size_t created)
{
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutting_down = true;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);

    for (size_t i = 0; i < created; i++)
        pthread_join(pool->threads[i], NULL);

    pthread_cond_destroy(&pool->queue_cond);
    pthread_mutex_destroy(&pool->queue_mutex);

    free(pool->threads);
    pool->threads = NULL;
}

static void *tp_worker(void *arg)
{
    struct tp_pool *pool = arg;

    for (;;)
    {
        pthread_mutex_lock(&pool->queue_mutex);

        while (pool->head == NULL && !pool->shutting_down)
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);

        if (pool->shutting_down && pool->head == NULL)
        {
            pthread_mutex_unlock(&pool->queue_mutex);
            return NULL;
        }

        struct tp_task *task = pool->head;
        pool->head = pool->head->next;
        if (pool->head == NULL)
            pool->tail = NULL;

        pthread_mutex_unlock(&pool->queue_mutex);

        task->fn(task->arg);
        free(task);
    }

    return NULL;
}

tp_error_t tp_pool_init(struct tp_pool *pool, size_t nthreads)
{
    if (pool == NULL || nthreads == 0)
        return TP_ERR_INVALID;

    pool->threads = malloc(nthreads * sizeof(*pool->threads));
    if (pool->threads == NULL)
        return TP_ERR_NOMEM;

    pool->nthreads = nthreads;
    pool->head = NULL;
    pool->tail = NULL;
    pool->shutting_down = false;

    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0)
    {
        free(pool->threads);
        return TP_ERR_SYNC;
    }

    if (pthread_cond_init(&pool->queue_cond, NULL) != 0)
    {
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool->threads);
        return TP_ERR_SYNC;
    }

    for (size_t i = 0; i < nthreads; i++)
    {
        if (pthread_create(&pool->threads[i], NULL, tp_worker, pool) != 0)
        {
            tp_init_cleanup_partial(pool, i);
            return TP_ERR_THREAD;
        }
    }

    return TP_OK;
}

tp_error_t tp_pool_submit(struct tp_pool *pool, tp_fn fn, void *arg)
{
    if (pool ==  NULL || fn == NULL)
        return TP_ERR_INVALID;

    struct tp_task *task = malloc(sizeof(*task));
    if (task == NULL)
        return TP_ERR_NOMEM;

    task->fn = fn;
    task->arg = arg;
    task->next = NULL;

    pthread_mutex_lock(&pool->queue_mutex);
    if (pool->shutting_down)
    {
        pthread_mutex_unlock(&pool->queue_mutex);
        free(task);
        return TP_ERR_SHUTDOWN;
    }

    if (pool->tail == NULL)
        pool->head = task;
    else
        pool->tail->next = task;
    pool->tail = task;

    pthread_cond_signal(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);

    return TP_OK;
}

void tp_pool_destroy(struct tp_pool *pool)
{
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutting_down = true;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);

    for (size_t i = 0; i < pool->nthreads; i++)
        pthread_join(pool->threads[i], NULL);

    free(pool->threads);

    struct tp_task *task = pool->head;
    while (task != NULL)
    {
        struct tp_task *next = task->next;
        free(task);
        task = next;
    }

    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->queue_cond);
}
