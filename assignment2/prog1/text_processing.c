/**
 * @file text_processing.c (implementation file)
 * 
 * @author Dinis Lei (dinislei@ua.pt), Martinho Tavares (martinho.tavares@ua.pt)
 * 
 * @brief Main file for Program 1.
 * 
 * Count the overall number of words and number of words containing each
 * possible vowel in a list of files passed as argument.
 * Each file is partitioned into chunks, that are distributed by a dispatcher
 * among workers to perform the word counting in parallel, using multiprocessing.
 * 
 * @date May 2023
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libgen.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <mpi.h>

#include "constants.h"
#include "utf8_parser.h"
#include "text_processing.h"


/**
 * @brief Structure holding the current progress when reading chunks from a text file with readChunk().
 * 
 * The progress contains the index of the file currently being read, with respect to an external array
 * holding file names. If the index is greater than or equal to that array's size, then the chunk reading
 * is finished.
 * 
 * @param file_ptr pointer to the file currently being read
 * @param file_idx index of the current file being read in an array holding file names
 * @param file_done whether the previous file has already been read, and thus the file at file_idx should now be read
 */
struct ChunkReadingProgress {
    FILE* file_ptr;
    int file_idx;
    bool file_done;
};

/**
 *  @brief Print command usage.
 *
 *  A message specifying how the program should be called is printed.
 *
 *  @param cmdName string with the name of the command
 */
static void printUsage (char *cmdName);

/**
 * @brief Process the command line arguments, printing usage if needed.
 * 
 * @param argv array of argument values
 * @param argc number of arguments
 * @param rank rank of the calling process
 */
static void processComandLine(char** argv, int argc, int rank);

/**
 * @brief Process the recently read chunk, updating the word counts.
 * 
 * Performed by the worker processes in parallel.
 *
 * @param chunk the chunk of text to calculate word counts
 * @param chunk_size number of bytes to be read from the chunk
 * @param counters counters to be updated with the chunk's word counts
 * @param current_file_idx index of the file to which the chunk belongs
 */
static void processText(unsigned char* chunk, int chunk_size, int* counters, int current_file_idx);

/**
 * @brief Read a fixed-size chunk of text from the file being currently read.
 * 
 * Performed by the dispatcher.
 * The maximum number of bytes read is defined in the macro MAX_CHUNK_SIZE.
 * Reads the file_pointer in ChunkReadingProgress, and opens files specified in file_names.
 * The actual amount of bytes read varies, since the file is only read until no word or UTF-8 character is cut.
 * 
 * A chunk size of 0 is returned whenever there is an error.
 * 
 * @param file_names        array with the names of the files to process
 * @param n_files           number of files to process
 * @param reading_progress  (input/output) current progress of readChunk in file reading
 * @param chunk             (output) chunk of text that will be read
 * @param chunk_size        (output) the number of bytes actually read from the file
 * @return 0 if successful, 1 otherwise
 */
static int readChunk(char** file_names, int n_files, struct ChunkReadingProgress* reading_progress, unsigned char* chunk, int* chunk_size);

/**
 * @brief Check if chunk of text is cutting off a word (or byte) and return the number of bytes to rewind the file.
 * 
 * @param chunk chunk of text to be verified
 * @param rewind_offset (output) number of bytes to rewind the file 
 * @return 0 if successful, 1 otherwise
 */
static int checkCutOff(unsigned char* chunk, int* rewind_offset);

/** @brief Print the formatted word count results to standard output */
static void printResults();

/** @brief Execution time measurement */
static double get_delta_time(void);

