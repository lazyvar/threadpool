/*
 * Fork/Join Framework 
 *
 * Test 2.
 *
 * Tests running multiple tasks.
 *
 * Written by G. Back for CS3214 Fall 2014.
 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>

#include "threadpool.h"
#include "threadpool_lib.h"
#define DEFAULT_THREADS 1

/* Data to be passed to callable. */
struct arg2 {
    uintptr_t a;
    uintptr_t b;
};

/* 
 * A FJ task that multiplies 2 numbers. 
 */
static void *
multiplier_task(struct thread_pool *pool, struct arg2 * data)
{
    return (void *)(data->a * data->b);
}

static int
run_test(int nthreads)
{
    struct benchmark_data * bdata = start_benchmark();
    struct thread_pool * threadpool = thread_pool_new(nthreads);
   
#define NTASKS 200
    struct future *f[NTASKS];
    struct arg2 *args[NTASKS];
    int i;
    for (i = 0; i < NTASKS; i++) {
        args[i] = malloc(sizeof (struct arg2));
        args[i]->a = i;
        args[i]->b = i+1;
        f[i] = thread_pool_submit(threadpool, (fork_join_task_t) multiplier_task, args[i]);
    }

    bool success = true;
    for (i = 0; i < NTASKS; i++) {
        uintptr_t sprod = (uintptr_t) future_get(f[i]);
        future_free(f[i]);
        free(args[i]);
        if (sprod != i * (i + 1))
            success = false;
    }
    thread_pool_shutdown_and_destroy(threadpool);

    stop_benchmark(bdata);

    // consistency check
    if (!success) {
        fprintf(stderr, "Wrong result\n");
        abort();
    }

    report_benchmark_results(bdata);
    printf("Test successful.\n");
    free(bdata);
    return 0;
}

/**********************************************************************************/

static void
usage(char *av0, int exvalue)
{
    fprintf(stderr, "Usage: %s [-n <n>]\n"
                    " -n number of threads in pool, default %d\n"
                    , av0, DEFAULT_THREADS);
    exit(exvalue);
}

int 
main(int ac, char *av[]) 
{
    int c, nthreads = DEFAULT_THREADS;
    while ((c = getopt(ac, av, "n:")) != EOF) {
        switch (c) {
        case 'n':
            nthreads = atoi(optarg);
            break;
        case 'h':
            usage(av[0], EXIT_SUCCESS);
        }
    }

    return run_test(nthreads);
}
