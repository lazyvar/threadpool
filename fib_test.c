/*
 * Parallel fibonacci.
 *
 * This is just a toy program to see how well the
 * underlying FJ framework supports extremely fine-grained
 * tasks.
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

/* Data to be passed to callable. */
struct problem_parameters {
    unsigned n;
};

static void *
fibonacci(struct thread_pool * pool, void * _data)
{
    struct problem_parameters * p = _data;
    if (p->n <= 1)
        return (void *) 1;

    struct problem_parameters left_half = { .n = p->n - 1 };
    struct problem_parameters right_half = { .n = p->n - 2 };
    struct future * f = thread_pool_submit(pool, fibonacci, &right_half);
    uintptr_t lresult = (uintptr_t) fibonacci(pool, &left_half);
    uintptr_t rresult = (uintptr_t) future_get(f);
    future_free(f);
    return (void *)(lresult + rresult);
}

static void usage(char *av0, int nthreads) {
    fprintf(stderr, "Usage: %s [-d <n>] [-n <n>] <N>\n"
                    " -n        number of threads in pool, default %d\n"
                    , av0, nthreads);
    exit(0);
}

int
main(int ac, char *av[])
{
    int nthreads = 4;
    int c;
    while ((c = getopt(ac, av, "n:h")) != EOF) {
        switch (c) {
        case 'n':
            nthreads = atoi(optarg);
            break;
        case 'h':
            usage(av[0], nthreads);
        }
    }
    if (optind == ac)
        usage(av[0], nthreads);

    int n = atoi(av[optind]);
    struct thread_pool * pool = thread_pool_new(nthreads);

    struct problem_parameters roottask = { .n = n };

    unsigned long long F[n+1];
    F[0] = F[1] = 1;
    int i;
    for (i = 2; i < n + 1; i++) {
        F[i] = F[i-1] + F[i-2];
    }

    printf("starting...\n");
    struct benchmark_data* bdata = start_benchmark();
    struct future *f = thread_pool_submit(pool, fibonacci, &roottask);
    unsigned long long Fvalue = (unsigned long long) future_get(f);
    stop_benchmark(bdata);
    future_free(f);
    if (Fvalue != F[n]) {
        printf("result %lld should be %lld\n", Fvalue, F[n]);
        abort();
    } else {
        printf("result ok.\n");
        report_benchmark_results(bdata);
    }

    thread_pool_shutdown_and_destroy(pool);
    return 0;
}

