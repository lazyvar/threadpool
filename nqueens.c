#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "threadpool.h"
#include "threadpool_lib.h"

#define MAX_N (18)
#define WORD_BITS (sizeof(long) * 8)
#define MAX_LONGS (MAX_N * MAX_N / WORD_BITS)

static int max_parallel_depth = 6;
static int valid_solutions[] = {0, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200,
                                73712, 365596, 2279184, 14772512, 95815104, 666090624};

struct board {
    long bits[MAX_LONGS];
};

struct board_state {
    struct board board;
    int N;
    int row;
}; 

static bool is_queen(struct board* board, int x, int y, int N) {
    if (x < 0 || x >= N || y < 0 || y >= N) {
        return false;
    }
    long long idx = x * N + y;
    return (board->bits[idx / WORD_BITS] & (1L << (idx % WORD_BITS))) ==
        (1L << (idx % WORD_BITS));
}

static void set_queen(struct board* board, int x, int y, int N) {
    int idx = x * N + y;
    board->bits[idx / WORD_BITS] |= (1L << (idx % WORD_BITS));
}
static void unset_queen(struct board* board, int x, int y, int N) {
    int idx = x * N + y;
    board->bits[idx / WORD_BITS] &= ~(1L << (idx % WORD_BITS));
}

static int solved(struct board* board, int N) {
    int queens = 0;
    int x, y, k;
    for (x = 0; x < N; x++) {
        for (y = 0; y < N; y++) {
            if (is_queen(board, x, y, N)) {
                queens++;
                for (k = 1; k < N; k++) {
                    if (is_queen(board, x + k, y, N)
                        || is_queen(board, x, y + k, N)
                        || is_queen(board, x + k, y + k, N)
                        || is_queen(board, x + k, y - k, N)) {
                        return -1;
                    }
                }
            }
        }
    }
    return queens;
}


static void* backtrack(struct thread_pool* pool, void* _state) {
    int i;
    struct board_state* state = (struct board_state*)_state;
    if (state->N == state->row && solved(&state->board, state->N) == state->N) {
        //print_board(&state->board, state->N);
        return (void*)1;
    }
    else if (state->row == state->N) {
        return (void*)0;
    }
    else if (solved(&state->board, state->N) == -1) {
        return (void*)0;
    }
    if (state->row < max_parallel_depth) {
        struct board_state* boards = calloc(sizeof(struct board_state), state->N);
        struct future** futures = calloc(sizeof(struct future*), state->N - 1);
        long slns = 0;
        for (i = 0; i < state->N; i++) {
            boards[i].N = state->N;
            boards[i].row = state->row + 1;
            memcpy(&boards[i].board, &state->board, sizeof(struct board));
            set_queen(&boards[i].board, state->row, i, state->N);
            if (i != state->N - 1) {
                futures[i] = thread_pool_submit(pool, backtrack, &boards[i]);
            }
        }
        slns += (long)backtrack(pool, &boards[state->N - 1]);
        for (i = 0; i < state->N - 1; i++) {
            slns += (long)future_get(futures[i]);
            future_free(futures[i]);
        }
        free(futures);
        free(boards);
        return (void*)slns;
    }
    else {
        long slns = 0;
        state->row++;
        for (i = 0; i < state->N; i++) {
            set_queen(&state->board, state->row - 1, i, state->N);
            slns += (long)backtrack(pool, state);
            unset_queen(&state->board, state->row - 1, i, state->N);
        }
        state->row--;
        return (void*)slns;
    }
}

static void benchmark(int N, int threads) {
    printf("Solving N = %d\n", N);
    struct board_state state;
    memset(&state.board, 0, sizeof(struct board));
    state.N = N;
    state.row = 0;

    struct thread_pool* pool = thread_pool_new(threads);

    struct benchmark_data* bdata = start_benchmark();
    
    struct future* fut = thread_pool_submit(pool, backtrack, &state);
    long slns = (long)future_get(fut);

    stop_benchmark(bdata);

    future_free(fut);
    thread_pool_shutdown_and_destroy(pool);

    printf("Solutions: %d\n", (int)slns);
    if (slns == valid_solutions[N]) {
        printf("Solution ok.\n");
        report_benchmark_results(bdata);
    }
    else { 
        fprintf(stderr, "Solution bad.\n");
        abort();
    }
}

static void usage(char *av0, int depth, int nthreads) {
    fprintf(stderr, "Usage: %s [-d <n>] [-n <n>] [-b] [-q] [-s <n>] <N>\n"
                    " -d        parallel recursion depth, default %d\n"
                    " -n        number of threads in pool, default %d\n"
                    , av0, depth, nthreads);
    abort();
}
int main(int ac, char** av) {
    int threads = 4;
    int c;
    while ((c = getopt(ac, av, "d:n:bhs:q")) != EOF) {
        switch (c) {
        case 'd':
            max_parallel_depth = atoi(optarg);
            break;
        case 'n':
            threads = atoi(optarg);
            break;
        case 'h':
            usage(av[0], max_parallel_depth, threads);
        }
    }
    if (optind == ac)
        usage(av[0], max_parallel_depth, threads);

    int N = atoi(av[optind]);
    if (N > MAX_N || N < 0) {
        fprintf(stderr, "N must be between 0 and %d\n", MAX_N);
        abort();
    }
    benchmark(N, threads);
    return 0;
    
}
