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
#define  MAX_CHUNK_SIZE  8

/**
 * @brief Struct to save the partial counters of a given file
 * @param partial_counters  Array of integers which count the number of words and number of each vowel
 * @param current_file_id   Id of the file that the array is counting
 */
struct PartialInfo {
    int partial_counters[N_VOWELS];
    int current_file_id;
};

/**
 *  \brief Print command usage.
 *
 *  A message specifying how the program should be called is printed.
 *
 *  \param cmdName string with the name of the command
 */
static void print_usage (char *cmdName);

// TODO: main should wait for each file's results, or only wait at the end for everything?
// TODO: can we simplify and create/terminate threads at each processed file?

/**
 * @brief Read a fixed-size chunk of text from the file being currently read.
 * 
 * Performed by the worker threads.
 * The maximum number of bytes read is defined in the macro MAX_CHUNK_SIZE.
 * Reads the file_pointer in shared memory, and so should have exclusive access.
 * The actual amount of bytes read varies, since the file is only read until no word or UTF-8 character is cut.
 * 
 * @param worker_id application-defined ID of the calling worker thread
 * @param chunk     address in memory where the chunk should be read to
 * @param pInfo     Struct that contains the application-defined ID of the file that the chunk is from.
 * This value is overwritten, and should merely be passed to the updateCounters() function in the future
 * @return the number of bytes actually read from the file
 */
int readChunk(unsigned int worker_id, unsigned char* chunk, struct PartialInfo *pInfo);

/**
 * @brief Update the word counts of the file being currently read.
 * 
 * Performed by the worker threads.
 * Writes to the counters in shared memory, and so should have exclusive access.
 *
 * @param worker_id         application-defined ID of the calling worker thread
 * @param worker_counters   array with the counter increments, in the same order as the shared counters
 * @param pInfo             Struct that contains the application-defined ID of the file that the chunk is from 
 *                          and array with the counter increments, in the same order as the shared counters
 */
void updateCounters(unsigned int worker_id, struct PartialInfo pInfo);

/**
 * @brief Process the recently read chunk, updating the word counts.
 * 
 * Performed by the worker threads.
 *
 * @param chunk             the chunk of text to calculate word counts
 * @param chunk_size        number of bytes to be read from the chunk
 * @param worker_counters   array of counters to be overwritten with the chunk's word counts
 */
void processText(char* chunk, int chunk_size, struct PartialInfo *pInfo);

/**
 * @brief Print the formatted word count results to standard output
 * @param file_names Array with the name of the files to show results
 */
void printResults(char** file_names);

/** @brief Worker threads' function, which will concurrently update the word counters for the file in file_pointer */
void *worker(void *id);

/**
 * @brief Reads files from argv and stores them
 * @param argv 
 */
void storeFiles(char** file_list);

// Global state variables
/** @brief True when the main thread knows there are no more files to be read. Worker threads exit when this value becomes true */
static bool work_done = false;
/** @brief True when the main thread knows the current file has been completely read. Worker threads wait when this value becomes true */
static bool file_done = false;
/** @brief Number of files to be processed */
static int n_files;
/** @brief Number of threads to be run in the program. Can be changed with command-line arguments, and it's global 
 * as the worker threads need to be aware of how many there are 
 */
static int n_threads = 4;
/** @brief Size of the chunks to be read by the workers, in kilobytes. Can be changed with command-line arguments */
static int max_chunk_size = 4;
/** @brief Array holding the exit status of the worker threads */
static int* statusWorkers;

// State Control variables
/** \brief Locking flag which warrants mutual exclusion inside the monitor */
static pthread_mutex_t accessCR = PTHREAD_MUTEX_INITIALIZER;

// Shared Region Variables
/** @brief Matrix holding the word counts: [words, words with A, words with E, words with I, words with O, words with U, words with Y] */
int **counters;
/** @brief Array of file names to be processed */
char** file_names;
/** @brief Counter of the number of threads that have finished processing the file chunks and updating the word counters */
int finished_processing = 0;
/** @brief  Index of the current file being processed */
int file_id;

