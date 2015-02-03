/*
 * Parallel Mergesort.
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
#include <sys/resource.h>
#include <pthread.h>
#include <assert.h>
#include <getopt.h>

/* When to switch from parallel to serial */
#define SERIAL_MERGE_SORT_THRESHOLD    1000
static int min_task_size = SERIAL_MERGE_SORT_THRESHOLD;

#define INSERTION_SORT_THRESHOLD    16
static int insertion_sort_threshold = INSERTION_SORT_THRESHOLD;

#include "threadpool.h"
#include "threadpool_lib.h"
#define DEFAULT_THREADS 4
static int nthreads = DEFAULT_THREADS;

typedef void (*sort_func)(int *, int);

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
 * Utilities: insertion sort.
 */
static void insertionsort(int *a, int lo, int hi) 
{
    int i;
    for (i = lo+1; i <= hi; i++) {
        int j = i;
        int t = a[j];
        while (j > lo && t < a[j - 1]) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = t;
    }
}

static void
merge(int * a, int * b, int bstart, int left, int m, int right)
{
    if (a[m] <= a[m+1])
        return;

    memcpy(b + bstart, a + left, (m - left + 1) * sizeof (a[0]));
    int i = bstart;
    int j = m + 1;
    int k = left;

    while (k < j && j <= right) {
        if (b[i] < a[j])
            a[k++] = b[i++];
        else
            a[k++] = a[j++];
    }
    memcpy(a + k, b + i, (j - k) * sizeof (a[0]));
}

/* ------------------------------------------------------------- 
 * Serial implementation.
 */
static void
mergesort_internal(int * array, int * tmp, int left, int right)
{
    if (right - left < insertion_sort_threshold) {
        insertionsort(array, left, right);
    } else {
        int m = (left + right) / 2;
        mergesort_internal(array, tmp, left, m);
        mergesort_internal(array, tmp, m + 1, right);
        merge(array, tmp, 0, left, m, right);
    }
}

static void
mergesort_serial(int * array, int n)
{
    if (n < insertion_sort_threshold) {
        insertionsort(array, 0, n);
    } else {
        int * tmp = malloc(sizeof(int) * (n / 2 + 1));
        mergesort_internal(array, tmp, 0, n-1);
        free (tmp);
    }
}

/* ------------------------------------------------------------- 
 * Parallel implementation.
 */

/* msort_task describes a unit of parallel work */
struct msort_task {
    int *array;
    int *tmp;
    int left, right;
}; 

/* Parallel mergesort */
static void  
mergesort_internal_parallel(struct thread_pool * threadpool, struct msort_task * s)
{
    int * array = s->array;
    int * tmp = s->tmp;
    int left = s->left;
    int right = s->right;

    if (right - left <= min_task_size) {
        mergesort_internal(array, tmp + left, left, right);
    } else {
        int m = (left + right) / 2;

        struct msort_task mleft = {
            .left = left,
            .right = m,
            .array = array,
            .tmp = tmp
        };
        struct future * lhalf = thread_pool_submit(threadpool, 
                                   (fork_join_task_t) mergesort_internal_parallel,  
                                   &mleft);

        struct msort_task mright = {
            .left = m + 1,
            .right = right,
            .array = array,
            .tmp = tmp
        };
        mergesort_internal_parallel(threadpool, &mright);
        future_get(lhalf);
        future_free(lhalf);
        merge(array, tmp, left, left, m, right);
    }
}

static void 
mergesort_parallel(int *array, int N) 
{
    int * tmp = malloc(sizeof(int) * (N));
    struct msort_task root = {
        .left = 0, .right = N-1, .array = array, .tmp = tmp
    };

    struct thread_pool * threadpool = thread_pool_new(nthreads);
    mergesort_internal_parallel(threadpool, &root);
    thread_pool_shutdown_and_destroy(threadpool);
    free (tmp);
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
    if (report) {
        report_benchmark_results(bdata);
    }

    printf("%s result ok. Timings follow\n", benchmark_name);
    report_benchmark_results_to_human(stdout, bdata);

    free(bdata);
    free(a);
}

static void
usage(char *av0, int exvalue)
{
    fprintf(stderr, "Usage: %s [-i <n>] [-n <n>] [-b] [-q] [-s <n>] <N>\n"
                    " -i        insertion sort threshold, default %d\n"
                    " -m        minimum task size before using serial mergesort, default %d\n"
                    " -n        number of threads in pool, default %d\n"
                    " -b        run built-in qsort\n"
                    " -s        specify srand() seed\n"
                    " -q        also run serial mergesort\n"
                    , av0, INSERTION_SORT_THRESHOLD, SERIAL_MERGE_SORT_THRESHOLD, DEFAULT_THREADS);
    exit(exvalue);
}

int 
main(int ac, char *av[]) 
{
    int c;
    bool run_builtin_qsort = false;
    bool run_serial_msort = false;

    while ((c = getopt(ac, av, "i:n:bhs:qm:")) != EOF) {
        switch (c) {
        case 'i':
            insertion_sort_threshold = atoi(optarg);
            break;
        case 'm':
            min_task_size = atoi(optarg);
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
            run_serial_msort = true;
            break;
        case 'h':
            usage(av[0], EXIT_SUCCESS);
        }
    }
    if (optind == ac)
        usage(av[0], EXIT_FAILURE);

    int N = atoi(av[optind]);

    int i, * a0 = malloc(N * sizeof(int));
    for (i = 0; i < N; i++)
        a0[i] = random();

    if (run_builtin_qsort)
        benchmark("Built-in qsort", builtin_qsort, a0, N, false);

    if (run_serial_msort)
        benchmark("mergesort serial", mergesort_serial, a0, N, false);

    printf("Using %d threads, parallel/serials threshold=%d insertion sort threshold=%d\n", 
        nthreads, min_task_size, insertion_sort_threshold);
    benchmark("mergesort parallel", mergesort_parallel, a0, N, true);

    return EXIT_SUCCESS;
}

