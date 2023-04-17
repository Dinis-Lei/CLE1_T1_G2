/**
 * @file text_processing.c (implementation file)
 * 
 * @author Dinis Lei (dinislei@ua.pt), Martinho Tavares (martinho.tavares@ua.pt)
 * 
 * @brief Main file for Program 1.
 * 
 * Count the overall number of words and number of words containing each
 * possible vowel in a list of files passed as argument.
 * Each file is partitioned into chunks, that are distributed among workers
 * to perform the word counting in parallel, using multithreading.
 * 
 * @date April 2023
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

#include "constants.h"
#include "utf8_parser.h"
#include "chunk_reader.h"
#include "text_processing.h"


// Global state variables
/** @brief Number of files to be processed */
int n_files;
/** @brief Exit status of the main thread when performing operations on the monitor */
int status_main;
/** @brief Array holding the exit status of the worker threads */
int* status_workers;

/**
 *  @brief Print command usage.
 *
 *  A message specifying how the program should be called is printed.
 *
 *  @param cmdName string with the name of the command
 */
static void printUsage (char *cmdName);

/**
 * @brief Process the recently read chunk, updating the word counts.
 * 
 * Performed by the worker threads in parallel.
 *
 * @param chunk the chunk of text to calculate word counts
 * @param chunk_size number of bytes to be read from the chunk
 * @param pInfo partial info containing the counters to be updated with the chunk's word counts
 */
static void processText(unsigned char* chunk, int chunk_size, struct PartialInfo *pInfo);

/** @brief Worker threads' function, which will concurrently update the word counters for the file in file_pointer */
static void *worker(void *id);

/** @brief Execution time measurement */
static double get_delta_time(void);

/** @brief Number of threads to be run in the program. Can be changed with command-line arguments */
static int n_threads = 4;

/**
 * @brief Main thread.
 *
 * Its role is storing the name of the files to process, and then launching the workers that will count words on them.
 * Afterwards, it waits for their termination, and prints the counting results in the end.
 *
 * @param argc number of words of the command line
 * @param argv list of words of the command line
 * @return status of operation
 */
int main (int argc, char *argv[]) {

    int rank, size;

    MPI_Init (&argc, &argv);
    MPI_Comm_rank (MPI_COMM_WORLD, &rank);
    MPI_Comm_size (MPI_COMM_WORLD, &size);

    n_files = argc - optind;

    pthread_t* t_worker_id;         // workers internal thread id array
    unsigned int* worker_id;        // workers application thread id array
    int* pStatus;                   // pointer to execution status
    int i;                          // counting variable

    /* Initializing the internal and application-defined thread id arrays, thread status array */
    if ((t_worker_id = malloc(n_threads * sizeof(pthread_t))) == NULL  ||
        (worker_id = malloc(n_threads * sizeof(unsigned int))) == NULL ||
        (status_workers = malloc(n_threads * sizeof(unsigned int))) == NULL) {
        fprintf(stderr, "error on allocating space for arrays of internal / application worker ids and worker status\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_threads; i++) {
        worker_id[i] = i;
    }

    storeFiles(&argv[optind]);

    (void) get_delta_time();

    // Launch Workers
    for (i = 0; i < n_threads; i++) {
        if (pthread_create(&t_worker_id[i], NULL, worker, &worker_id[i]) != 0) {
            fprintf(stderr, "error on creating worker thread");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for the workers to terminate
    for (i = 0; i < n_threads; i++) {
        if (pthread_join(t_worker_id[i], (void *)&pStatus) != 0) {
            fprintf(stderr, "error on waiting for thread worker");
            exit(EXIT_FAILURE);
        }
    }

    printResults();
    
    free(t_worker_id);
    free(worker_id);
    free(status_workers);
    monitorFreeMemory();

    printf ("\nElapsed time = %.6f s\n", get_delta_time ());
    return EXIT_SUCCESS;
}

static void *worker(void *par) {
    unsigned int id = *((unsigned int *) par);

    struct PartialInfo pInfo;
    unsigned char *chunk;
    int chunk_size;
    
    if ((chunk = malloc(MAX_CHUNK_SIZE*1024 * sizeof(char))) == NULL ||
        (pInfo.partial_counters = (int **) malloc(n_files * sizeof(int*))) == NULL) {
        fprintf(stderr, "error on allocating space for the worker text chunk and the rows of the partial counters matrix\n");
        status_workers[id] = EXIT_FAILURE;
        pthread_exit(&status_workers[id]);
    }

    for (int i = 0; i < n_files; i++) {
        if ((pInfo.partial_counters[i] = (int *) calloc(N_VOWELS, sizeof(int))) == NULL) {
            fprintf(stderr, "error on allocating space for the columns of row %d of the partial counters matrix\n", i);
            status_workers[id] = EXIT_FAILURE;
            pthread_exit(&status_workers[id]);
        }
    }

    while ((chunk_size = readChunk(id, chunk, &pInfo)) > 0) {
        processText(chunk, chunk_size, &pInfo);
    }
    
    // Update word counts
    updateCounters(id, &pInfo);

    free(chunk);
    for (int i = 0; i < n_files; i++)
        free(pInfo.partial_counters[i]);
    free(pInfo.partial_counters);

    status_workers[id] = EXIT_SUCCESS;
    pthread_exit(&status_workers[id]);
}

static void processText(unsigned char* chunk, int chunk_size, struct PartialInfo *pInfo) {
    unsigned char code[4] = {0, 0, 0, 0};                               // UTF-8 code of a symbol
    int code_size = 0;                                                  // Size of the code
    bool is_word = false;                                               // Checks if it is currently parsing a word
    bool has_counted[6] = {false, false, false, false, false, false};   // Controls if given vowel has already been counted
    int byte_ptr = 0;                                                   // Current byte being read in the chunk

    while (byte_ptr < chunk_size) {    
        readUTF8Character(&chunk[byte_ptr], code, &code_size);

        if (code_size == 0){
            fprintf(stderr, "error on processing file chunk, could not parse UTF-8 character\n");
            return;
        }

        // Increment pointer
        byte_ptr += code_size;

        if (isalphanum(code)) {
            // If previous state wasn't word, increment word counter
            if (!is_word) {
                pInfo->partial_counters[pInfo->current_file_id][0]++;
            }
            is_word = true;

            // Check if code corresponds to a vowel
            int vowel = whatVowel(code);

            if (vowel < 0) {
                continue;
            }

            // Increment vowel counter if it hasn't been counted
            if (!has_counted[vowel-1]) {
                has_counted[vowel-1] = true;
                pInfo->partial_counters[pInfo->current_file_id][vowel]++;
            }
        }
        else {
            if (!isapostrofe(code)) {
                is_word = false;
                for (int i = 0; i < N_VOWELS-1; i++) {
                    has_counted[i] = false;
                }
            }
        }
    }
    
}

static double get_delta_time(void) {
    static struct timespec t0, t1;

    t0 = t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) {
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

static void processComandLine(char* argv, int argc) {
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
                exit(EXIT_SUCCESS);
            case '?': /* invalid option */
                fprintf (stderr, "%s: invalid option\n", basename(argv[0]));
                exit(EXIT_FAILURE);
        }
    }

    if (argc < 2) {
        fprintf(stderr, "Input some files to process!\n");
        exit(EXIT_FAILURE);
    }
}