/**
 *  \brief Main thread.
 *
 *  Its role is starting the processing by generating the workers threads and redistribute work for them.
 *
 *  \param argc number of words of the command line
 *  \param argv list of words of the command line
 *
 *  \return status of operation
 */
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

    n_files = argc - optind;

    /* Initializing the internal and application-defined thread id arrays, thread status array and word counters */

    if ((t_worker_id = malloc(n_threads * sizeof(pthread_t))) == NULL       ||
        (worker_id = malloc(n_threads * sizeof(unsigned int))) == NULL      ||
        (statusWorkers = malloc(n_threads * sizeof(unsigned int))) == NULL  ||
        (file_names = malloc(n_files * sizeof(char*))) == NULL  ||
        (counters = (int **)malloc(n_files*sizeof(int*))) == NULL) {
        fprintf(stderr, "error on allocating space to both internal / external worker id arrays\n");
        exit(EXIT_FAILURE);
    }


    for (i = 0; i < n_threads; i++)
        worker_id[i] = i;

    for (i = 0; i < n_files; i++) {
        file_names[i] = argv[optind+i];
		printf("File name: %s\n", file_names[i]);
        if ((counters[i] = calloc(N_VOWELS, sizeof(int))) == NULL) {
            fprintf(stderr, "Error on allocating space to both internal / external worker id arrays\n");
            exit(EXIT_FAILURE);
        }
    }

    // Launch Workers
    for (i = 0; i < n_threads; i++) {
        if (pthread_create(&t_worker_id[i], NULL, worker, &worker_id[i]) != 0) {
            fprintf(stderr, "error on creating worker thread");
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < n_threads; i++) {
        if (pthread_join(t_worker_id[i], (void *)&pStatus) != 0) {
            fprintf(stderr, "error on waiting for thread worker");
            exit(EXIT_FAILURE);
        }
    }

    printResults(file_names);
    
    free(t_worker_id);
    free(worker_id);

    return EXIT_SUCCESS;
}

int start_working = 0;
void *worker(void *par) {
    unsigned int id = *((unsigned int *) par);
    printf("Init worker %d\n", id);

    struct PartialInfo pInfo;
    char *chunk;
    int chunk_size = 2;
    
    if ((chunk = malloc(max_chunk_size*1024 * sizeof(char))) == NULL) {
        perror("error on allocating space to both internal / external worker id arrays\n");
        statusWorkers[id] = EXIT_FAILURE;
        pthread_exit(&statusWorkers[id]);
    }

    while (!work_done) {
        // Get chunks (critical region)
        readChunk(id, chunk, &pInfo);
        // Can be done in parallel
        processText(chunk, chunk_size, &pInfo);
    }
    // Update word counts (critical region)
    updateCounters(id, pInfo);

    free(chunk);

    statusWorkers[id] = EXIT_SUCCESS;
    pthread_exit(&statusWorkers[id]);
}

void processText(char* chunk, int chunk_size, struct PartialInfo *pInfo) {
    printf("Process Text\n");
    for (int i = 0; i < N_VOWELS; i++) {
        (*pInfo).partial_counters[i]++;
        printf("%d ", (*pInfo).partial_counters[i]);
    }
    printf("\n");
    
}

int readChunk(unsigned int worker_id, unsigned char* chunk, struct PartialInfo *pInfo) {
    printf("Read Chunk %d\n", worker_id);
    if(pthread_mutex_lock(&accessCR)) {
        perror ("error on entering monitor(CF)");
        statusWorkers[worker_id] = EXIT_FAILURE;
        pthread_exit (&statusWorkers[worker_id]);   
    }
    (*pInfo).current_file_id = 1;
    work_done = true;
    if(pthread_mutex_unlock(&accessCR)) {
        perror ("error on entering monitor(CF)");
        statusWorkers[worker_id] = EXIT_FAILURE;
        pthread_exit (&statusWorkers[worker_id]);    
    }
    return 1;
}

void updateCounters(unsigned int worker_id, struct PartialInfo pInfo) {
    printf("Update Counters %d\n", worker_id);
    if(pthread_mutex_lock(&accessCR)) {
        perror ("error on entering monitor(CF)");
        statusWorkers[worker_id] = EXIT_FAILURE;
        pthread_exit (&statusWorkers[worker_id]);   
    }
    pInfo.current_file_id = 0;
    for (int i = 0; i < N_VOWELS; i++) {
        printf("%d ", pInfo.partial_counters[i]);
        counters[pInfo.current_file_id][i] += pInfo.partial_counters[i];
    }
    printf("\n");
    if(pthread_mutex_unlock(&accessCR)) {
        perror ("error on entering monitor(CF)");
        statusWorkers[worker_id] = EXIT_FAILURE;
        pthread_exit (&statusWorkers[worker_id]);    
    }
}

void printResults(char** file_names) {
    printf("Init print");
    for (int i = 0; i < n_files; i++) {
        printf("File name: %s", file_names[i]);
        printf("Total number of words = %d\n", counters[i][0]);
        printf("N. of words with an\n");
        printf("      A\t    E\t    I\t    O\t    U\t    Y\n  ");
        for(int j = 1; j < N_VOWELS; j++) {
            printf("%5d\t", counters[i][j]);
        }
        printf("\n");
    }
}

static void print_usage (char *cmdName) {
    fprintf(stderr, "\nSynopsis: %s [OPTIONS] FILE...\n"
           "  OPTIONS:\n"
           "  -h      --- print this help\n", cmdName);
}