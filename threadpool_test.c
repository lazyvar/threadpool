/*
 * Fork/Join Framework 
 *
 * Test 1.
 *
 * Tests simple task execution.
 *
 * Written by G. Back for CS3214 Fall 2014.
 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
 * A FJ task that adds 2 numbers. 
 */
static void *
adder_task(struct thread_pool *pool, struct arg2 * data)
{
    return (void *)(data->a + data->b);
}

static int
run_test(int nthreads)
{
    struct benchmark_data * bdata = start_benchmark();
    struct thread_pool * threadpool = thread_pool_new(nthreads);
   
    struct arg2 args = {
        .a = 20,
        .b = 22,
    };

    struct future * sum = thread_pool_submit(threadpool, (fork_join_task_t) adder_task, &args);

    uintptr_t ssum = (uintptr_t) future_get(sum);
    future_free(sum);
    thread_pool_shutdown_and_destroy(threadpool);

    stop_benchmark(bdata);

    // consistency check
    if (ssum != 42) {
        fprintf(stderr, "Wrong result, expected 42, got %ld\n", ssum);
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