/**
 * @brief Main function.
 *
 * The execution is branched depending on whether the dispatcher process (rank 0)
 * or the worker processes (rank > 0) are running the function.
 * At least 2 processes are required (1 dispatcher and 1 or more workers).
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

    // Explicitly set the MPI error handler
    MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_ARE_FATAL);

    if (size < 2) {
        if (rank == 0) {
            fprintf(stderr, "error, at least 2 processes are necessary (1 dispatcher + 1..N workers)");
        }
        MPI_Finalize();
        exit(EXIT_FAILURE);
    }

    extern int optind;
    processComandLine(argv, argc, rank);
    
    int n_files = argc - optind;
    char** file_names = &argv[optind];

    if (rank == 0) {
        (void) get_delta_time();
    }

    bool can_advance = true; // whether this process can advance with no errors. All processes quit if at least one process can't advance

    /** @brief Matrix holding the word counts for each file, with columns: [words, words with A, words with E, words with I, words with O, words with U, words with Y] */
    int* counters = malloc((n_files * N_VOWELS) * sizeof(int));
    if (counters == NULL) {
        fprintf(stderr, "[%d] error on allocating space for the matrix of partial word counters\n", rank);
        can_advance = false;
    }
    else {
        for (int i = 0; i < n_files * N_VOWELS; i++) {
            counters[i] = 0;
        }
    }

    /** @brief Final counters matrix, only significant for the root process. Result matrix to which all partial counters from the processes will be summed */
    int* counters_final = NULL;
    if (rank == 0) {
        if ((counters_final = malloc((n_files * N_VOWELS) * sizeof(int))) == NULL) {
            fprintf(stderr, "[%d] error on allocating space for the matrix of final word counters\n", rank);
            can_advance = false;
        }
    }

    unsigned char *chunk;
    if ((chunk = malloc(MAX_CHUNK_SIZE*1024 * sizeof(char))) == NULL) {
        fprintf(stderr, "[%d] error on allocating space for the text chunk\n", rank);
        can_advance = false;
    }

    // Allocation of request arrays, only significant at root.
    // They are allocated sooner here to make use of the can_advance procedure, instead of being checked later while the algorithm is already being executed (which complicates message passing)
    int* work_request_ranks = NULL;
    MPI_Request* work_requests = NULL;
    if (rank == 0) {
        if ((work_request_ranks = malloc((size - 1) * sizeof(int))) == NULL
                || (work_requests = malloc((size - 1) * sizeof(MPI_Request))) == NULL) {
            fprintf(stderr, "[%d] error on allocating space for the request data structures\n", rank);
            can_advance = false;
        }
    }

    // Check whether all processes can proceed with the algorithm with no errors
    bool can_all_advance;
    MPI_Allreduce(&can_advance, &can_all_advance, 1, MPI_C_BOOL, MPI_LAND, MPI_COMM_WORLD);

    if (!can_all_advance) {
        if (rank == 0) {
            fprintf(stderr, "error, can't proceed with the text processing due to bad memory allocation, quitting...\n");
        }
        MPI_Finalize();
        exit(EXIT_FAILURE);
    }

    // The text processing itself
    bool chunk_reading_failed = false; // check whether the chunk reading was successful, if not terminate other processes (only used by root)
    if (rank == 0) {
        int chunk_size;
        
        struct ChunkReadingProgress reading_progress = {
            .file_ptr = NULL,
            .file_idx = 0,
            .file_done = true
        };

        // Signal a non-blocking receive for every worker, to wait for requests in the future
        for (int worker_rank = 1; worker_rank < size; worker_rank++) {
            MPI_Irecv(&work_request_ranks[worker_rank - 1], 1, MPI_INT, worker_rank, 0, MPI_COMM_WORLD, &work_requests[worker_rank - 1]);
        }

        int request_idx;
        int request_rank;
        int chunk_file_idx;
        int terminated_communications = 0;
        while (terminated_communications < size - 1) {
            chunk_file_idx = reading_progress.file_idx; // the value of current_file_idx may change after readChunk(), and so the file_idx sent to the workers could be wrong if the file was switched
            // As soon as the chunk reading fails once, send termination messages to the other processes (messages with chunk_size=0)
            if (!chunk_reading_failed) {
                chunk_reading_failed = readChunk(file_names, n_files, &reading_progress, chunk, &chunk_size) != 0;
            }

            // Wait for any of the non-blocking requests by workers
            MPI_Waitany(size - 1, work_requests, &request_idx, MPI_STATUS_IGNORE);
            request_rank = work_request_ranks[request_idx];

            // This sends are blocking, otherwise we could not change the chunk before we are sure the recipient got it
            MPI_Send(&chunk_size, 1, MPI_INT, request_rank, 0, MPI_COMM_WORLD);
            if (chunk_size > 0) {
                MPI_Send(&chunk_file_idx, 1, MPI_INT, request_rank, 0, MPI_COMM_WORLD);
                MPI_Send(chunk, chunk_size, MPI_UNSIGNED_CHAR, request_rank, 0, MPI_COMM_WORLD);
                
                // Refresh the wait for the next request from this worker
                MPI_Irecv(&work_request_ranks[request_idx], 1, MPI_INT, request_rank, 0, MPI_COMM_WORLD, &work_requests[request_idx]);
            }
            else {
                terminated_communications++;
            }
        }
    }
    else {
        int chunk_size;
        int current_file_idx;

        MPI_Request sent_request;
        while (true) {
            // Send a non-blocking request to the dispatcher
            MPI_Isend(&rank, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &sent_request);

            // Wait for the response from the dispatcher. If the chunk will be empty, then assume the work is done
            MPI_Recv(&chunk_size, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (chunk_size == 0) {
                break;
            }

            MPI_Recv(&current_file_idx, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(chunk, chunk_size, MPI_UNSIGNED_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            processText(chunk, chunk_size, counters, current_file_idx); // UTF-8 parsing errors are not considered fatal
        }
    }

    MPI_Reduce(counters, counters_final, n_files * N_VOWELS, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    int return_status = EXIT_SUCCESS;
    if (rank == 0) {
        if (chunk_reading_failed) {
            fprintf(stderr, "error, could not properly read the source files\n");
            return_status = EXIT_FAILURE;
        }
        else {
            printResults(counters_final, file_names, n_files);
            printf("\nElapsed time = %.6f s\n", get_delta_time());
        }
    }

    free(counters);
    free(chunk);
    if (rank == 0) {
        free(counters_final);
        free(work_requests);
        free(work_request_ranks);
    }

    MPI_Finalize();
    exit(return_status);
}

static void processText(unsigned char* chunk, int chunk_size, int* counters, int current_file_idx) {
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
                counters[current_file_idx * N_VOWELS]++;
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
                counters[current_file_idx * N_VOWELS + vowel]++;
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

int readChunk(char** file_names, int n_files, struct ChunkReadingProgress* reading_progress, unsigned char* chunk, int* chunk_size) {    
    *chunk_size = 0;
    bool no_more_work = false;
    
    // Check if file has ended, open a new file if there are files to be processed
    if (reading_progress->file_done) {
        if (reading_progress->file_idx == n_files) {
            no_more_work = true;
        }
        else {
            reading_progress->file_ptr = fopen(file_names[reading_progress->file_idx], "r");
            if (reading_progress->file_ptr == NULL) {
                fprintf(stderr, "error opening file %s\n", file_names[reading_progress->file_idx]);
                return 1;
            }
            reading_progress->file_done = false;
        }
    }

    if (!no_more_work) {
        *chunk_size = fread(chunk, sizeof(char), MAX_CHUNK_SIZE*1024, reading_progress->file_ptr);    
        if (feof(reading_progress->file_ptr)) {
            reading_progress->file_idx++;
            fclose(reading_progress->file_ptr);
            reading_progress->file_done = true;
        }
        else if (ferror(reading_progress->file_ptr)) {
            fprintf(stderr, "error, invalid file format\n");
            return 1;
        }
        else {
            int offset;
            if (checkCutOff(chunk, &offset) != 0) {
                return 1;
            }
            fseek(reading_progress->file_ptr, -offset, SEEK_CUR);
            *chunk_size -= offset;
        }
    }

    return 0;
}

static int checkCutOff(unsigned char* chunk, int* rewind_offset) {
    int chunk_ptr = MAX_CHUNK_SIZE*1024 - 1;
    int code_size = 0;
    unsigned char symbol[4] = {0,0,0,0};
    
    *rewind_offset = 0;

    while (true) {
        
        readUTF8Character(&chunk[chunk_ptr], symbol, &code_size);
        // Last Byte is the n-th byte of a 2 or more byte code
        if (code_size == 0 && (chunk[chunk_ptr] & 0xC0) == 0x80) {
            chunk_ptr--;
            continue;
        }
        else if (code_size == 0) {
            perror("error on parsing file chunk\n");
            return 1;
        }

        // Not enough bytes to form a complete code
        if (MAX_CHUNK_SIZE*1024 - chunk_ptr < code_size) {
            chunk_ptr--;
            continue;
        }

        // Check if its not alpha-numeric
        if (!isalphanum(symbol) && !isapostrofe(symbol)) {
            *rewind_offset = MAX_CHUNK_SIZE*1024 - chunk_ptr;
            return 0;
        }

        chunk_ptr--;
    }

    return 0;
}

void printResults(int* counters_final, char** file_names, int n_files) {
    for (int i = 0; i < n_files; i++) {
        printf("File name: %s\n", file_names[i]);
        printf("Total number of words = %d\n", counters_final[i * N_VOWELS]);
        printf("N. of words with an\n");
        printf("      A\t    E\t    I\t    O\t    U\t    Y\n  ");
        for (int j = 1; j < N_VOWELS; j++) {
            printf("%5d\t", counters_final[i * N_VOWELS + j]);
        }
        printf("\n\n");
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
           "  -h      --- print this help\n", cmdName);
}

static void processComandLine(char** argv, int argc, int rank) {
    int opt;
    extern char* optarg;
    extern int optind;

    while ((opt = getopt(argc, argv, "t:h")) != -1) {
        switch (opt) {
            case 'h':
                if (rank == 0) {
                    printUsage(basename(argv[0]));
                }
                MPI_Finalize();
                exit(EXIT_SUCCESS);
            case '?': /* invalid option */
                if (rank == 0) {
                    fprintf(stderr, "%s: invalid option\n", basename(argv[0]));
                }
                MPI_Finalize();
                exit(EXIT_FAILURE);
        }
    }

    if (argc < 2) {
        if (rank == 0) {
            fprintf(stderr, "Input some files to process!\n");
        }
        MPI_Finalize();
        exit(EXIT_FAILURE);
    }
}
