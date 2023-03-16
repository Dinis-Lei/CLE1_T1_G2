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
#define  MAX_CHUNK_SIZE  2

/**
 * @brief Struct to save the partial counters of a given file
 * @param partial_counters  Array of integers which count the number of words and number of each vowel
 * @param current_file_id   Id of the file that the array is counting
 */
struct PartialInfo {
    int **partial_counters;
    int current_file_id;
};

/**
 *  \brief Print command usage.
 *
 *  A message specifying how the program should be called is printed.
 *
 *  \param cmdName string with the name of the command
 */
static void printUsage (char *cmdName);

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
void updateCounters(unsigned int worker_id, struct PartialInfo *pInfo);

/**
 * @brief Process the recently read chunk, updating the word counts.
 * 
 * Performed by the worker threads.
 *
 * @param chunk             the chunk of text to calculate word counts
 * @param chunk_size        number of bytes to be read from the chunk
 * @param worker_counters   array of counters to be overwritten with the chunk's word counts
 */
void processText(unsigned char* chunk, int chunk_size, struct PartialInfo *pInfo);

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

/**
 * @brief Receives an utf-8 code and asserts if it is alphanumeric or '_'
 * @param symbol array of bytes corresponding to an utf-8 code
 * @return assertion
 */
bool isalphanum(unsigned char* code);

/**
 * @brief Checks if chunk of text is cutting off a word (or byte) and returns the number of bytes to rewind the file 
 * @param chunk array of bytes to be verified
 * @return number of bytes to rewind the file 
 */
int checkCutOff(unsigned char* chunk);

/**
 * @brief Checks if utf-8 code corresponds with an apostrofe, or variants
 * @param code array of bytes corresponding to an utf-8 code
 * @return assertion
 */
bool isapostrofe(unsigned char* code);

/**
 * @brief Checks what vowel does the code correspond to and gives the correct index to update the counter
 * @param code array of bytes corresponding to an utf-8 code
 * @return counter index of the vowel
 */
int whatVowel(unsigned char* code);

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
static int max_chunk_size = MAX_CHUNK_SIZE;
/** @brief Array holding the exit status of the worker threads */
static int* status_workers;

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
int file_id = 0;
/** @brief  */
FILE *file_ptr;

bool init_file = false;

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
        (status_workers = malloc(n_threads * sizeof(unsigned int))) == NULL  ||
        (file_names = malloc(n_files * sizeof(char*))) == NULL  ||
        (counters = (int **)malloc(n_files*sizeof(int*))) == NULL) {
        fprintf(stderr, "error on allocating space to both internal / external worker id arrays\n");
        exit(EXIT_FAILURE);
    }


    for (i = 0; i < n_threads; i++)
        worker_id[i] = i;

    for (i = 0; i < n_files; i++) {
        file_names[i] = argv[optind+i];
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
    free(status_workers);
    free(file_names);
    for (int i = 0; i < n_files; i++)
        free(counters[i]);
    free(counters);

    return EXIT_SUCCESS;
}

int start_working = 0;
void *worker(void *par) {
    unsigned int id = *((unsigned int *) par);

    struct PartialInfo pInfo;
    unsigned char *chunk;
    int chunk_size;
    
    if ((chunk = malloc(max_chunk_size*1024 * sizeof(char))) == NULL ||
        (pInfo.partial_counters = (int **) malloc(n_files * sizeof(int*))) == NULL) {
        perror("error on allocating space to both internal / external worker id arrays\n");
        status_workers[id] = EXIT_FAILURE;
        pthread_exit(&status_workers[id]);
    }

    for (int i = 0; i < n_files; i++) {
        if((pInfo.partial_counters[i] = (int *) calloc(N_VOWELS, sizeof(int))) == NULL) {
            perror("error on allocating space to both internal / external worker id arrays\n");
            status_workers[id] = EXIT_FAILURE;
            pthread_exit(&status_workers[id]);
        }
    }

    while (!work_done) {
        // Get chunks (critical region)
        if(pthread_mutex_lock(&accessCR)) {
            printf("ERROR\n");
            perror ("error on entering monitor(CF)");
            status_workers[id] = EXIT_FAILURE;
            pthread_exit(&status_workers[id]);   
        }
        
        chunk_size = readChunk(id, chunk, &pInfo);
    
        if(pthread_mutex_unlock(&accessCR)) {
            printf("ERROR\n");
            perror ("error on exiting monitor(CF)");
            status_workers[id] = EXIT_FAILURE;
            pthread_exit(&status_workers[id]);    
        }
        // Can be done in parallel
        processText(chunk, chunk_size, &pInfo);
    }
    // Update word counts (critical region)
    updateCounters(id, &pInfo);

    if(pthread_mutex_unlock(&accessCR)) {
        printf("ERROR\n");
        perror ("error on entering monitor(CF)");
        status_workers[id] = EXIT_FAILURE;
        pthread_exit(&status_workers[id]);    
    }

    free(chunk);
    for (int i = 0; i < n_files; i++)
        free(pInfo.partial_counters[i]);
    free(pInfo.partial_counters);

    status_workers[id] = EXIT_SUCCESS;
    pthread_exit(&status_workers[id]);
}

