#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>

#include "threadpool_lib.h"

// http://www.guyrutenberg.com/2007/09/22/profiling-code-using-clock_gettime/
struct timespec timespec_diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}

void timespec_print(struct timespec ts, char *buf, size_t buflen)
{
    snprintf(buf, buflen, "%lld.%.9ld", (long long)ts.tv_sec, ts.tv_nsec);
}

/**
 * Count number of threads by scanning /proc/self/status
 * for the Threads: ... line
 */
int
count_number_of_threads(void)
{
    FILE * p = fopen("/proc/self/status", "r");
    while (!feof(p)) {
        int threadsleft;
        char buf[128];
        fgets(buf, sizeof buf, p);
        if (sscanf(buf, "Threads: %d\n", &threadsleft) != 1)
            continue;

        fclose(p);
        return threadsleft;
    }
    printf("Internal error, please send email to gback@cs.vt.edu\n");
    abort();
}

// int rc = getrusage(RUSAGE_CHILDREN, &usage);
//           struct rusage {
//               struct timeval ru_utime; /* user time used */
//               struct timeval ru_stime; /* system time used */
//               long   ru_maxrss;        /* maximum resident set size */
//               long   ru_ixrss;         /* integral shared memory size */
//               long   ru_idrss;         /* integral unshared data size */
//               long   ru_isrss;         /* integral unshared stack size */
//               long   ru_minflt;        /* page reclaims */
//               long   ru_majflt;        /* page faults */
//               long   ru_nswap;         /* swaps */
//               long   ru_inblock;       /* block input operations */
//               long   ru_oublock;       /* block output operations */
//               long   ru_msgsnd;        /* messages sent */
//               long   ru_msgrcv;        /* messages received */
//               long   ru_nsignals;      /* signals received */
//               long   ru_nvcsw;         /* voluntary context switches */
//               long   ru_nivcsw;        /* involuntary context switches */
//           };
//
static void print_rusage_as_json(FILE *output, struct rusage *usage)
{
    fprintf(output, "\"ru_utime\" : %ld.%06ld, \"ru_stime\" : %ld.%06ld, \"ru_nvcsw\" : %ld, \"ru_nivcsw\" : %ld",
        usage->ru_utime.tv_sec, usage->ru_utime.tv_usec,
        usage->ru_stime.tv_sec, usage->ru_stime.tv_usec,
        usage->ru_nvcsw, usage->ru_nivcsw
    );
}

static void print_rusage_to_human(FILE *output, struct rusage *usage)
{
    fprintf(output, "user time: %ld.%06lds\nsystem time: %ld.%06lds\n",
        usage->ru_utime.tv_sec, usage->ru_utime.tv_usec,
        usage->ru_stime.tv_sec, usage->ru_stime.tv_usec
    );
}

/* Compute the diff of interesting parameters in two rusage structs */
static void rusagesub(struct rusage *end, struct rusage *start, struct rusage *diff)
{
    diff->ru_nvcsw = end->ru_nvcsw - start->ru_nvcsw;
    diff->ru_nivcsw = end->ru_nivcsw - start->ru_nivcsw;
    timersub(&end->ru_utime, &start->ru_utime, &diff->ru_utime);
    timersub(&end->ru_stime, &start->ru_stime, &diff->ru_stime);
}

struct benchmark_data {
    struct rusage rstart, rend, rdiff;
    struct timeval start, end, diff;
};

struct benchmark_data * start_benchmark(void)
{
    struct benchmark_data * bdata = malloc(sizeof *bdata);
    
    int rc = getrusage(RUSAGE_SELF, &bdata->rstart);
    if (rc == -1)
        perror("getrusage");

    gettimeofday(&bdata->start, NULL);
    return bdata;
}

void stop_benchmark(struct benchmark_data * bdata)
{
    gettimeofday(&bdata->end, NULL);
    int rc = getrusage(RUSAGE_SELF, &bdata->rend);
    if (rc == -1)
        perror("getrusage");

    rusagesub(&bdata->rend, &bdata->rstart, &bdata->rdiff);
    timersub(&bdata->end, &bdata->start, &bdata->diff);
}

void report_benchmark_results(struct benchmark_data *bdata)
{
    char buf[80];
    snprintf(buf, sizeof buf, "runresult.%d.json", getppid());
    FILE * f = fopen(buf, "w");
    if (f == NULL) {
        perror("fopen");
        abort();
    }

    // fprintf(stderr, "Writing %s\n", buf);
    fprintf(f, "{");
    print_rusage_as_json(f, &bdata->rdiff);
    fprintf(f, ", \"realtime\" : %ld.%06ld", bdata->diff.tv_sec, bdata->diff.tv_usec);
    fprintf(f, "}");
    fclose(f);
}

void report_benchmark_results_to_human(FILE *f, struct benchmark_data *bdata)
{
    // fprintf(stderr, "Writing %s\n", buf);
    print_rusage_to_human(f, &bdata->rdiff);
    fprintf(f, "real time: %ld.%06lds\n", bdata->diff.tv_sec, bdata->diff.tv_usec);
}
