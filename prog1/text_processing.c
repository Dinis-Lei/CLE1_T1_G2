#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libgen.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#define  MAX_THREADS  10
#define  MAX_CHUNK_SIZE_MIN  4
#define  MAX_CHUNK_SIZE_MAX  8
#define  N_VOWELS  7

/**
 *  \brief Print command usage.
 *
 *  A message specifying how the program should be called is printed.
 *
 *  \param cmdName string with the name of the command
 */
static void print_usage (char *cmdName);

// TODO: main should wait for each file's results, or only wait at the end for everything?

/**
 * @brief Read a fixed-size chunk of text from the file being currently read.
 * 
 * Performed by the worker threads.
 * Reads the file_pointer in shared memory, and so should have exclusive access.
 * The actual amount of bytes read varies, since the file is only read until no word or UTF-8 character is cut.
 *
 * @param chunk address in memory where the chunk should be read to
 * @param chunk_size size of the allocated memory for the chunk (in bytes)
 * @return int the number of bytes actually read from the file
 */
int readChunk(char* chunk, int* chunk_size);

/**
 * @brief Update the word counts of the file being currently read.
 * 
 * Performed by the worker threads.
 * Writes to the counters in shared memory, and so should have exclusive access.
 *
 * @param counters array with the counter increments, in the same order as the shared counters
 */
void updateCounters(int* counters);

/**
 * @brief Process the recently read chunk, updating the word counts.
 * 
 * Performed by the worker threads.
 *
 * @param counters array of counters to be overwritten with the chunk's word counts
 */
void processText(char* chunk, int chunk_size, int* counters);

/** @brief Print the formatted word count results to standard output */
void printResults();

/** @brief Worker threads' function, which will concurrently update the word counters for the file in file_pointer */
void *worker(void *id);

// Global state variables
/** @brief True when the main thread knows there are no more files to be read. Worker threads exit when this value becomes true */
static bool work_done = false;
/** @brief True when the main thread knows the current file has been completely read. Worker threads wait when this value becomes true */
static bool file_done = false;
/** @brief Number of threads to be run in the program. Can be changed with command-line arguments, and it's global as the worker threads need to be aware of how many there are */
static int n_threads = 4;
/** @brief Size of the chunks to be read by the workers, in kilobytes. Can be changed with command-line arguments */
static int max_chunk_size = 4;
/** @brief Array holding the exit status of the worker threads */
static int* statusWorkers;

// State Control variables
/** @brief Workers synchronization point for new work to be assigned */
static pthread_cond_t await_work;
/** @brief Workers synchronization point for new work to be assigned */
static pthread_cond_t await_results;
/** \brief Locking flag which warrants mutual exclusion inside the monitor */
static pthread_mutex_t accessCR = PTHREAD_MUTEX_INITIALIZER;

// Shared Region Variables
/** @brief Array holding the word counts: [words, words with A, words with E, words with I, words with O, words with U, words with Y] */
int counters[N_VOWELS];
/** @brief Pointer representing the file currently being read */
FILE *file_pointer;
/** @brief Counter of the number of threads that have finished processing the file chunks and updating the word counters */
int finished_processing;

