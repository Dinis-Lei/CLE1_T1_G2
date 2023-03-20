/**
 * @file sort_control.h (implementation file)
 * 
 * @author Dinis Lei (you@domain.com), Martinho Tavares (martinho.tavares@ua.pt)
 * 
 * @brief Monitor for mutual exclusion in Program 2.
 * 
 * Implements a Lampson/Redell monitor to allow concurrent distribution and requests of sorting work.
 * The sorting work is distributed among all threads in the first stage, and the number of participating
 * threads is halved at each subsequent stage.
 *
 * @date March 2023
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#include "sort_control.h"

// External global variables
extern int n_threads;
extern int* status_workers;
extern int status_monitor_init;
extern int status_distributor;

/** @brief Array of work details for each of the sorter threads. Indexed by the threads' application id
 * (which should be an auto-incrementing counter) */
static struct SorterWork* work_array;
/** @brief Array of work requests for each of the sorter threads. Indexed by the threads' application id
 * (which should be an auto-incrementing counter) */
static bool* request_array;

// Shared memory variables
/** @brief Array of integers to sort */
static int* numbers;
/** @brief Size of numbers array */
static int numbers_size;
/** @brief Name of the file with the array to sort */
static char *filename; 
/** @brief Counter of the number of workers that finished the current sorting stage, so that the distributor can wait for all workers before continuing */
static int n_of_work_finished = 0;

// State Control variables
/** @brief Workers' synchronization point for new work to be assigned */
static pthread_cond_t await_work_assignment;
/** @brief Distributor's synchronization point for when the workers request work */
static pthread_cond_t await_work_request;
/** @brief Distributor's synchronization point for when the workers have finished sorting at this stage */
static pthread_cond_t workers_finished;

/** @brief Locking flag which warrants mutual exclusion inside the monitor */
static pthread_mutex_t access_cr = PTHREAD_MUTEX_INITIALIZER;

/** @brief Flag that guarantees the monitor is initialized once and only once */
static pthread_once_t init = PTHREAD_ONCE_INIT;

/**
 * @brief Assign work according to the current requests.
 * 
 * Depending on how many requests were fulfilled until now, the work assigned at the current stage will differ.
 * 
 * @param stage stage at which the bitonic sorting algorithm is at (must be a power of two)
 * @param n_of_work_requested number of work requests already fulfilled at this stage
 * @return how many requests were fulfilled in this call
 */
static int assignWork(struct SorterWork* work_to_distribute, int n_workers, int n_of_work_requested_so_far);


// Initialization procedures
void storeFilename(char* name) {
    filename = name;
}

static void monitorInitialize() {
    if ((work_array = malloc(n_threads * sizeof(struct SorterWork))) == NULL
            || (request_array = malloc(n_threads * sizeof(bool))) == NULL) {
        fprintf(stderr, "error on allocating space for the work and request arrays\n");
        pthread_exit(&status_monitor_init);
    }

    for (int i = 0; i < n_threads; i++)
        request_array[i] = false;

    pthread_cond_init(&await_work_assignment, NULL);
    pthread_cond_init(&await_work_request, NULL);
    pthread_cond_init(&workers_finished, NULL);
}

// Worker procedures
void fetchWork(int id, struct SorterWork* work) {
    pthread_mutex_lock(&access_cr);
    pthread_once(&init, monitorInitialize);
    request_array[id] = true;
    pthread_cond_signal(&await_work_request);

    // If the request hasn't been fulfilled yet
    while (request_array[id]) {
        pthread_cond_wait(&await_work_assignment, &access_cr);
    }
    *work = work_array[id];
    pthread_mutex_unlock(&access_cr);
}

void reportWork() {
    pthread_mutex_lock(&access_cr);
    pthread_once(&init, monitorInitialize);
    n_of_work_finished++;
    pthread_cond_signal(&workers_finished);
    pthread_mutex_unlock(&access_cr);
}

// Distributor procedures
void readIntegerFile() {
    pthread_mutex_lock(&access_cr);
    pthread_once(&init, monitorInitialize);

    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s", filename);
        return;
    }

    int res = fread(&numbers_size, sizeof(int), 1, file);
    if (ferror(file)) {
        fprintf(stderr, "Invalid file format\n");
        return;
    }

    numbers = (int*) malloc(numbers_size * sizeof(int));

    while (true) {
        res = fread(numbers, sizeof(int), numbers_size, file);
        if (feof(file)) {
            break;
        }
        else if (ferror(file)) {
            printf("Invalid file format\n");
            return;
        }
    }

    fclose(file);

    pthread_mutex_unlock(&access_cr);
}

void distributeWork(struct SorterWork* work_to_distribute, int n_workers) {
    pthread_mutex_lock(&access_cr);
    pthread_once(&init, monitorInitialize);

    int actual_workers = 0;
    for (int i = 0; i < n_workers; i++)
        if (work_to_distribute[i].should_work)
            actual_workers++;

    int n_of_work_requested_so_far = 0;
    n_of_work_requested_so_far = assignWork(work_to_distribute, n_workers, n_of_work_requested_so_far);
    pthread_cond_broadcast(&await_work_assignment);
    while (n_of_work_requested_so_far < n_workers) {
        pthread_cond_wait(&await_work_request, &access_cr);
        n_of_work_requested_so_far += assignWork(work_to_distribute, n_workers, n_of_work_requested_so_far);
        pthread_cond_broadcast(&await_work_assignment);
    }

    // The number of actually effective workers is different from the n_of_work_requested_so_far
    while (n_of_work_finished < actual_workers) {
        pthread_cond_wait(&workers_finished, &access_cr);
    }
    // Reset counter for future work distributions
    n_of_work_finished = 0;

    pthread_mutex_unlock(&access_cr);
}

void defineIntegerSubsequence(int number_of_subsequences, int subsequence_idx, int** subsequence, int* subsequence_size) {
    *subsequence_size = numbers_size / number_of_subsequences;
    *subsequence = numbers + subsequence_idx * (*subsequence_size);
}

static int assignWork(struct SorterWork* work_to_distribute, int n_workers, int n_of_work_requested_so_far) {
    int assigned_work = 0;
    for (int i = 0; i < n_threads; i++) {
        int work_to_distribute_idx = assigned_work + n_of_work_requested_so_far;
        // No more work to be assigned, terminate
        if (work_to_distribute_idx >= n_workers)
            break;

        if (request_array[i]) {
            work_array[i] = work_to_distribute[work_to_distribute_idx];
            assigned_work++;
            request_array[i] = false;
        }
    }
    return assigned_work;
}

// Validation procedure
bool validateSort() {
    int i;
    for (i = 0; i < numbers_size - 1; i++)
        if (numbers[i] > numbers[i+1]) { 
            printf ("Error in position %d between element %d and %d\n",
            i, numbers[i], numbers[i+1]);
            return false;
        }

    printf ("Everything is OK!\n");
    return true;
}
