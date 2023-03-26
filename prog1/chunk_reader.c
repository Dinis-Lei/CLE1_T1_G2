/**
 * @file chunk_reader.c (implementation file)
 * 
 * @author Dinis Lei (you@domain.com), Martinho Tavares (martinho.tavares@ua.pt)
 * 
 * @brief Monitor for mutual exclusion in Program 1.
 * 
 * Implements a Lampson/Redell monitor to allow concurrent word counting on a
 * list of files that are sequentially processed.
 * 
 * Defines the following procedures for the main thread:
 * \li storeFiles
 * \li printResults
 * \li monitorFreeMemory
 * Defines the following procedures for the workers:
 * \li readChunk
 * \li updateCounters
 * 
 * @date March 2023
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>

#include "constants.h"
#include "utf8_parser.h"
#include "chunk_reader.h"

// External global variables
extern int* status_workers;
extern int status_main;
extern int n_files;

/**
 * @brief Checks if chunk of text is cutting off a word (or byte) and returns the number of bytes to rewind the file 
 * @param chunk array of bytes to be verified
 * @return number of bytes to rewind the file 
 */
static int checkCutOff(unsigned char* chunk);

// State Control variables
/** \brief Locking flag which warrants mutual exclusion inside the monitor */
static pthread_mutex_t accessCR = PTHREAD_MUTEX_INITIALIZER;

// Shared region variables
/** @brief Matrix holding the word counts: [words, words with A, words with E, words with I, words with O, words with U, words with Y] */
static int** counters;
/** @brief Array of file names to be processed */
static char** file_names;
/** @brief Index of the current file being processed */
static int file_id = 0;
/** @brief Pointer to the current file being processed */
static FILE* file_ptr;
/** @brief Flag that tells whether the next file can be loaded when reading a chunk */
static bool file_done = true;
/** @brief Flag that guarantees the monitor is initialized once and only once */
static pthread_once_t init = PTHREAD_ONCE_INIT;


