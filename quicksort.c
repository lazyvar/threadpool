/*
 * Parallel Quicksort.
 *
 * Demo application that shows how one might use threadpools/futures
 * in an application.
 *
 * Requires threadpool.c/threadpool.h
 *
 * Written by Godmar Back gback@cs.vt.edu for CS3214 Fall 2014.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <assert.h>
#include <getopt.h>

#include "threadpool_lib.h"
#include "threadpool.h"

typedef void (*sort_func)(int *, int);

#define DEFAULT_THREADS 4
static int nthreads = DEFAULT_THREADS;

/* Return true if array 'a' is sorted. */
static bool
check_sorted(int a[], int n) 
{
    int i;
    for (i = 0; i < n-1; i++)
        if (a[i] > a[i+1])
            return false;
    return true;
}

/* ------------------------------------------------------------- 
 * Built-in qsort.
 */
static int cmp_int(const void *a, const void *b)
{
    return *(int *)a - *(int *)b;
}

static void builtin_qsort(int *a, int N)
{
    qsort(a, N, sizeof(int), cmp_int);
}

/* ------------------------------------------------------------- 
 * Quicksort utility routines.
 */
/* Swap two elements */
static inline void swap(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Partitioning using middle element as pivot */
static int 
qsort_partition(int * array, int left, int right)
{
    int middle = left + (right-left)/2;

    // left <=> middle
    swap(array + left, array + middle);

    int current, last = left;
    for (current = left + 1; current <= right; ++current) {
        if (array[current] < array[left]) {
            ++last;
            // last <=> current
            swap(array + last, array + current);
        }
    }

    // left <=> last
    swap(array + left, array + last);
    return last;
}

/* ------------------------------------------------------------- 
 * Serial implementation.
 */
static void 
qsort_internal_serial(int *array, int left, int right) 
{
    if (left >= right)
        return;

    int split = qsort_partition(array, left, right);
    qsort_internal_serial(array, left, split - 1);
    qsort_internal_serial(array, split + 1, right);
}

static void 
qsort_serial(int *array, int N) 
{
    qsort_internal_serial(array, 0, N - 1);
}

/* ------------------------------------------------------------- 
 * Parallel implementation.
 */

/* qsort_task describes a unit of parallel work */
struct qsort_task {
    int *array;
    int left, right, depth;
}; 

/* Parallel qsort - returns size of segment sorted */
static int  
qsort_internal_parallel(struct thread_pool * threadpool, struct qsort_task * s)
{
    int * array = s->array;
    int left = s->left;
    int right = s->right;
    int depth = s->depth;

    if (left >= right)
        return 0;

    int split = qsort_partition(array, left, right);
    if (depth < 1) {
        qsort_internal_serial(array, left, split - 1);
        qsort_internal_serial(array, split + 1, right);
    } else {
        struct qsort_task qleft = {
            .left = s->left,
            .right = split-1,
            .depth = s->depth-1,
            .array = s->array
        };
        struct future * lhalf = thread_pool_submit(threadpool, 
                                   (fork_join_task_t) qsort_internal_parallel,  
                                   &qleft);
        struct qsort_task qright = {
            .left = split+1,
            .right = s->right,
            .depth = s->depth-1,
            .array = s->array
        };
        qsort_internal_parallel(threadpool, &qright);
        future_get(lhalf);
        future_free(lhalf);
    }
    return right - left;
}

// maximum depth to which each recursive call is executed in parallel
static int depth = 3;

static void 
qsort_parallel(int *array, int N) 
{
    struct qsort_task root = {
        .left = 0, .right = N-1, .depth = depth, .array = array
    };

    struct thread_pool * threadpool = thread_pool_new(nthreads);
    qsort_internal_parallel(threadpool, &root);
    thread_pool_shutdown_and_destroy(threadpool);
}

/*
 * Benchmark one run of sort_func sorter
 */
static void 
benchmark(const char *benchmark_name, sort_func sorter, int *a0, int N, bool report)
{
    int *a = malloc(N * sizeof(int));
    memcpy(a, a0, N * sizeof(int));

    struct benchmark_data * bdata = start_benchmark();

    // parallel section here, including thread pool startup and shutdown
    sorter(a, N);

    stop_benchmark(bdata);

    // consistency check
    if (!check_sorted(a, N)) {
        fprintf(stderr, "Sort failed\n");
        abort();
    }

    // report only if successful
    if (report)
        report_benchmark_results(bdata);

    printf("%s result ok. Timings follow\n", benchmark_name);
    report_benchmark_results_to_human(stdout, bdata);

    free(bdata);
    free(a);
}


static void
usage(char *av0, int depth)
{
    fprintf(stderr, "Usage: %s [-d <n>] [-n <n>] [-b] [-q] [-s <n>] <N>\n"
                    " -d        parallel recursion depth, default %d\n"
                    " -n        number of threads in pool, default %d\n"
                    " -b        run built-in qsort\n"
                    " -s        specify srand() seed\n"
                    " -q        run serial qsort\n"
                    , av0, depth, DEFAULT_THREADS);
    exit(0);
}

int 
main(int ac, char *av[]) 
{
    int c;
    bool run_builtin_qsort = false;
    bool run_serial_qsort = false;

    while ((c = getopt(ac, av, "d:n:bhs:q")) != EOF) {
        switch (c) {
        case 'd':
            depth = atoi(optarg);
            break;
        case 'n':
            nthreads = atoi(optarg);
            break;
        case 's':
            srand(atoi(optarg));
            break;
        case 'b':
            run_builtin_qsort = true;
            break;
        case 'q':
            run_serial_qsort = true;
            break;
        case 'h':
            usage(av[0], depth);
        }
    }
    if (optind == ac)
        usage(av[0], depth);

    int N = atoi(av[optind]);

    int i, * a0 = malloc(N * sizeof(int));
    for (i = 0; i < N; i++)
        a0[i] = random();

    if (run_builtin_qsort)
        benchmark("Built-in qsort", builtin_qsort, a0, N, false);

    if (run_serial_qsort)
        benchmark("qsort serial", qsort_serial, a0, N, false);

    printf("Using %d threads, recursive parallel depth=%d\n", nthreads, depth);
    benchmark("qsort parallel", qsort_parallel, a0, N, true);

    return 0;
}

