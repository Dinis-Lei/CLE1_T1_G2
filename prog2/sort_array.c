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

/**
 * @brief Structure holding the work details that the sorter thread should use when sorting at the current stage
 * 
 * @param array address of the array to sort
 * @param array_size number of elements to sort
 * @param ascending whether the sort should be in ascending or descending order
 * @param should_work whether the sorter thread should perform work or terminate at this stage
 * @param skip_sort whether the bitonic sorting should skip to the merge step
 */
struct SorterWork {
    int* array;
    int array_size;
    bool ascending;
    bool should_work;
    bool skip_sort;
};

/**
 * @brief 
 * @param cmdName 
 */
void printUsage (char *cmdName);

/** @brief Worker threads' function, which will concurrently sort the assigned array of integers */
void *worker(void *id);

/** @brief Distributor thread's function, which will distribute work between the workers */
void *distributor(void *par);

/**
 * @brief Read and Store the contents of the file in Shared Memory
 * @param filename Name of the file to be read
 */
void readStoreFile(char *filename);

/**
 * @brief Merge a bitonic sequence into an ascending or descending sequence 
 * @param arr Bitonic sequence
 * @param size Size of the sequence
 * @param asc Is ascending
 */
void bitonicMerge(int* arr, int size, bool asc);

/**
 * @brief Sort a sequence of integers into a bitonic sequence
 * @param arr Bitonic sequence
 * @param size Size of the sequence
 * @param asc Isascending
 */
void bitonicSort(int* arr, int size, bool asc);

/**
 * @brief Swap elements in array in ascending or descending order
 * @param arr Array of elements to swap
 * @param i Index of first element
 * @param j Index of second element
 * @param asc Is ascending
 */
void swap(int* arr, int i, int j, bool asc);

/**
 * @brief Fetches assigned work from the work_array
 * @param id worker thread's application id
 * @param work the work details to be fetched
 */
void fetchWork(int id, struct SorterWork* work);

/**
 * @brief Request work from the distributor
 * @param id application defined worker thread id
 */
void requestWork(int id);

/** @brief Signal that the sorting work has been finished. To be done by the worker threads */
void reportWork();

/**
 * @brief Checks which workers have requested work and assigns it to them
 * @param stage which stage of the sort it is
 * @param work_assigned number of workers with work already assigned
 * @return Number of work assigned
 */
int distributeWork(int stage, int work_assigned);

/**
 * @brief Validate that the integer array is sorted
 * @param val the array of integers to validate
 * @param N the size of the integer array
 */
void validate(int* val, int N);

/** @brief Array of work details for each of the sorter threads. Indexed by the threads' application id
 * (which should be an auto-incrementing counter) */
struct SorterWork* work_array;
/** @brief Array of work requests for each of the sorter threads. Indexed by the threads' application id
 * (which should be an auto-incrementing counter) */
bool* request_array;

// State Control variables
/** @brief Workers' synchronization point for new work to be assigned */
static pthread_cond_t await_work_assignment;
/** @brief Distributor's synchronization point for when the workers request work */
static pthread_cond_t await_work_request;
/** @brief Distributor's synchronization point for when the workers have finished sorting at this stage */
static pthread_cond_t workers_finished;

/** \brief Locking flag which warrants mutual exclusion inside the monitor */
static pthread_mutex_t accessCR = PTHREAD_MUTEX_INITIALIZER;

// Global state variables
/** @brief Number of threads to be run in the program. Can be changed with command-line arguments, 
 * and it's global as the distributor thread needs to be aware of how many there are */
static int n_threads = 4;
/** @brief Array holding the exit status of the worker threads */
static int* status_workers;

