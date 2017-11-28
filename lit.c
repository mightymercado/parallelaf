#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#define N 5
#define STATES (1 << (N * N))

// unpacked toggle masks
int mask[] = {
    35, 71, 142, 284, 536, 1121, 2274, 4548, 9096, 17168, 35872, 72768, 145536, 291072, 549376,
    1147904, 2328576, 4657152, 9314304, 17580032, 3178496, 7405568, 14811136, 29622272, 25690112
};

// prv[state] is the toggle used to arrive at state
unsigned char prv[STATES];
#define lim 2097152 
// visited array for bfs
unsigned char visited[1<<22];
#define visit(i) visited[i >> 3] |= 1 << (i & 7)
#define test(i) ((visited[i >> 3] & (1 << (i & 7))) != 0)
// set used for removing duplicaed when combining next_level queues
unsigned char set[1<<22];
#define insert(i) set[i >> 3] |= 1 << (i & 7)
#define check(i) ((set[i >> 3] & (1 << (i & 7))) != 0)

#define MAX_CORES 8
int cores;
int arr1[1<<25];
int arr2[MAX_CORES][1<<25];

// next_level in bfs tree
int *next[MAX_CORES];
int next_size[MAX_CORES];

// current_level in bfs tree
int *curr;
int curr_size;

void render(uint32_t board) {
    uint32_t i, j;
    for (i = 0; i < N; ++i) {
        for (j = 0; j < N; ++j) {
            printf("%d", (board & (1 << ((i * N) + j))) != 0);
        }
        printf("\n");
    }
}

void *partial_consumer(void *data) {
    // all partial consumers process mutually exclusive portions of the current_level queues
    // but they also have different next_level queues which will then be merged when all partial consumers are done

    // unpack
    // first 30 bits = start
    // second 30 bits = end
    // remaining 4 bits = cores
    int start = (uint64_t) data >> 34;
    int end = ((uint64_t) data >> 4) & ((1 << 30) - 1);
    int core = (uint64_t) data & 0xf;
    
    for (int j = start; j < end; j++) {
        // get state
        int u = curr[j];
        for (int i = 24; i >= 0; --i) {
            // get next_state
            int x = u ^ mask[i];
            if (!test(x)) {
                // add to it's own next_level or queue
                next[core][next_size[core]++] = x;
                visit(x);
                prv[x] = i;
            }
        }
    }
}

void swap_pointer(int **x, int **y) {
    int *t = *x;
    *x = *y;
    *y = t;
}
void swap_int(int *x, int *y) {
    int t = *x;
    *x = *y;
    *y = t;
}
int min(int a, int b) {
    if (a < b) return a;
    else return b;
}

int max(int a, int b) {
    if (a > b) return a;
    else return b;
}
void bfs(int threshold) {
    cpu_set_t cpus;
    pthread_t threads[MAX_CORES];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    
    int i, j, x, u;
     
    curr_size = 1;
    curr[0] = 0;

    visit(0);    
    while (curr_size != 0) {
        // if the size of the current_level queue is feasible for parallization
        if (curr_size >= threshold && curr_size >= cores) {
            // start threads that consume mutually exclusive regions of the queue
            int start = 0;
            int size = curr_size;
            for (i = 0; i < cores; i++) {
                // set core affinity
                CPU_ZERO(&cpus);
                CPU_SET(i, &cpus);
                pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
                // trim last size if the curr_size is not divisble by cores
                if (i == cores - 1 && curr_size % cores != 0) size = curr_size % cores; 
                // pack data into 64-bit pointer
                uint64_t data = ((uint64_t) start << 34) | (((uint64_t) start + size) << 4) | i;
                // run thread
                pthread_create(&threads[i], &attr, partial_consumer, data);
                start += size;
            }
            // set will be used to merge the next_level queues without duplicates
            memset(set, 0, sizeof set);
            // finish execution
            for (i = 0; i < cores; i++) {
                pthread_join(threads[i], NULL);
            }
            /*
                If we are dealing with large queues, using memcpy will be faster
                because it is probably optimized and avoids branch misprediction (STEP 1).
                To avoid duplicates, we can do another loop that removes duplicates (STEP 2)
            */

            // combine the results without dealing with duplicates
            // STEP 1: pure efficient copy

            // instead of copying the first next_level queue, we swap the pointers
            swap_pointer(&curr, &next[0]);
            curr_size = next_size[0];

            // insert first next_level queue elements into set
            for (i = 0; i < curr_size; i++) {
                insert(curr[i]);
            }

            // copy remaining next_level queues
            for (i = 1; i < cores; i++) {
                memcpy(&curr[curr_size], next[i], sizeof(int) * next_size[i]);
                curr_size += next_size[i];
            }

            // STEP 2
            // remove duplicates, skipping first next_level queue elements
            for (i = next_size[0]; i < curr_size; i++) {
                if (!check(curr[i])) {
                    insert(curr[i]);
                } else {
                    curr[i] = curr[curr_size--];
                }
            }
            memset(next_size, 0, sizeof next_size);
        } else {
            // serial processing of current level
            for (int j = 0; j < curr_size; j++) {
                int u = curr[j];
                for (int i = 24; i >= 0; --i) {
                    int x = u ^ mask[i];
                    if (!test(x)) {
                        next[0][next_size[0]++] = x;
                        visit(x);
                        prv[x] = i;
                    }
                }
            }
            curr_size = next_size[0];            
            next_size[0] = 0;
            swap_pointer(&curr, &next[0]);
        }
    }
}

uint32_t open() {
    FILE *f = fopen("in", "r");
    uint32_t board = 0;
    uint32_t i, c;
    for (i = 0; i < N * N; ++i) {
        c = fgetc(f);
        if (c!='0'&&c!='1') {
            i--; continue;
        }
        board |= (c-'0') * (1 << i);
    }
    fclose(f);
    return board;
}

int run_tests() {
    struct timespec start, finish;
    double elapsed;
    for (int threshold = 100; threshold <= 1e6; threshold*=10) {
        for (cores = 1; cores <= 4; cores++) {
            printf("Cores: %d, Threshold: %d\n", cores, threshold);
            memset(prv, 25, sizeof prv);
            memset(visited, 0, sizeof visited);
            memset(set, 0, sizeof set);
            memset(next_size, 0, sizeof next_size);
            for (int i = 0; i < cores; i++) next[i] = arr2[i];
            curr = arr1;
            
            uint32_t board = open();
            clock_gettime(CLOCK_MONOTONIC, &start);            
            bfs(threshold);
            clock_gettime(CLOCK_MONOTONIC, &finish);
            elapsed = (finish.tv_sec - start.tv_sec);
            elapsed += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;

            // render(board);
            // while (board != 0) {
            //     if (prv[board] == 25) {
            //         break;
            //     }
            //     board = board ^ mask[prv[board]];
            //     render(board);        
            // }
            printf("%.2f\n", elapsed);
            fprintf(stderr, "sleeping for 5 seconds\n");
            sleep(5);
        }
    }
}

int main() {

}