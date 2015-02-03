#include "threadpool.h"
#include "list.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>

typedef enum {
    NOT_STARTED = 0,
    IN_PROGRESS = 1,
    COMPLETED = 2
} status_t;

struct worker {
    pthread_t tid;
    struct list worker_queue;
    struct list_elem elem;
};

struct thread_pool {
    struct list worker_threads;
    struct list global_queue;
    pthread_mutex_t lock;
    pthread_cond_t work_flag;
    pthread_barrier_t start_sync;
    bool shutdown;
    int nthreads;
};

struct future {
    void * data;
    void * result;
    struct list_elem elem;
    struct thread_pool * pool;
    pthread_cond_t done;
    fork_join_task_t task;
    status_t status;
};

//#define DEBUG

static __thread struct worker * w;
static __thread bool is_worker;

static struct list_elem * steal_task(struct thread_pool * p);
static bool sleeping(struct thread_pool *);
static void * working_thread(void *);

void thread_pool_shutdown_and_destroy(struct thread_pool * t) {
    pthread_mutex_lock(&t->lock);
    t->shutdown = true;
    pthread_cond_broadcast(&t->work_flag);
    pthread_mutex_unlock(&t->lock);

    #ifdef DEBUG
        printf("Shutdown signaled, joining threads.\n");
    #endif

    struct list_elem * we;
    struct worker * w;
    for (we = list_begin(&t->worker_threads); we != list_end(&t->worker_threads); we = list_next(we)) {
        w = list_entry(we, struct worker, elem);
        if ((pthread_join(w->tid, NULL)) != 0) {
            printf("Error joing threads.\n");
        }
    }

    while (!list_empty(&t->worker_threads)) {
        we = list_pop_front(&t->worker_threads);
        w = list_entry(we, struct worker, elem);
        free(w);
    }

    pthread_mutex_destroy(&t->lock);
    pthread_cond_destroy(&t->work_flag);
    pthread_barrier_destroy(&t->start_sync);
    free(t);
}

struct thread_pool * thread_pool_new(int nthreads) {
    
    struct thread_pool * pool;
    if ((pool = malloc(sizeof(struct thread_pool))) == NULL) {
        printf("Error malloc'ing thread pool.\n");
        return NULL;
    }

    if ((pthread_mutex_init(&pool->lock, NULL)) != 0) {
        printf("Error initializing lock.\n");
        return NULL;
    }
    
    if ((pthread_cond_init(&pool->work_flag, NULL)) != 0) {
        printf("Error initializing work_flag.\n");
        return NULL;
    }

    if ((pthread_barrier_init(&pool->start_sync, NULL, nthreads + 1)) != 0) {
        printf("Error initializing start_sync.\n");
        return NULL;
    }

    pthread_mutex_lock(&pool->lock);

    list_init(&pool->worker_threads);
    list_init(&pool->global_queue);
    pool->shutdown = false;   
    pool->nthreads = nthreads;

    int i;
    for (i = 0;i < nthreads; i++) {
        struct worker * wt;
        if ((wt = malloc(sizeof(struct worker))) == NULL) {
            printf("Error malloc'ing worker thread.\n");
            return NULL;
        }

        list_push_front(&pool->worker_threads, &wt->elem);
        list_init(&wt->worker_queue);
        
        if ((pthread_create(&wt->tid, NULL, working_thread, pool)) != 0) {
            printf("Error creating worker thread.\n");
            return NULL;
        }
        #ifdef DEBUG
            printf("Created worker thread %d with tid %d.\n", i, (int) wt->tid);
        #endif
    }

    if ((w = malloc(sizeof(struct worker))) == NULL) {
        printf("Error malloc'ing worker thread.\n");
        return NULL;
    }

    w->tid = pthread_self();
    list_init(&w->worker_queue);
    is_worker = false;

    pthread_mutex_unlock(&pool->lock);
    
    pthread_barrier_wait(&pool->start_sync);

    return pool;
}

struct future * thread_pool_submit( struct thread_pool *pool,  fork_join_task_t task, void * data) {
    
    pthread_mutex_lock(&pool->lock);
        
    struct future * f;
   
    if ((f = malloc(sizeof(struct future))) == NULL) {
        printf("Error mallc'ing future.\n");
        return NULL;
    }

    if ((pthread_cond_init(&f->done, NULL)) != 0) {
        printf("Error initializing future->done.\n");
        return NULL;
    }

    f->task = task;
    f->data = data;
    f->status = NOT_STARTED;
    f->pool = pool;

