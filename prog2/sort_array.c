/**
 * @file sort_array.c (implementation file)
 *
 * @author Dinis Lei (you@domain.com), Martinho Tavares (martinho.tavares@ua.pt)
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

/**
 * @brief Print program usage
 * @param cmdName program's name
 */
static void printUsage (char *cmdName);

/** @brief Worker threads' function, which will concurrently sort the assigned array of integers */
static void *worker(void *id);

/** @brief Distributor thread's function, which will distribute work between the workers */
static void *distributor(void *par);

/**
 * @brief Merge a bitonic sequence into an ascending or descending sequence 
 * @param arr bitonic sequence
 * @param size size of the sequence
 * @param asc is ascending
 */
void bitonicMerge(int* arr, int size, bool asc);

/**
 * @brief Sort a sequence of integers into a bitonic sequence
 * @param arr bitonic sequence
 * @param size size of the sequence
 * @param asc is ascending
 */
void bitonicSort(int* arr, int size, bool asc);

/**
 * @brief Swap elements in array in ascending or descending order
 * @param arr array of elements to swap
 * @param i index of first element
 * @param j index of second element
 * @param asc is ascending
 */
void swap(int* arr, int i, int j, bool asc);

/** \brief execution time measurement */
static double get_delta_time(void);

// Global state variables
/** @brief Number of threads to be run in the program. Can be changed with command-line arguments, 
 * and it's global as the distributor thread needs to be aware of how many there are */
int n_threads = 4;
/** @brief Exit status of the monitor initialization */
int status_monitor_init;
/** @brief Array holding the exit status of the worker threads */
int* status_workers;
/** @brief Exit status of the distributor */
int status_distributor;

int main (int argc, char *argv[]) {
    int opt;
    extern char* optarg;
    extern int optind;

    while((opt = getopt(argc, argv, "t:h")) != -1) {
        switch (opt) {
            case 't': /* number of threads to be created */
                if (atoi(optarg) <= 0){ 
                    fprintf(stderr, "%s: non positive number\n", basename(argv[0]));
                    printUsage(basename(argv[0]));
                    return EXIT_FAILURE;
                }
                n_threads = (int) atoi(optarg);
                if (n_threads > MAX_THREADS){ 
                    fprintf(stderr, "%s: too many threads\n", basename(argv[0]));
                    printUsage(basename(argv[0]));
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                printUsage(basename(argv[0]));
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
            || (status_workers = malloc(n_threads * sizeof(unsigned int))) == NULL) {
        fprintf(stderr, "error on allocating space to both internal / external worker id arrays\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_threads; i++)
        worker_id[i] = i;
    distributor_id = i;

    (void) get_delta_time ();

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
        return EXIT_FAILURE;


    printf ("\nElapsed time = %.6f s\n", get_delta_time ());
    return EXIT_SUCCESS;
}


void *distributor(void *par) {
    unsigned int id = *((unsigned int *) par);

    readIntegerFile();

    struct SorterWork* work_to_distribute = malloc(n_threads * sizeof(struct SorterWork));

    for (int stage = n_threads; stage > 0; stage >>= 1) {

        // The first stage is done differently, since no workers are dispensed from work.
        // In the following stages, however, half of the workers will be dispensed from work,
        // while the other half works as expected.
        int n_workers = (stage == n_threads) ? stage : stage << 1;
        for (int work_id = 0; work_id < n_workers; work_id++) {
            work_to_distribute[work_id].should_work = work_id < stage;            
            if (work_to_distribute[work_id].should_work) {                
                defineIntegerSubsequence(stage, work_id, &work_to_distribute[work_id].array, &work_to_distribute[work_id].array_size);
                work_to_distribute[work_id].ascending = (work_id % 2) == 0;
                work_to_distribute[work_id].skip_sort = stage != n_threads;
            }
        }

        distributeWork(work_to_distribute, n_workers);
    }   

    work_to_distribute[0].should_work = false;
    distributeWork(work_to_distribute, 1);

    status_workers[id] = EXIT_SUCCESS;
    pthread_exit(&status_workers[id]);
}

// TODO: proper error handling for pthread functions
void *worker(void *par) {
    unsigned int id = *((unsigned int *) par);
    struct SorterWork work;
    
    while (true) {
        fetchWork(id, &work);
        
        if (!work.should_work)
            break;

        if (work.skip_sort) {
            printf("Merge\n");
            bitonicMerge(work.array, work.array_size, work.ascending); 
        }
        else {
            printf("Sort\n");
            bitonicSort(work.array, work.array_size, work.ascending);
        }
        
        reportWork();
    }
    printf("Exiting %d\n", id);
    status_workers[id] = EXIT_SUCCESS;
    pthread_exit(&status_workers[id]);
}

void swap(int* arr, int i, int j, bool asc) {
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

void bitonicMerge(int* arr, int size, bool asc) {
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

void bitonicSort(int* arr, int size, bool asc) {
    for (int j = 1; j <= log2(size); j++) {
        int N = pow(2, j);
        for (int i = 0; i < size; i += N) {
            bitonicMerge(arr + i, N, asc);
            asc = !asc;
        }
    }
}

static double get_delta_time(void)
{
  static struct timespec t0, t1;

  t0 = t1;
  if(clock_gettime (CLOCK_MONOTONIC, &t1) != 0)
  {
    perror ("clock_gettime");
    exit(1);
  }
  return (double) (t1.tv_sec - t0.tv_sec) + 1.0e-9 * (double) (t1.tv_nsec - t0.tv_nsec);
}

void printUsage (char *cmdName) {
    fprintf(stderr, "\nSynopsis: %s [OPTIONS] FILE...\n"
           "  OPTIONS:\n"
           "  -h      --- print this help\n", cmdName);
}
