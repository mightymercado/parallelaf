#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>
typedef unsigned __int128 uint128_t;
#define N 5
#define STATES (1 << (N * N))

/*
    These are called "toggle masks".
*/
int mask[] = {
    35, 71, 142, 284, 536, 1121, 2274, 4548, 9096, 17168, 35872, 72768, 145536, 291072, 549376,
    1147904, 2328576, 4657152, 9314304, 17580032, 3178496, 7405568, 14811136, 29622272, 25690112
};

uint128_t cmask[5] = {
    443677736902923512 * (uint128_t) (1e14)
    +139394679320,

    142103640876514133 * (uint128_t) (1e16)
    +6035471552168720,

    454731650804845227 * (uint128_t) (1e17)
    +53135089669399040,

    145514128257550472 * (uint128_t) (1e19)
    +8100322869420769280,

    402922264199749179 * (uint128_t) (1e19)
    +4093243870648729600
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

#define MAX_CORES 4
int cores;
int arr1[MAX_CORES][1<<22];
int arr2[MAX_CORES][1<<22];

// next_level in bfs tree
int *next[MAX_CORES];
int next_size[MAX_CORES];

// current_level in bfs tree
int *curr[MAX_CORES];
int curr_size[MAX_CORES];

int size = 0;
int start = 0;
#define lim 2097152

// Queue via Circular array
int q[lim];
int pop() {
    size--;
    return q[start++ & (lim-1)];
}
void push(int x) {
    q[(start + size++) & (lim-1)] = x;
}

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
    int k = (int) data;
    for (int j = 0; j < curr_size[k]; j++) {
        int u = curr[k][j];
        for (int i = 24; i >= 0; --i) {
            int x = u ^ mask[i];
            if (!test(x)) {  
                visit(x);
                next[k][next_size[k]++] = x;
                prv[x] = i;
            } else {
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
void serial_bfs() {
    int i, j, k;
    push(0);
    visit(0);
    while (size != 0) {
        uint128_t u = pop();
        u = (((((((u << 25) | u) << 25) | u) << 25) | u) << 25) | u;
        for (i = 4; i >= 0; --i) {
            // loop unrolling x5 + packed 128-bit XOR
            uint128_t x = u ^ cmask[i];
            int e = x & 0x1FFFFFF;
            x >>= 25;
            int d = x & 0x1FFFFFF;
            x >>= 25;
            int c = x & 0x1FFFFFF;
            x >>= 25;
            int b = x & 0x1FFFFFF;
            x >>= 25;
            int a = x;
            k = (i << 2) + i;
            if (!test(a)) { visit(a); prv[a] = k  ; push(a); }
            if (!test(b)) { visit(b); prv[b] = k+1; push(b); }
            if (!test(c)) { visit(c); prv[c] = k+2; push(c); }
            if (!test(d)) { visit(d); prv[d] = k+3; push(d); }
            if (!test(e)) { visit(e); prv[e] = k+4; push(e); }
        }
    }
}

void parallel_bfs() {
    cpu_set_t cpus;
    pthread_t threads[MAX_CORES];
    pthread_attr_t attr;
    pthread_attr_init(&attr);    
    int s = 25 / cores + (25 % cores != 0);
    for (int i = 0, k = 0; i < 25; i++) {
        curr[k][curr_size[k]++] = mask[i];
        prv[mask[i]] = i;
        visit(mask[i]);
        if (curr_size[k] == s) k++;
    }
    
    visit(0);    
    while (1) {
        // for (int i = 0; i < cores; i++) {
        //     printf("%d ", curr_size[i]);
        // }
        // printf("\n");
        for (int i = 0; i < cores; i++) {
            CPU_ZERO(&cpus);
            CPU_SET(i, &cpus);
            pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
            pthread_create(&threads[i], &attr, partial_consumer, i);
        }
        for (int i = 0; i < cores; i++) {
            pthread_join(threads[i], NULL);
        }
        int sum = 0;
         for (int i = 0; i < cores; i++) {
            swap_pointer(&curr[i], &next[i]);
            curr_size[i] = next_size[i];
            sum += curr_size[i];
            next_size[i] = 0;
        }
        if (sum == 0) break;
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

int main() {
    struct timespec _start, _finish;
    double elapsed;
    
    // get board
    uint32_t board = open();
    
    // initialize memory needed
    cores = 4;
    memset(prv, 25, sizeof prv);
    memset(visited, 0, sizeof visited);
    memset(next_size, 0, sizeof next_size);
    memset(curr_size, 0, sizeof curr_size);
    
    for (int i = 0; i < cores; i++) 
        next[i] = arr2[i],
        curr[i] = arr1[i];
    
    // benchmark parallel
    clock_gettime(CLOCK_MONOTONIC, &_start);            
    parallel_bfs();
    clock_gettime(CLOCK_MONOTONIC, &_finish);
    elapsed = (_finish.tv_sec - _start.tv_sec);
    elapsed += (_finish.tv_nsec - _start.tv_nsec) / 1000000000.0;
    printf("Parallel Runtime: %.2f\n", elapsed);
    fprintf(stderr, "sleeping for 5 seconds\n");

    // render(board);
    // while (board != 0) {
    //     if (prv[board] == 25) {
    //         break;
    //     }
    //     board = board ^ mask[prv[board]];
    //     render(board);        
    // }
    sleep(5);
    
    // initialize memory needed
    memset(visited, 0, sizeof visited);
    memset(prv, 25, sizeof prv);
    start = size = 0;
    // benchmark
    clock_gettime(CLOCK_MONOTONIC, &_start);            
    serial_bfs();
    clock_gettime(CLOCK_MONOTONIC, &_finish);
    elapsed = (_finish.tv_sec - _start.tv_sec);
    elapsed += (_finish.tv_nsec - _start.tv_nsec) / 1000000000.0;
    printf("Serial Runtime: %.2f\n", elapsed);
    

    // render(board);
    // while (board != 0) {
    //     if (prv[board] == 25) {
    //         break;
    //     }
    //     board = board ^ mask[prv[board]];
    //     render(board);        
    // }
}
