#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libgen.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#define MAX_THREADS 16

/** @brief Worker threads' function, which will concurrently sort the assigned array of integers */
void *worker(void *id);

/**
 * @brief Merge a bitonic sequence into an ascending or descending sequence 
 * @param arr Bitonic sequence
 * @param size Size of the sequence
 * @param asc Is ascending
 */
void bitonic_merge(int* arr, int size, bool asc);

/**
 * @brief Sort a sequence of integers into a bitonic sequence
 * @param arr Bitonic sequence
 * @param size Size of the sequence
 */
void bitonic_sort(int* arr, int size);

/**
 * @brief Swap elements in array in ascending or descending order
 * @param arr Array of elements to swap
 * @param i Index of first element
 * @param j Index of second element
 * @param asc Is ascending
 */
void swap(int* arr, int i, int j, bool asc);

/**
 * @brief Structure holding the work details that the sorter thread should use when sorting at the current stage
 * 
 * @param array address of the array to sort
 * @param array_size number of elements to sort
 * @param ascending whether the sort should be in ascending or descending order
 * @param should_work whether the sorter thread should perform work or terminate at this stage
 */
struct SorterWork {
    int* array;
    int array_size;
    bool ascending;
    bool should_work;
};

/** @brief Array of work details for each of the sorter threads. Indexed by the threads' application id
 * (which should be an auto-incrementing counter) */
struct SorterWork* work_array;

// State Control variables
/** @brief Workers synchronization point for new work to be assigned */
static pthread_cond_t await_work;
/** @brief Workers synchronization point for new work to be assigned */
static pthread_cond_t workers_finished;
/** \brief Locking flag which warrants mutual exclusion inside the monitor */
static pthread_mutex_t accessCR = PTHREAD_MUTEX_INITIALIZER;

// Global state variables
/** @brief Array of integers to sort */
int* numbers;
/** @brief Number of threads to be run in the program. Can be changed with command-line arguments, 
 * and it's global as the worker threads need to be aware of how many there are 
 */
static int n_threads = 4;
/** @brief Array holding the exit status of the worker threads */
static int* status_workers;



int main (int argc, char *argv[]) {
    int opt;
    extern char* optarg;
    extern int optind;

    while((opt = getopt(argc, argv, "t:h")) != -1) {
        switch (opt) {
            case 't': /* number of threads to be created */
                if (atoi(optarg) <= 0){ 
                    fprintf(stderr, "%s: non positive number\n", basename(argv[0]));
                    print_usage(basename(argv[0]));
                    return EXIT_FAILURE;
                }
                n_threads = (int) atoi(optarg);
                if (n_threads > MAX_THREADS){ 
                    fprintf(stderr, "%s: too many threads\n", basename(argv[0]));
                    print_usage(basename(argv[0]));
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                print_usage(basename(argv[0]));
                return EXIT_SUCCESS;
            case '?': /* invalid option */
                fprintf (stderr, "%s: invalid option\n", basename(argv[0]));
                return EXIT_FAILURE;
        }
    }

    if (argc < 2) {
        fprintf(stderr, "Input some files to process!\n");
        exit(EXIT_FAILURE);
    }

    pthread_t* t_worker_id;         // workers internal thread id array
    unsigned int* worker_id;        // workers application thread id array
    int* pStatus;                   // pointer to execution status
    int i;                          // counting variable

    /* initializing the application defined thread id arrays for the producers and the consumers and the random number
     generator */

    if ((t_worker_id = malloc(n_threads * sizeof(pthread_t))) == NULL
            || (worker_id = malloc(n_threads * sizeof(unsigned int))) == NULL
            || (status_workers = malloc(n_threads * sizeof(unsigned int))) == NULL) {
        fprintf(stderr, "error on allocating space to both internal / external worker id arrays\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_threads; i++)
        worker_id[i] = i;

    // Launch Workers
    for (i = 0; i < n_threads; i++) {
        if (pthread_create(&t_worker_id[i], NULL, worker, &worker_id[i]) != 0) {
            fprintf(stderr, "error on creating worker thread");
            exit(EXIT_FAILURE);
        }
    }

}

void swap(int* arr, int i, int j, bool asc) {
    if (asc && arr[i] > arr[j]) {
        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
    else if(!asc && arr[i] < arr[j]) {
        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

void bitonic_merge(int* arr, int size, bool asc) {
    int v = size >> 1;
    int nL = 1;
    int n, u;
    for (int m = 0; m < log2(size); m++) {
        n = 0;
        u = 0;
        while (n < nL) {
            for (int t = 0; t < v; t++) {
                swap(arr, t+u, t+u+v, asc);    
            }
            u += (v << 1);
            n += 1;
        }
        v >>= 1;
        nL <<= 1;
    }
}

void bitonic_sort(int* arr, int size) {
    for (int j = 1; j <= log2(size); j++) {
        bool is_asc = true;
        int N = pow(2, j);
        for (int i = 0; i < size; i += N) {
            bitonic_merge(arr, N, is_asc);
            is_asc = !is_asc;
        }
    }
}