int main (int argc, char *argv[]) {

    int opt;
    extern char* optarg;
    extern int optind;

    while((opt = getopt(argc, argv, "t:s:h")) != -1) {
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
            case 's':
                if (atoi(optarg) <= 0){ 
                    fprintf(stderr, "%s: non positive number\n", basename(argv[0]));
                    print_usage(basename(argv[0]));
                    return EXIT_FAILURE;
                }
                max_chunk_size = (int) atoi(optarg);
                if (max_chunk_size > MAX_CHUNK_SIZE_MAX){ 
                    fprintf(stderr, "%s: chunk size is too large\n", basename(argv[0]));
                    print_usage(basename(argv[0]));
                    return EXIT_FAILURE;
                }
                if (max_chunk_size < MAX_CHUNK_SIZE_MIN){ 
                    fprintf(stderr, "%s: chunk size is too small\n", basename(argv[0]));
                    print_usage(basename(argv[0]));
                    return EXIT_FAILURE;
                }
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
        return EXIT_FAILURE;
    }

    pthread_t* t_worker_id;         // workers internal thread id array
    unsigned int* worker_id;        // workers application thread id array
    int* pStatus;                   // pointer to execution status
    int i;                          // counting variable

    /* initializing the application defined thread id arrays for the producers and the consumers and the random number
     generator */

    if ((t_worker_id = malloc(n_threads * sizeof(pthread_t))) == NULL
            || (worker_id = malloc(n_threads * sizeof(unsigned int))) == NULL
            || (statusWorkers = malloc(n_threads * sizeof(unsigned int))) == NULL) {
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

    pthread_cond_init(&await_work, NULL);
    pthread_cond_init(&await_results, NULL);

    for (; optind < argc; optind++) {
        char* file_name = argv[optind];
		printf("File name: %s\n", file_name);
        
        // Assign Work
        if (pthread_mutex_lock (&accessCR) != 0) {                  // Enter Monitor
            fprintf(stderr, "error on entering monitor");
            exit(EXIT_FAILURE);
        }
        file_pointer = fopen(file_name, "r");                       // Next File to process
        for (i = 0; i < N_VOWELS; i++)
            counters[i] = 0;
        
        if (pthread_cond_broadcast(&await_work) != 0) {             // Signal workers to start processing file                                            
            fprintf(stderr, "error on signaling await work");
            exit(EXIT_FAILURE);
        }
        if (pthread_mutex_unlock (&accessCR) != 0) {                // Exit Monitor
            fprintf(stderr, "error on exiting monitor");
            exit(EXIT_FAILURE);
        }


        // Wait for Results
        if (pthread_mutex_lock (&accessCR) != 0) {                  // Enter Monitor
            fprintf(stderr, "error on entering monitor");
            exit(EXIT_FAILURE);
        }
        if (pthread_cond_wait(&await_results, &accessCR) != 0) {    // Wait for workers to finish file processing
            fprintf(stderr, "error on waiting for signal await results");
            exit(EXIT_FAILURE);
        }
        printResults();                                             // Print Results

        if (pthread_mutex_unlock (&accessCR) != 0) {                // Exit Monitor
            fprintf(stderr, "error on exiting monitor");
            exit(EXIT_FAILURE);
        }

    }

    if (pthread_mutex_lock (&accessCR) != 0) {                  // Enter Monitor
        fprintf(stderr, "error on entering monitor");
        exit(EXIT_FAILURE);
    }
    work_done = true;
    if (pthread_cond_broadcast(&await_work) != 0) {             // Signal workers to start processing file                                            
        fprintf(stderr, "error on signaling await work");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_unlock (&accessCR) != 0) {                // Exit Monitor
        fprintf(stderr, "error on exiting monitor");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_threads; i++) {
        if (pthread_join(t_worker_id[i], (void *)&pStatus) != 0) {
            fprintf(stderr, "error on waiting for thread worker");
            exit(EXIT_FAILURE);
        }
    }
    free(t_worker_id);
    free(worker_id);

    return EXIT_SUCCESS;
}


void *worker(void *par) {
    unsigned int id = *((unsigned int *) par);
    printf("Init worker %d\n", id);

    int worker_counters[N_VOWELS] = {0,0,0,0,0,0,0};              // Internal Counters
    char *chunk;
    int chunk_size;
    
    if ((chunk = malloc(chunk_size*1024 * sizeof(char))) == NULL ) {
        perror("error on allocating space to both internal / external worker id arrays\n");
        statusWorkers[id] = EXIT_FAILURE;
        pthread_exit(&statusWorkers[id]);
    }

    while (!work_done) {

        while (!file_done) {
            // Get chunks (critical region)
            if (pthread_mutex_lock(&accessCR) != 0) {              // Enter Monitor
                perror("error on entering monitor");
                statusWorkers[id] = EXIT_FAILURE;
                pthread_exit(&statusWorkers[id]);
            }
            readChunk(chunk, &chunk_size);
            file_done = chunk_size == 0;
            if (pthread_mutex_unlock(&accessCR) != 0) {            // Exit Monitor
                perror("error on exit monitor");
                statusWorkers[id] = EXIT_FAILURE;
                pthread_exit(&statusWorkers[id]);
            }
            // Can be done in parallel
            processText(chunk, chunk_size, worker_counters);
        }
        
        if (pthread_mutex_lock(&accessCR) != 0) {                  // Enter Monitor
            perror("error on entering monitor");
            statusWorkers[id] = EXIT_FAILURE;
            pthread_exit(&statusWorkers[id]);
        }
        updateCounters(worker_counters);
        finished_processing++;

        if (finished_processing == n_threads) {
            if (pthread_cond_signal(&await_results) != 0) {        // Signal main to advance to next file, or terminate                                            
                perror("error on signaling await results");
                statusWorkers[id] = EXIT_FAILURE;
                pthread_exit(&statusWorkers[id]);
            }
            finished_processing = 0;
        }

        if (pthread_mutex_unlock(&accessCR) != 0) {                // Exit Monitor
            perror("error on exit monitor");
            statusWorkers[id] = EXIT_FAILURE;
            pthread_exit(&statusWorkers[id]);
        }

        // Wait for main's next signal to work
        if (pthread_mutex_lock(&accessCR) != 0) {                  // Enter Monitor
            perror("error on entering monitor");
            statusWorkers[id] = EXIT_FAILURE;
            pthread_exit(&statusWorkers[id]);
        }
        if ((pthread_cond_wait(&await_work, &accessCR)) != 0) { 
            perror ("error on waiting for work to do");
            statusWorkers[id] = EXIT_FAILURE;
            pthread_exit(&statusWorkers[id]);
        }
        if (pthread_mutex_unlock(&accessCR) != 0) {                // Exit Monitor
            perror("error on exit monitor");
            statusWorkers[id] = EXIT_FAILURE;
            pthread_exit(&statusWorkers[id]);
        }
    }

    free(chunk);

    statusWorkers[id] = EXIT_SUCCESS;
    pthread_exit(&statusWorkers[id]);
}

void processText(char* chunk, int chunk_size, int* worker_counters) {
    printf("Process Text\n");
    for (int i = 0; i < N_VOWELS; i++)
        worker_counters[i] = 1;
}

int readChunk(char* chunk, int* chunk_size) {
    printf("Read Chunk\n");
    *chunk_size = 0;
    return 1;
}

void updateCounters(int* worker_counters) {
    printf("Update Counters\n");
    for (int i = 0; i < N_VOWELS; i++) {
        counters[i] += worker_counters[i];
        worker_counters[i] = 0;
    }
}

void printResults() {
    printf("Total number of words = %d\n", counters[0]);
    printf("N. of words with an\n");
    printf("      A\t    E\t    I\t    O\t    U\t    Y\n  ");
    for (int i = 1; i < 7; i++) {
        printf("%5d\t", counters[i]);
    }
    printf("\n");
}

static void print_usage (char *cmdName) {
  fprintf (stderr, "\nSynopsis: %s [OPTIONS] FILE...\n"
           "  OPTIONS:\n"
           "  -h      --- print this help\n", cmdName);
}