static void monitorInitialize() {
    if ((file_names = malloc(n_files * sizeof(char*))) == NULL  ||
        (counters = (int **)malloc(n_files*sizeof(int*))) == NULL) {
        fprintf(stderr, "error on allocating space to both internal / external worker id arrays\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < n_files; i++) {
        if ((counters[i] = calloc(N_VOWELS, sizeof(int))) == NULL) {
            fprintf(stderr, "Error on allocating space to both internal / external worker id arrays\n");
            exit(EXIT_FAILURE);
        }
    }

}

void monitorFreeMemory() {
    if (pthread_mutex_lock(&accessCR)) {
        printf("ERROR\n");
        perror("error on entering monitor(CF)");
        status_main = EXIT_FAILURE;
        pthread_exit(&status_main);
    }

    free(file_names);
    for (int i = 0; i < n_files; i++)
        free(counters[i]);
    free(counters);

    if (pthread_mutex_unlock(&accessCR)) {
        printf("ERROR\n");
        perror("error on exiting monitor(CF)");
        status_main = EXIT_FAILURE;
        pthread_exit(&status_main);
    }
}

void storeFiles(char** file_names_in) {
    if (pthread_mutex_lock(&accessCR)) {
        printf("ERROR\n");
        perror("error on entering monitor(CF)");
        status_main = EXIT_FAILURE;
        pthread_exit(&status_main);   
    }
    pthread_once(&init, monitorInitialize);

    for (int i = 0; i < n_files; i++)
        file_names[i] = file_names_in[i];

    if (pthread_mutex_unlock(&accessCR)) {
        printf("ERROR\n");
        perror("error on exiting monitor(CF)");
        status_main = EXIT_FAILURE;
        pthread_exit(&status_main);   
    }
}

int readChunk(unsigned int worker_id, unsigned char* chunk, struct PartialInfo *pInfo) {    
    if (pthread_mutex_lock(&accessCR)) {
        perror ("error on entering monitor(CF)");
        status_workers[worker_id] = EXIT_FAILURE;
        pthread_exit(&status_workers[worker_id]);   
    }
    pthread_once(&init, monitorInitialize);
    
    int chunk_size;
    bool no_more_work = false;
    
    // Check if file has ended, open a new file if there are files to be processed
    if (file_done) {
        if (file_id == n_files) {
            chunk_size = 0;
            no_more_work = true;
        }
        else {
            file_ptr = fopen(file_names[file_id], "r");
            file_done = false;
        }
    }

    if (!no_more_work) {
        pInfo->current_file_id = file_id;
        chunk_size = fread(chunk, sizeof(char), MAX_CHUNK_SIZE*1024, file_ptr);    

        if (feof(file_ptr)) {
            file_id++;
            fclose(file_ptr);
            file_done = true;
        }
        else {
            int offset = checkCutOff(chunk);
            fseek(file_ptr, -offset, SEEK_CUR);
            chunk_size -= offset;
        }
    }

    if (pthread_mutex_unlock(&accessCR)) {
        perror("error on exiting monitor(CF)");
        status_workers[worker_id] = EXIT_FAILURE;
        pthread_exit(&status_workers[worker_id]);    
    }

    return chunk_size;
}


void updateCounters(unsigned int worker_id, struct PartialInfo *pInfo) {
    if (pthread_mutex_lock(&accessCR)) {
        printf("ERROR\n");
        perror("error on entering monitor(CF)");
        status_workers[worker_id] = EXIT_FAILURE;
        pthread_exit (&status_workers[worker_id]);   
    }
    pthread_once(&init, monitorInitialize);

    for (int file = 0; file < n_files; file++) {
        for (int i = 0; i < N_VOWELS; i++) {
            // Increment global counter
            counters[file][i] += pInfo->partial_counters[file][i];
        }
    }
    
    if (pthread_mutex_unlock(&accessCR)) {
        perror("error on entering monitor(CF)");
        status_workers[worker_id] = EXIT_FAILURE;
        pthread_exit (&status_workers[worker_id]);    
    }
}

void printResults() {
    if (pthread_mutex_lock(&accessCR)) {
        printf("ERROR\n");
        perror("error on entering monitor(CF)");
        status_main = EXIT_FAILURE;
        pthread_exit(&status_main);   
    }
    pthread_once(&init, monitorInitialize);

    for (int i = 0; i < n_files; i++) {
        printf("File name: %s\n", file_names[i]);
        printf("Total number of words = %d\n", counters[i][0]);
        printf("N. of words with an\n");
        printf("      A\t    E\t    I\t    O\t    U\t    Y\n  ");
        for(int j = 1; j < N_VOWELS; j++) {
            printf("%5d\t", counters[i][j]);
        }
        printf("\n\n");
    }

    if (pthread_mutex_unlock(&accessCR)) {
        perror("error on entering monitor(CF)");
        status_main = EXIT_FAILURE;
        pthread_exit(&status_main);    
    }
}

static int checkCutOff(unsigned char* chunk) {
    int chunk_ptr = MAX_CHUNK_SIZE*1024 - 1;
    int code_size = 0;
    unsigned char symbol[4] = {0,0,0,0};
    while (true) {
        
        readUTF8Character(chunk, symbol, &code_size);
        // Last Byte is the n-th byte of a 2 or more byte code
        if(code_size == 0 && (chunk[chunk_ptr] & 0xC0) == 0x80) {
            chunk_ptr--;
            continue;
        }
        else if (code_size == 0) {
            fprintf(stderr, "Error on parsing file chunk\n");
            exit(EXIT_FAILURE);
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
        if (!isalphanum(symbol) && !isapostrofe(symbol)) {
            return MAX_CHUNK_SIZE*1024 - chunk_ptr;
        }
        // Decrement chunk_ptr
        chunk_ptr--;
    }

    return 0;
}