// Shared memory variables
/** @brief Array of integers to sort */
int* numbers;
/** @brief Size of numbers array */
int numbers_size;
/** @brief Name of the file with the array to sort */
char *filename; 
/** @brief Counter of the number of workers that finished the current sorting stage, so that the distributor can wait for all workers before continuing */
int n_of_work_finished = 0;

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

    filename = argv[optind];

    pthread_t* t_worker_id;         // workers internal thread id array
    pthread_t t_distributor_id;     // distributor internal thread id
    unsigned int* worker_id;        // workers application thread id array
    unsigned int distributor_id;    // distributor application thread id
    int* pStatus;                   // pointer to execution status
    int i;                          // counting variable

    /* initializing the application defined thread id arrays for the producers and the consumers and the random number
     generator */

    if ((t_worker_id = malloc(n_threads * sizeof(pthread_t))) == NULL
            || (worker_id = malloc(n_threads * sizeof(unsigned int))) == NULL
            || (status_workers = malloc(n_threads * sizeof(unsigned int))) == NULL
            || (work_array = calloc(n_threads, sizeof(struct SorterWork))) == NULL
            || (request_array = calloc(n_threads, sizeof(bool))) == NULL) {
        fprintf(stderr, "error on allocating space to both internal / external worker id arrays\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_threads; i++)
        worker_id[i] = i;

    pthread_cond_init(&await_work_assignment, NULL);
    pthread_cond_init(&await_work_request, NULL);
    pthread_cond_init(&workers_finished, NULL);

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

    validate(numbers, numbers_size);

    return EXIT_SUCCESS;
}


void *distributor(void *par) {
    unsigned int id = *((unsigned int *) par);
    int n_workers = n_threads;

    pthread_mutex_lock(&accessCR);
    readStoreFile(filename);
    pthread_mutex_unlock(&accessCR);

    for (int stage = n_threads; stage > 0; stage >>= 1) {

        pthread_mutex_lock(&accessCR);

        // The first iteration is done differently, since no workers are dispensed from work.
        // In the following iterations, however, half of the workers will be dispensed from work,
        // while the other half works as expected.
        n_workers = (stage == n_threads) ? stage : stage << 1;
        int n_of_work_requested = 0;
        n_of_work_requested = distributeWork(stage, n_of_work_requested);
        // TODO error handling
        pthread_cond_broadcast(&await_work_assignment);
        while (n_of_work_requested < n_workers) {
            pthread_cond_wait(&await_work_request, &accessCR);
            n_of_work_requested += distributeWork(stage, n_of_work_requested);
            // TODO: C pthread uses Lampson & Redell right? Therefore we can't assume that after a signal the waiting
            // thread will execute immediately, so we have to broadcast instead of 1 signal here
            pthread_cond_broadcast(&await_work_assignment);
        }

        while (n_of_work_finished < n_workers) {
            pthread_cond_wait(&workers_finished, &accessCR);
        }
        // Reset counter for the next iteration
        n_of_work_finished = 0;

        pthread_mutex_unlock(&accessCR);
    }
    status_workers[id] = EXIT_SUCCESS;
    pthread_exit(&status_workers[id]);
}

// TODO: proper error handling for pthread functions
void *worker(void *par) {
    unsigned int id = *((unsigned int *) par);
    struct SorterWork work;
    
    while (true) {
        requestWork(id);
        fetchWork(id, &work);
        
        if (!work.should_work)
            break;

        if (work.skip_sort)
            bitonicMerge(work.array, work.array_size, work.ascending); 
        else
            bitonicSort(work.array, work.array_size, work.ascending);
        
        reportWork();
    }
    printf("Exiting %d\n", id);
    status_workers[id] = EXIT_SUCCESS;
    pthread_exit(&status_workers[id]);
}

int distributeWork(int stage, int n_of_work_requested) {
    int distributed_work = 0;
    for (int i = 0; i < n_threads; i++) {
        if (request_array[i]) {
            struct SorterWork* work_to_distribute = &work_array[i];
            
            int work_id = n_of_work_requested + distributed_work;
            work_to_distribute->should_work = work_id < stage;
            
            if (work_to_distribute->should_work) {
                int block_size = numbers_size / stage;
                work_to_distribute->array = numbers + work_id * block_size;
                work_to_distribute->array_size = block_size;
                work_to_distribute->ascending = (work_id % 2) == 0;
                work_to_distribute->skip_sort = stage != n_threads;
            }

            distributed_work++;
            request_array[i] = false;
        }
    }
    return distributed_work;
}

void requestWork(int id) {
    pthread_mutex_lock(&accessCR);
    request_array[id] = true;
    pthread_mutex_unlock(&accessCR);
    pthread_cond_signal(&await_work_request);
}

void reportWork() {
    pthread_mutex_lock(&accessCR);
    n_of_work_finished++;
    pthread_mutex_unlock(&accessCR);
    pthread_cond_signal(&workers_finished);
}

void fetchWork(int id, struct SorterWork* work) {
    pthread_mutex_lock(&accessCR);
    // If the request hasn't been fulfilled yet
    if (request_array[id]) {
        pthread_cond_wait(&await_work_assignment, &accessCR);
    }
    *work = work_array[id];
    pthread_mutex_unlock(&accessCR);
}

void readStoreFile(char *filename) {
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
        res = fread(&numbers, sizeof(int), numbers_size, file);
        if (feof(file)) {
            break;
        }
        else if (ferror(file)) {
            printf("Invalid file format\n");
            return;
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
            bitonicMerge(arr, N, asc);
            asc = !asc;
        }
    }
}

void validate(int* val, int N) {
    int i;
    for (i = 0; i < N - 1; i++)
        if (val[i] > val[i+1]) { 
            printf ("Error in position %d between element %d and %d\n",
            i, val[i], val[i+1]);
            break;
        }
    if (i == (N - 1))
        printf ("Everything is OK!\n");
}

void printUsage (char *cmdName) {
    fprintf(stderr, "\nSynopsis: %s [OPTIONS] FILE...\n"
           "  OPTIONS:\n"
           "  -h      --- print this help\n", cmdName);
}
