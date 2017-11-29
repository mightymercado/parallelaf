# parallelaf
Solving Lights Out Boards using Standard Depth-Parallelized Breadth-First Search (BFS) with low-level optimizations

# Serial Algorithm 
Standalone Repository: https://github.com/mightymercado/Very-Fast-Lights-Out-Solver 
1. The serial algorithm uses register-level parallelism. 

# Parallel Algorithm Explanation

1. For each core-affined thread, store two queues, namely, current_level and next_level. The current_level queue denotes the nodes that are in the frontier of the search tree which are all at the same level L. The next_level queue is used to store the cumulative adjacency of each node in the current_level. All nodes in the next_level queue will have a depth/level of L+1.

2. After all the threads have processed their corresponding current_level queues and pushed their adjacencies to the next_level queues, we set the current_level = next_level and clear the next_level.

3. We repeat this process for each level.

# Issues
1. Each thread uses the visited array which does cause race condition. However, it does not affect the optimality of the length of the solution.

2. Register-level parallelism penalizes the runtime in the parallel algorithm because of race condition between registers and execution units.

# Results without optimizations

gcc lit.c -o lit -pthread 

Parallel Runtime: 2.64

Serial Runtime: 5.06

# Results with compiler optimizations

gcc lit.c -o lit -pthread -Ofast -unroll-loops -mtune=intel -march=sandybridge 

Parallel Runtime: 1.24

Serial Runtime: 1.75

