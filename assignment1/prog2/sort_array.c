/**
 * @file sort_array.c (implementation file)
 *
 * @author Dinis Lei (dinislei@ua.pt), Martinho Tavares (martinho.tavares@ua.pt)
 *
 * @brief Main file for Program 2.
 * 
 * Sort an array of integers stored in a file, whose path is provided as a command-line argument.
 * The bitonic sorting algorithm is used, done using multithreading.
 *
 * @date March 2023
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libgen.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#include "sort_control.h"
#include "constants.h"
#include "sort_array.h"


// Global state variables

/** @brief Number of threads to be run in the program. Can be changed with command-line arguments, 
 * and it's global as the distributor thread needs to be aware of how many there are
 */
int n_threads = 4;
/** @brief Exit status of the main thread when performing operations on the monitor */
int status_main;
/** @brief Exit status of the monitor initialization */
int status_monitor_init;
/** @brief Array holding the exit status of the worker and distributor threads */
int* status_threads;

/**
 * @brief Print command usage.
 *
 * A message specifying how the program should be called is printed.
 *
 * @param cmdName string with the name of the command
 */
static void printUsage (char *cmdName);

/** @brief Worker threads' function, which will concurrently sort the assigned array of integers */
static void *worker(void *id);

/** @brief Distributor thread's function, which will distribute work between the workers */
static void *distributor(void *par);

/**
 * @brief Merge a bitonic sequence into an ascending or descending sequence
 *
 * @param arr bitonic sequence
 * @param size size of the sequence
 * @param asc is ascending
 */
static void bitonicMerge(int* arr, int size, bool asc);

/**
 * @brief Sort a sequence of integers into a bitonic sequence
 *
 * @param arr bitonic sequence
 * @param size size of the sequence
 * @param asc is ascending
 */
static void bitonicSort(int* arr, int size, bool asc);

/**
 * @brief Swap elements in array in ascending or descending order
 *
 * @param arr array of elements to swap
 * @param i index of first element
 * @param j index of second element
 * @param asc is ascending
 */
static void swap(int* arr, int i, int j, bool asc);

/** @brief Execution time measurement */
static double get_delta_time(void);

/**
 * @brief Main thread.
 *
 * Its role is storing the name of the file to process, and launching the distributor and worker threads.
 * Afterwards, it waits for their termination, and validates if the sort was correctly performed.
 * 
 * @param argc number of words of the command line
 * @param argv list of words of the command line
 * @return status of operation
 */