void processText(unsigned char* chunk, int chunk_size, struct PartialInfo *pInfo) {
    unsigned char code[4] = {0, 0, 0, 0};                               // Utf-8 code of a symbol
    int code_size = 0;                                                  // Size of the code
    bool is_word = false;                                               // Checks if it is currently parsing a word
    bool has_counted[6] = {false, false, false, false, false, false};   // Controls if given vowel has already been counted
    int byte_ptr = 0;                                                   // Current byte being read in the chunk
    while (byte_ptr < chunk_size) {    
        code_size = 0;
        // Byte is 1-byte Code    
        if (!(chunk[byte_ptr] & 0x80)) {
           code_size = 1;
        }
        // Byte is 1st byte of a 2-byte code
        else if ((chunk[byte_ptr] & 0xe0) == 0xc0) {
            code_size = 2;
        }
        // Byte is 1st byte of a 3-byte code
        else if ((chunk[byte_ptr] & 0xf0) == 0xe0) {
            code_size = 3;
        }
        // Byte is 1st byte of a 4-byte code
        else if ((chunk[byte_ptr] & 0xf8) == 0xf0) {
            code_size = 4;
        }
        else {
            fprintf(stderr, "Error on processing file chunk\n");
            return;
        }

        // Grab code
        for (int i = 0; i < code_size; i++) {
            code[i] = chunk[byte_ptr+i];
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

int readChunk(unsigned int worker_id, unsigned char* chunk, struct PartialInfo *pInfo) {
    if (!init_file) {
        file_ptr = fopen(file_names[file_id], "r");
        init_file = true;
    }
    
    // Check if file has ended, open a new file if not every file has been processed
    // TODO: we could consider file_done to be true at the very beginning, even if no file was actually done, and avoid using init_file
    if (file_done) {
        if (file_id == n_files) {
            work_done = true;
            return 0;
        }
        file_ptr = fopen(file_names[file_id], "r");
        file_done = false;
    }

    (*pInfo).current_file_id = file_id;
    int num = fread(chunk, sizeof(char), MAX_CHUNK_SIZE*1024, file_ptr);    

    if (feof(file_ptr)) {
        file_id++;
        fclose(file_ptr);
        file_done = true;
        return num;
    }

    int offset = checkCutOff(chunk);
    fseek(file_ptr, -offset, SEEK_CUR);

    return num - offset;
}

void updateCounters(unsigned int worker_id, struct PartialInfo *pInfo) {
    if (pthread_mutex_lock(&accessCR)) {
        printf("ERROR\n");
        perror ("error on entering monitor(CF)");
        status_workers[worker_id] = EXIT_FAILURE;
        pthread_exit (&status_workers[worker_id]);   
    }
    for (int file = 0; file < n_files; file++) {
        for (int i = 0; i < N_VOWELS; i++) {
            // Increment global counter
            counters[file][i] += pInfo->partial_counters[file][i];
        }
    }
    
    if(pthread_mutex_unlock(&accessCR)) {
        perror ("error on entering monitor(CF)");
        status_workers[worker_id] = EXIT_FAILURE;
        pthread_exit (&status_workers[worker_id]);    
    }
}

void printResults(char** file_names) {
    for (int i = 0; i < n_files; i++) {
        printf("File name: %s\n", file_names[i]);
        printf("Total number of words = %d\n", counters[i][0]);
        printf("N. of words with an\n");
        printf("      A\t    E\t    I\t    O\t    U\t    Y\n  ");
        for(int j = 1; j < N_VOWELS; j++) {
            printf("%5d\t", counters[i][j]);
        }
        printf("\n");
    }
}

static void printUsage (char *cmdName) {
    fprintf(stderr, "\nSynopsis: %s [OPTIONS] FILE...\n"
           "  OPTIONS:\n"
           "  -h      --- print this help\n", cmdName);
}

bool isalphanum(unsigned char* code) {
    
    return
        (code[0] >= 0x30     && code[0] <= 0x39)    || 
        (code[0] >= 0x41     && code[0] <= 0x5a)    ||
        (code[0] == 0x5f)                           ||
        (code[0] >= 0x61     && code[0] <= 0x7a)    ||
        (code[0] >= 0x61     && code[0] <= 0x7a)    ||
        (code[0] == 0xc3     && (
            (code[1] >= 0x80 && code[1] <= 0x83)    ||
            (code[1] >= 0x87 && code[1] <= 0x8a)    ||
            (code[1] >= 0x8c && code[1] <= 0x8d)    ||
            (code[1] >= 0x92 && code[1] <= 0x95)    ||
            (code[1] >= 0x99 && code[1] <= 0x9a)    ||
            (code[1] >= 0xa0 && code[1] <= 0xa3)    ||
            (code[1] >= 0xa7 && code[1] <= 0xaa)    ||
            (code[1] >= 0xac && code[1] <= 0xad)    ||
            (code[1] >= 0xb2 && code[1] <= 0xb5)    ||
            (code[1] >= 0xb9 && code[1] <= 0xba) 
        ));
}

int checkCutOff(unsigned char* chunk) {
    int chunk_ptr = MAX_CHUNK_SIZE*1024 - 1;
    int code_size = 0;
    unsigned char symbol[4] = {0,0,0,0};
    while (true) {
        
        // Last Byte is 1-byte Code
        if (!(chunk[chunk_ptr] & 0x80)) {
           code_size = 1;
        }
        // Last Byte is 1st byte of a 2-byte code
        else if ((chunk[chunk_ptr] & 0xe0) == 0xc0) {
            code_size = 2;
        }
        // Last Byte is 1st byte of a 3-byte code
        else if ((chunk[chunk_ptr] & 0xf0) == 0xe0) {
            code_size = 3;
        }
        // Last Byte is 1st byte of a 4-byte code
        else if ((chunk[chunk_ptr] & 0xf8) == 0xf0) {
            code_size = 4;
        }
        // Last Byte is the n-th byte of a 2 or more byte code
        else if((chunk[chunk_ptr] & 0xC0) == 0x80) {
            chunk_ptr--;
            continue;
        }
        else {
            fprintf(stderr, "Error on parsing file chunk\n");
            return -1;
        }

        // Not enough bytes to form a complete code
        if (MAX_CHUNK_SIZE*1024 - chunk_ptr < code_size) {
            chunk_ptr--;
            continue;
        }

        // Grab code
        for (int i = 0; i < code_size; i++) {
            symbol[i] = chunk[chunk_ptr+i];
        }

        // Check if its not alpha-numeric
        if (!isalphanum(symbol)) {
            return MAX_CHUNK_SIZE*1024 - chunk_ptr;
        }
        // Decrement chunk_ptr
        chunk_ptr--;
    }

    return 0;
}

bool isapostrofe(unsigned char* code) {
    return (code[0] == 0x27) || (code[0] == 0xe2 && code[1] == 0x80 && (code[2] == 0x98 || code[2] == 0x99));
}

int whatVowel(unsigned char* code) {
    if ( code[0] == 0x41 || code[0] == 0x61 || (code[0] == 0xc3 && ((code[1] >= 0x80 && code[1] <= 0x83) || (code[1] >= 0xa0 && code[1] <= 0xa3)))) {
        return 1;
    }
    if ( code[0] == 0x45 || code[0] == 0x65 || (code[0] == 0xc3 && ((code[1] >= 0x88 && code[1] <= 0x8a) || (code[1] >= 0xa8 && code[1] <= 0xaa)))) {
        return 2;
    }
    if ( code[0] == 0x49 || code[0] == 0x69 || (code[0] == 0xc3 && ((code[1] >= 0x8c && code[1] <= 0x8d) || (code[1] >= 0xac && code[1] <= 0xad)))) {
        return 3;
    }
    if ( code[0] == 0x4f || code[0] == 0x6f || (code[0] == 0xc3 && ((code[1] >= 0x92 && code[1] <= 0x95) || (code[1] >= 0xb2 && code[1] <= 0xb5)))) {
        return 4;
    }
    if ( code[0] == 0x55 || code[0] == 0x75 || (code[0] == 0xc3 && ((code[1] >= 0x99 && code[1] <= 0x9a) || (code[1] >= 0xb9 && code[1] <= 0xba)))) {
        return 5;
    }
    if ((code[0] == 0x59 || code[0] == 0x79)) {
        return 6;
    }

    return -1;
}