#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ARRAY_SIZE (1 << 20)  // 1 million elements
#define STRIDE 16             // Stride size
#define ITERATIONS 10000      // Number of iterations in each loop

int main() {
    int *array = (int *)malloc(sizeof(int) * ARRAY_SIZE);
    
    if (array == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 1;
    }

    // Initialize the array with sequential values
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }

    // Access the array with a fixed stride and measure time in nanoseconds
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    int sum1 = 0;
    for (int i = 0; i < ARRAY_SIZE && i < ITERATIONS; i += STRIDE) {
        sum1 += array[i];
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    long long time_taken_stride = (end_time.tv_sec - start_time.tv_sec) * 1000000000LL + (end_time.tv_nsec - start_time.tv_nsec);

    printf("Stride Access Time: %lld ns\n", time_taken_stride);

    // Reset the array
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }

    // Access the array with a different fixed stride and measure time in nanoseconds
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    int sum2 = 0;
    for (int i = 0; i < ARRAY_SIZE && i < ITERATIONS * 2; i += STRIDE * 2) {
        sum2 += array[i];
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    long long time_taken_stride2 = (end_time.tv_sec - start_time.tv_sec) * 1000000000LL + (end_time.tv_nsec - start_time.tv_nsec);

    printf("Stride Access Time (Doubled Stride): %lld ns\n", time_taken_stride2);

    free(array);

    return 0;
}