int main(int argc, char *argv[]) {
    int opt;
    extern char* optarg;
    extern int optind;

    while ((opt = getopt(argc, argv, "t:h")) != -1) {
        switch (opt) {
            case 't': /* number of threads to be created */
                if (atoi(optarg) <= 0){ 
                    fprintf(stderr, "%s: non positive number\n", basename(argv[0]));
                    printUsage(basename(argv[0]));
                    exit(EXIT_FAILURE);
                }
                n_threads = (int) atoi(optarg);
                if (n_threads > MAX_THREADS){ 
                    fprintf(stderr, "%s: too many threads\n", basename(argv[0]));
                    printUsage(basename(argv[0]));
                    exit(EXIT_FAILURE);
                }
                break;
            case 'h':
                printUsage(basename(argv[0]));
                return EXIT_SUCCESS;
            case '?': /* invalid option */
                fprintf (stderr, "%s: invalid option\n", basename(argv[0]));
                exit(EXIT_FAILURE);
        }
    }

    if (argc < 2) {
        fprintf(stderr, "Input some files to process!\n");
        exit(EXIT_FAILURE);
    }

    char* filename = argv[optind];

    storeFilename(filename);

    pthread_t* t_worker_id;         // workers internal thread id array
    pthread_t t_distributor_id;     // distributor internal thread id
    unsigned int* worker_id;        // workers application thread id array
    unsigned int distributor_id;    // distributor application thread id
    int* pStatus;                   // pointer to execution status
    int i;                          // counting variable

    /**
     * Allocate memory for and initialize arrays of:
     * - the worker threads' internal IDs
     * - the worker threads' application-defined IDs
     * - the worker threads' status
     * 
     */
    if ((t_worker_id = malloc(n_threads * sizeof(pthread_t))) == NULL
            || (worker_id = malloc(n_threads * sizeof(unsigned int))) == NULL
            || (status_threads = malloc((n_threads + 1) * sizeof(unsigned int))) == NULL) {
        fprintf(stderr, "error on allocating space to both internal / external worker id arrays\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_threads; i++) {
        worker_id[i] = i;
    }
    distributor_id = i;

    (void) get_delta_time();

    // Launch Workers and Distributor
    for (i = 0; i < n_threads; i++) {
        if (pthread_create(&t_worker_id[i], NULL, worker, &worker_id[i]) != 0) {
            fprintf(stderr, "error on creating worker thread");
            exit(EXIT_FAILURE);
        }
    }
    if (pthread_create(&t_distributor_id, NULL, distributor, &distributor_id) != 0) {
        fprintf(stderr, "error on creating worker thread");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_threads; i++) {
        if (pthread_join(t_worker_id[i], (void *)&pStatus) != 0) {
            fprintf(stderr, "error on waiting for thread worker");
            exit(EXIT_FAILURE);
        }
    }
    if (pthread_join(t_distributor_id, (void *)&pStatus) != 0) {
        fprintf(stderr, "error on waiting for thread distributor");
        exit(EXIT_FAILURE);
    }

    if (!validateSort())
        exit(EXIT_FAILURE);

    free(t_worker_id);
    free(worker_id);
    free(status_threads);

    monitorFreeMemory();

    printf ("\nElapsed time = %.6f s\n", get_delta_time ());
    exit(EXIT_SUCCESS);
}


static void *distributor(void *par) {
    unsigned int id = *((unsigned int *) par);

    readIntegerFile(id);

    struct SorterWork* work_to_distribute = malloc(n_threads * sizeof(struct SorterWork));

    for (int stage = n_threads; stage > 0; stage >>= 1) {

        // The first stage is done differently, since no workers are dispensed from work.
        // In the following stages, however, half of the workers will be dispensed from work,
        // while the other half works as expected.
        int n_workers = (stage == n_threads) ? stage : stage << 1;
        for (int work_id = 0; work_id < n_workers; work_id++) {
            work_to_distribute[work_id].should_work = work_id < stage;   // distribute work to half of the workers, and tell the other half to stop working (unless it's the first stage)          
            if (work_to_distribute[work_id].should_work) {                
                defineIntegerSubsequence(id, stage, work_id, &work_to_distribute[work_id].array, &work_to_distribute[work_id].array_size);
                work_to_distribute[work_id].ascending = (work_id % 2) == 0;   // the sorts alternate between ascending and descending
                work_to_distribute[work_id].skip_sort = stage != n_threads;   // the bitonic sort (including merging) is only done in the first stage, afterwards it's merely merging
            }
        }

        distributeWork(id, work_to_distribute, n_workers);
    }   

    work_to_distribute[0].should_work = false;
    distributeWork(id, work_to_distribute, 1);

    free(work_to_distribute);

    status_threads[id] = EXIT_SUCCESS;
    pthread_exit(&status_threads[id]);
}

static void *worker(void *par) {
    unsigned int id = *((unsigned int *) par);
    struct SorterWork work;
    
    while (true) {
        fetchWork(id, &work);
        
        if (!work.should_work) {
            break;
        }

        if (work.skip_sort) {
            bitonicMerge(work.array, work.array_size, work.ascending); 
        }
        else {
            bitonicSort(work.array, work.array_size, work.ascending);
        }
        
        reportWork(id);
    }
    
    status_threads[id] = EXIT_SUCCESS;
    pthread_exit(&status_threads[id]);
}

static void swap(int* arr, int i, int j, bool asc) {
    if (asc && arr[i] > arr[j]) {              
        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
    else if (!asc && arr[i] < arr[j]) {
        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

static void bitonicMerge(int* arr, int size, bool asc) {
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

static void bitonicSort(int* arr, int size, bool asc) {
    for (int j = 1; j <= log2(size); j++) {
        int N = pow(2, j);
        for (int i = 0; i < size; i += N) {
            bitonicMerge(arr + i, N, asc);
            asc = !asc;
        }
    }
}

static double get_delta_time(void) {
    static struct timespec t0, t1;

    t0 = t1;
    if(clock_gettime(CLOCK_MONOTONIC, &t1) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    return (double) (t1.tv_sec - t0.tv_sec) + 1.0e-9 * (double) (t1.tv_nsec - t0.tv_nsec);
}

static void printUsage (char *cmdName) {
    fprintf(stderr, "\nSynopsis: %s [OPTIONS] FILE...\n"
           "  OPTIONS:\n"
           "  -h      --- print this help\n"
           "  -t      --- Nº of worker threads launched, MAX = %d\n", cmdName, MAX_THREADS);
}
