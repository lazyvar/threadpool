/*
 * Thread pool test program.
 * Parallel sum.
 * C version of http://en.cppreference.com/w/cpp/thread/async
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
    unsigned beg, end;
    int *v;
};

#define GRANULARITY 100

static void *
parallel_sum(struct thread_pool * pool, void * _data)
{
    struct problem_parameters * p = _data;
    int i, len = p->end - p->beg;
    if (len < GRANULARITY) {
        uintptr_t sum = 0;
        int * v = p->v;
        for (i = p->beg; i < p->end; i++)
            sum += v[i];
        return (void *) sum;   
    }
    int mid = p->beg + len / 2;

    struct problem_parameters left_half = {
        .beg = p->beg, .end = mid, .v = p->v
    };
    struct problem_parameters right_half = {
        .beg = mid, .end = p->end, .v = p->v
    };
    struct future * f = thread_pool_submit(pool, parallel_sum, &right_half);
    uintptr_t lresult = (uintptr_t) parallel_sum(pool, &left_half);
    uintptr_t rresult = (uintptr_t) future_get(f);
    return (void *)(lresult + rresult);
}

static void usage(char *av0, int nthreads) {
    fprintf(stderr, "Usage: %s [-n <n>] <N>\n"
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

    int len = atoi(av[optind]);
    struct thread_pool * pool = thread_pool_new(nthreads);

    int * v = malloc(sizeof(int) * len);
    struct problem_parameters roottask = {
        .beg = 0, .end = len, .v = v,
    };

    unsigned long long realsum = 0;
    int i;
    for (i = 0; i < len; i++) {
        v[i] = i % 3;
        realsum += v[i];
    }

    printf("starting...\n");
    struct benchmark_data* bdata = start_benchmark();
    struct future *f = thread_pool_submit(pool, parallel_sum, &roottask);
    unsigned long long sum = (unsigned long long) future_get(f);
    stop_benchmark(bdata);
    future_free(f);

    if (sum != realsum) {
        printf("result %lld should be %lld\n", sum, realsum);
        abort();
    } else {
        printf("result ok.\n");
        report_benchmark_results(bdata);
    }

    thread_pool_shutdown_and_destroy(pool);
    return 0;
}

