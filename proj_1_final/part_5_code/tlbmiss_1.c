#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ARRAY_SIZE (1 << 20)  // 1 million elements

int main() {
    int *array = (int *)malloc(sizeof(int) * ARRAY_SIZE);
    
    if (array == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 1;
    }

    // random number generator
    srand(time(NULL));

    // Initialize and start time measurement
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Sequentially access the array
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i*2;  // Write to each element
    }

    // End time measurement
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    // Calculate time taken in nanoseconds
    long long elapsed_ns = (end_time.tv_sec - start_time.tv_sec) * 1000000000LL + (end_time.tv_nsec - start_time.tv_nsec);

    printf("Sequential Access Time: %lld ns\n", elapsed_ns);

    // Reset the array
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = 0;
    }

    // Randomly access the array
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (int i = 0; i < ARRAY_SIZE; i++) {
        int random_index = rand() % ARRAY_SIZE;
        array[random_index] = i*2;  // Write to a random element
    }

    // End time measurement
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    // Calculate time taken in nanoseconds
    elapsed_ns = (end_time.tv_sec - start_time.tv_sec) * 1000000000LL + (end_time.tv_nsec - start_time.tv_nsec);

    printf("Random Access Time: %lld ns\n", elapsed_ns);

    free(array);

    return 0;
}
