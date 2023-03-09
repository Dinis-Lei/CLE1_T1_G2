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

static void print_usage (char *cmdName);

// TODO: main should wait for each file's results, or only wait at the end for everything?

// Global state variables
/** @brief True when the main thread knows there are no more files to be read. Worker threads exit when this value becomes true */
bool work_done = false;
/** @brief True when the main thread knows the current file has been completely read. Worker threads wait when this value becomes true */
bool file_done = false;

// Shared Region Variables
/** @brief Array holding the word counts: [words, words with A, words with E, words with I, words with O, words with U, words with Y] */
int* counters;
/** @brief Pointer representing the file currently being read */
FILE *file_pointer;

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
int readChunk(char* chunk, int chunk_size);

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
void processText(int* counters);

/** @brief Print the formatted word count results to standard output */
void printResults();

/** @brief Worker threads' function, which will concurrently update the word counters for the file in file_pointer */
void worker(void *id);

int main (int argc, char *argv[]) {

    int opt;
    int n_threads = 4;
    char* optarg;
    int optind;

    while((opt = getopt(argc, argv, "t:h")) != -1) {
        switch (opt) {
            case 't': /* number of threads to be created */
                if (atoi (optarg) <= 0){ 
                    fprintf (stderr, "%s: non positive number\n", basename (argv[0]));
                    printUsage (basename (argv[0]));
                    return EXIT_FAILURE;
                }
                n_threads = (int) atoi (optarg);
                if (n_threads > MAX_THREADS){ 
                    fprintf (stderr, "%s: too many threads\n", basename (argv[0]));
                    printUsage (basename (argv[0]));
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                print_usage(basename (argv[0]));
                return EXIT_SUCCESS;
            case '?': /* invalid option */
                fprintf (stderr, "%s: invalid option\n", basename (argv[0]));
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

    if ((t_worker_id = malloc(n_threads * sizeof(pthread_t))) == NULL) {
        fprintf(stderr, "error on allocating space to both internal / external producer / consumer id arrays\n");
        exit(EXIT_FAILURE);
    }

    for (; optind < argc; optind++) {
        char* file_name = argv[optind];
		printf("File name: %s\n", file_name);
        

        // Launch Workers
        for (i = 0; i < n_threads; i++) {
                if (pthread_create(&t_worker_id[i], NULL, worker, &worker_id[i]) != 0)
                {
                    perror("error on creating thread producer");
                    exit(EXIT_FAILURE);
                }
        }
        
        // Assign Work


        // Wait for Results

        printResults();
    }

    for (i = 0; i < n_threads; i++)
    {
        if (pthread_join(t_worker_id[i], (void *)&pStatus) != 0)
        {
                perror("error on waiting for thread worker");
                exit(EXIT_FAILURE);
        }
    }

    return EXIT_SUCCESS;
}


/**
 *  \brief Print command usage.
 *
 *  A message specifying how the program should be called is printed.
 *
 *  \param cmdName string with the name of the command
 */
static void print_usage (char *cmdName) {
  fprintf (stderr, "\nSynopsis: %s [OPTIONS] FILE...\n"
           "  OPTIONS:\n"
           "  -h      --- print this help\n", cmdName);
}