/**
 * @file sort_array.c (implementation file)
 *
 * @author Dinis Lei (dinislei@ua.pt), Martinho Tavares (martinho.tavares@ua.pt)
 *
 * @brief Main file for Program 2.
 * 
 * Sort an array of integers stored in a file, whose path is provided as a command-line argument.
 * The bitonic sorting algorithm is used, done using multithreading.
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
#include <math.h>
#include <time.h>
#include <mpi.h>

#include "constants.h"
#include "sort_array.h"


/**
 * @brief Print command usage.
 *
 * A message specifying how the program should be called is printed.
 *
 * @param cmdName string with the name of the command
 */
static void printUsage (char *cmdName);

/** 
 * @brief Read the contents of a file containing integers into memory.
 * 
 * @param filename name of the file to be read
 * @param numbers address to which the integers will be stored
 * @param numbers_size address to which the amount of numbers read from the file will be stored
 * @return whether the file was successfully read
 */
bool readIntegerFile(char* filename, int** numbers, int* numbers_size);

/**
 * @brief Returns the largest power of 2 lesser than or equal to x. Only works if x is a 32-bit number.
 * @param x 
 * @return 
 */
uint32_t previousPower2(uint32_t x);

bool validateSort(int* numbers, int numbers_size);

/**
 * @brief Merge a bitonic sequence into an ascending or descending sequence.
 *
 * @param arr bitonic sequence
 * @param size size of the sequence
 * @param asc is ascending
 */
static void bitonicMerge(int* arr, int size, bool asc);

/**
 * @brief Sort a sequence of integers into a bitonic sequence.
 *
 * @param arr bitonic sequence
 * @param size size of the sequence
 * @param asc is ascending
 */
static void bitonicSort(int* arr, int size, bool asc);

/**
 * @brief Swap elements in array in ascending or descending order.
 *
 * @param arr array of elements to swap
 * @param i index of first element
 * @param j index of second element
 * @param asc is ascending
 */
static void swap(int* arr, int i, int j, bool asc);

/** @brief Execution time measurement */
static double get_delta_time(void);

/**
 * @brief Main thread.
 *
 * Its role is storing the name of the file to process, and launching the distributor and worker threads.
 * Afterwards, it waits for their termination, and validates if the sort was correctly performed.
 * 
 * @param argc number of words of the command line
 * @param argv list of words of the command line
 * @return status of operation
 */
