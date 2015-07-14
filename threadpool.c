/* import statements */

#include "threadpool.h"
#include "list.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>

/* status of job */
typedef enum {
    NOT_STARTED = 0,
    IN_PROGRESS = 1,
    COMPLETED = 2
} status_t;

/* worker info */
struct worker {
    pthread_t tid;
    struct list worker_queue;
    struct list_elem elem;
};

/* pool info */
struct thread_pool {
    struct list worker_threads;
    struct list global_queue;
    pthread_mutex_t lock;
    pthread_cond_t work_flag;
    pthread_barrier_t start_sync;
    bool shutdown;
    int nthreads;
};

/* future info */
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

/* save worker info local so the thread knows itself
 * can still access other worker threads info through 
 * pool queue */
static __thread struct worker * w;
static __thread bool is_worker;

static struct list_elem * steal_task(struct thread_pool * p);
static bool sleeping(struct thread_pool *);
static void * working_thread(void *);

/* raise shutdown flag and free variable */
void thread_pool_shutdown_and_destroy(struct thread_pool * t) {
    pthread_mutex_lock(&t->lock);
    t->shutdown = true;
    
    /* wake all threads so they can shutdown */
    pthread_cond_broadcast(&t->work_flag);
    pthread_mutex_unlock(&t->lock);

    #ifdef DEBUG
        printf("Shutdown signaled, joining threads.\n");
    #endif


    /* threads join here */
    struct list_elem * we;
    struct worker * w;
    for (we = list_begin(&t->worker_threads); we != list_end(&t->worker_threads); we = list_next(we)) {
        w = list_entry(we, struct worker, elem);
        if ((pthread_join(w->tid, NULL)) != 0) {
            printf("Error joing threads.\n");
        }
    }

    /* free worker structs */
    while (!list_empty(&t->worker_threads)) {
        we = list_pop_front(&t->worker_threads);
        w = list_entry(we, struct worker, elem);
        free(w);
    }

    /* free condition vars and self */
    pthread_mutex_destroy(&t->lock);
    pthread_cond_destroy(&t->work_flag);
    pthread_barrier_destroy(&t->start_sync);
    free(t);
}

/* thread pool creation */
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

    /* initialize and create worker threads */
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

/* submit a job to be completed. could be externally or internally requested */
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

    /* check for internal / external submission */
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

/* worker thread function */
static void * working_thread(void * param) {
    struct thread_pool * pool = (struct thread_pool *) param;
  
    /* wait for all worker threads to be created before workers start working */
    pthread_barrier_wait(&pool->start_sync);

    bool first = true;
    /* run loop */
    while (1) {
    
        pthread_mutex_lock(&pool->lock);
       
        /* if in creation, set local worker info */
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

        /* surrounded in loop to prevent spurious wake ups */
        while(sleeping(pool)) {
            #ifdef DEBUG
                printf("No work, now sleeping.\n");
            #endif
            pthread_cond_wait(&pool->work_flag, &pool->lock);
        }
        
        #ifdef DEBUG
            printf("Awoken worker thread: %d.\n", (int) w->tid);
        #endif
       
        /* if shutdown break out of run loop */
        if (pool->shutdown) {
            break;
        }
      
        #ifdef DEBUG
            printf("Doing work.\n");
        #endif
       
        /* first check worker's own queue, then check global queue,
         * and finally steal from other workers if the first two
         * are empty. */
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
   
        /* this should never return error as the mutex is still held,
         * and therefore no changes should be made in any queue */
        if (e == NULL) {
            printf("Error finding task.\n");
            pool->shutdown = true;
            break;
        }
     
        /* get future and run it */
        struct future * f = list_entry(e, struct future, elem);
        
        f->status = IN_PROGRESS;

        /* cant forget to release the lock! */
        pthread_mutex_unlock(&pool->lock);

        f->result = (f->task)(pool, f->data);
       
        /* task is done, reacquire lock and notify any thread waiting
         * on future */
        pthread_mutex_lock(&pool->lock);
        
        f->status = COMPLETED;
        pthread_cond_signal(&f->done);       

        pthread_mutex_unlock(&pool->lock);   
    }

    /* pool is shutting down */
    pthread_mutex_unlock(&pool->lock);
    
    #ifdef DEBUG
        printf("Exiting thread %d\n", (int) w->tid);
    #endif
    
    pthread_exit(NULL);
    return NULL;
}

/* returns a future once it has finished executing */
void * future_get(struct future * f) { 
   
    #ifdef DEBUG
        printf("future_get called.\n");
    #endif

    pthread_mutex_lock(&f->pool->lock);
   
    /* if not started, thread helps in execution */
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

/* goes through and checks all the queues, if all are empty then the thread should sleep */
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

/* goes through all worker threads and finds the first job available to steal */
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