    if (is_worker) {
        #ifdef DEBUG
            printf("Received internal thread_pool_submit, pushing onto worker's stack\n");
        #endif
        list_push_front(&w->worker_queue, &f->elem);
    } else {
        #ifdef DEBUG
            printf("Received external thread_pool_submit, pushing onto global queue\n");
        #endif
        list_push_back(&pool->global_queue, &f->elem);
    }

    #ifdef DEBUG
        printf("Sending signal to sleeping workers...\n");
    #endif

    pthread_cond_signal(&pool->work_flag);
    pthread_mutex_unlock(&pool->lock);

    return f;
}

static void * working_thread(void * param) {
    struct thread_pool * pool = (struct thread_pool *) param;
   
    pthread_barrier_wait(&pool->start_sync);

    bool first = true;
    while (1) {
    
        pthread_mutex_lock(&pool->lock);
        
        if (first) {
            pthread_t tid = pthread_self();
            struct list_elem * we;
            struct worker * wt = NULL;
            for (we = list_begin(&pool->worker_threads); we != list_end(&pool->worker_threads); we = list_next(we)) {
                wt = list_entry(we, struct worker, elem);

                if (tid == wt->tid) {
                    w = wt;
                    break;
                }
            }
            is_worker = true;
            first = false;
        }

        while(sleeping(pool)) {
            #ifdef DEBUG
                printf("No work, now sleeping.\n");
            #endif
            pthread_cond_wait(&pool->work_flag, &pool->lock);
        }
        
        #ifdef DEBUG
            printf("Awoken worker thread: %d.\n", (int) w->tid);
        #endif
        
        if (pool->shutdown) {
            break;
        }
      
        #ifdef DEBUG
            printf("Doing work.\n");
        #endif
        
        struct list_elem * e;
        if (list_empty(&w->worker_queue)) {
            if (list_empty(&pool->global_queue)) {
                e = steal_task(pool);
            } else {   
                e = list_pop_front(&pool->global_queue);
            }
        } else {
            e = list_pop_front(&w->worker_queue);
        }
    
        if (e == NULL) {
            printf("Error finding task.\n");
            pool->shutdown = true;
            break;
        }
      
        struct future * f = list_entry(e, struct future, elem);
        
        f->status = IN_PROGRESS;

        pthread_mutex_unlock(&pool->lock);

        f->result = (f->task)(pool, f->data);
        
        pthread_mutex_lock(&pool->lock);
        
        f->status = COMPLETED;
        pthread_cond_signal(&f->done);       

        pthread_mutex_unlock(&pool->lock);   
    }
    pthread_mutex_unlock(&pool->lock);
    
    #ifdef DEBUG
        printf("Exiting thread %d\n", (int) w->tid);
    #endif
    
    pthread_exit(NULL);
    return NULL;
}

void * future_get(struct future * f) { 
   
    #ifdef DEBUG
        printf("future_get called.\n");
    #endif

    pthread_mutex_lock(&f->pool->lock);
    
    if (f->status == NOT_STARTED) {
        #ifdef DEBUG
            printf("Task not yet started, starting now.\n");
        #endif
    
        list_remove(&f->elem);
        f->status = IN_PROGRESS;

        pthread_mutex_unlock(&f->pool->lock);
    
        f->result = (f->task)(f->pool, f->data);

        pthread_mutex_lock(&f->pool->lock);

        f->status = COMPLETED; 
    } else {
        while (f->status != COMPLETED) {       
            #ifdef DEBUG
                printf("Task already started, waiting for completion.\n");
            #endif
            pthread_cond_wait(&f->done, &f->pool->lock);
        }
    }

    #ifdef DEBUG
        printf("Task completed, return result.\n");
    #endif
    
    void * ret = f->result;
    
    pthread_mutex_unlock(&f->pool->lock);

    return ret;
}

void future_free(struct future * f) {
    pthread_cond_destroy(&f->done);
    free(f);
}

static bool sleeping(struct thread_pool * p) {
    struct list_elem * wte;
    for (wte = list_begin(&p->worker_threads); wte != list_end(&p->worker_threads); wte = list_next(wte)) {
        struct worker *  worka = list_entry(wte, struct worker, elem);

        if (!list_empty(&worka->worker_queue)) {
            return false;
        }    
    }

    return list_empty(&p->global_queue) && list_empty(&w->worker_queue) && !p->shutdown;
}

static struct list_elem * steal_task(struct thread_pool * p) {
    
    struct list_elem * wte;
    for (wte = list_begin(&p->worker_threads); wte != list_end(&p->worker_threads); wte = list_next(wte)) {
        struct worker *  worka = list_entry(wte, struct worker, elem);

        if (!list_empty(&worka->worker_queue)) {
           return list_pop_back(&worka->worker_queue);
        }    
    }
    return NULL;
}