int main(int argc, char *argv[]) {
    int opt;
    extern char* optarg;
    extern int optind;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                printUsage(basename(argv[0]));
                return EXIT_SUCCESS;
            case '?': /* invalid option */
                fprintf (stderr, "%s: invalid option\n", basename(argv[0]));
                exit(EXIT_FAILURE);
        }
    }

    if (argc < 2) {
        fprintf(stderr, "Input some files to process!\n");
        exit(EXIT_FAILURE);
    }

    int rank, size;
    int numbers_size;
    int* numbers;
    int* numbers_partial;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm comm = MPI_COMM_WORLD;

    printf("%d - Init\n", rank);

    bool can_proceed = false;
    if (rank == 0) {
        (void) get_delta_time();
        char* filename = argv[optind];
        can_proceed = readIntegerFile(filename, &numbers, &numbers_size);
        // printf("%d - Numbers Size: %d\n", rank, numbers_size);
        // for (int i = 0; i < numbers_size; i++) {
        //     printf("%d ", numbers[i]);
        // }
        // printf("\n");
    }

    MPI_Bcast(&can_proceed, 1, MPI_C_BOOL, 0, comm);    
    if (!can_proceed) {
        MPI_Finalize();
        exit(EXIT_FAILURE);
    }

    MPI_Bcast(&numbers_size, 1, MPI_INT, 0, comm);

    numbers_partial = malloc(numbers_size * sizeof(int));

    int tot_iterations = previousPower2(size);

    int nits = 0;
    for (int iteration = tot_iterations; iteration > 0; iteration >>= 1) {
        // if (rank == 0) {
        //     printf("%d - Iteration %d\n", rank, iteration);
        //     for (int i = 0; i < numbers_size; i++) {
        //         printf("%d ", numbers[i]);
        //     }
        //     printf("\n");
        // }

        MPI_Comm new_comm;
        int color = (rank < iteration) ? 0 : 1;
        MPI_Comm_split(comm, color, rank, &new_comm);
        comm = new_comm;

        if (color == 1)
            break;

        int chunk_size = numbers_size/iteration;
        if (rank == 0)
            printf("Chunk size = %d\n", chunk_size);

        MPI_Scatter(numbers, chunk_size, MPI_INT, numbers_partial, chunk_size, MPI_INT, 0, comm);
        // printf("%d - Finish Scatter\n", rank);

        // Sort Chunk
        if (iteration == tot_iterations)
            bitonicSort(numbers_partial, chunk_size, rank%2 == 0);
        else
            bitonicMerge(numbers_partial, chunk_size, rank%2 == 0);

        // printf("%d - Finish Sort\n", rank);
        // for (int i = 0; i < chunk_size; i++) {
        //     printf("%d ", numbers_partial[i]);
        // }
        // printf("\n");

        MPI_Gather(numbers_partial, chunk_size, MPI_INT, numbers, chunk_size, MPI_INT, 0, comm);
        // printf("%d - Finish Gather\n", rank);

        nits++;
    }

    // if (rank == 0) {
    //     printf("END: ");
    //     for (int i = 0; i < numbers_size; i++) {
    //         printf("%d ", numbers[i]);
    //     }
    //     printf("\n");
    // }


    if (rank == 0 && !validateSort(numbers, numbers_size)) {
        MPI_Finalize();
        exit(EXIT_FAILURE);
    }
        

    if (rank == 0) 
        printf ("\nElapsed time = %.6f s\n", get_delta_time ());

    printf("Rank %d Finished\n", rank);

    MPI_Finalize();
    exit(EXIT_SUCCESS);
}

static void swap(int* arr, int i, int j, bool asc) {
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

static void bitonicMerge(int* arr, int size, bool asc) {
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

static void bitonicSort(int* arr, int size, bool asc) {
    for (int j = 1; j <= log2(size); j++) {
        int N = pow(2, j);
        for (int i = 0; i < size; i += N) {
            bitonicMerge(arr + i, N, asc);
            asc = !asc;
        }
    }
}

static double get_delta_time(void) {
    static struct timespec t0, t1;

    t0 = t1;
    if(clock_gettime(CLOCK_MONOTONIC, &t1) != 0) {
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

bool readIntegerFile(char* filename, int** numbers, int* numbers_size) {
    printf("READ FILE %s\n", filename);
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stdout, "Error: could not open file %s\n", filename);
        return false;
    }
    int res = fread(numbers_size, sizeof(int), 1, file);
    if (res != 1) {
        if (ferror(file)) {
            fprintf(stderr, "Error: invalid file format\n");
            fclose(file);
            return false;
        }
        else if (feof(file)) {
            fprintf(stderr, "Error: end of file reached\n");
            fclose(file);
            return false;
        }
    }

    if (((*numbers_size) != 0) && (((*numbers_size) & ((*numbers_size) - 1)) != 0)) {
        fprintf(stderr, "Error: invalid file, array size must be a power of 2\n");
        fclose(file);
        return false;
    }

    (*numbers) = (int*) malloc((*numbers_size) * sizeof(int));

    while (true) {
        res = fread(*numbers, sizeof(int), (*numbers_size), file);
        if (feof(file)) {
            break;
        }
        else if (ferror(file)) {
            fprintf(stderr, "Error: invalid file format\n");
            fclose(file);
            return false;
        }
    }

    fclose(file);
    return true;
}

uint32_t previousPower2(uint32_t x) {
    if (x == 0) {
        return 0;
    }
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return x - (x >> 1);
}

bool validateSort(int* numbers, int numbers_size) {
    bool correct_sort = true;

    int i;
    for (i = 0; i < numbers_size - 1; i++)
        if (numbers[i] > numbers[i+1]) { 
            printf("Error in position %d between element %d and %d\n",
            i, numbers[i], numbers[i+1]);
            correct_sort = false;
            break;
        }

    if (correct_sort) {
        printf("Everything is OK!\n");
    }

    return correct_sort;
}