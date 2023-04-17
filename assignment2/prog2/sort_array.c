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
#include <pthread.h>
#include <math.h>
#include <time.h>
#include <mpi.h>

#include "sort_control.h"
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
 */
void readIntegerFile(char* filename, int** numbers, int* numbers_size);

/**
 * @brief Merge a bitonic sequence into an ascending or descending sequence
 *
 * @param arr bitonic sequence
 * @param size size of the sequence
 * @param asc is ascending
 */
static void bitonicMerge(int* arr, int size, bool asc);

/**
 * @brief Sort a sequence of integers into a bitonic sequence
 *
 * @param arr bitonic sequence
 * @param size size of the sequence
 * @param asc is ascending
 */
static void bitonicSort(int* arr, int size, bool asc);

/**
 * @brief Swap elements in array in ascending or descending order
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

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (rank == 0) {
        (void) get_delta_time();
        char* filename = argv[optind];
        readIntegerFile(filename, &numbers, &numbers_size);
    }
    MPI_Bcast(&numbers_size, 1, MPI_INT, 0, MPI_COMM_WORLD);

    
    if (rank >= n_workers_at_iteration)



    if (!validateSort())
        exit(EXIT_FAILURE);

    if (rank == 0) 
        printf ("\nElapsed time = %.6f s\n", get_delta_time ());
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

// TODO: mallocing inside function is a good practice?
void readIntegerFile(char* filename, int** numbers, int* numbers_size) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    int res = fread(&numbers_size, sizeof(int), 1, file);
    if (res != 1) {
        if (ferror(file)) {
            fprintf(stderr, "Invalid file format\n");
            exit(EXIT_FAILURE);
        }
        else if (feof(file)) {
            printf("Error: end of file reached\n");
            exit(EXIT_FAILURE);
        }
    }

    if (((*numbers_size) != 0) && (((*numbers_size) & ((*numbers_size) - 1)) != 0)) {
        fprintf(stderr, "Invalid file, Array must be a power of 2\n");
        exit(EXIT_FAILURE);
    }

    (*numbers) = (int*) malloc((*numbers_size) * sizeof(int));

    while (true) {
        res = fread(*numbers, sizeof(int), numbers_size, file);
        if (feof(file)) {
            break;
        }
        else if (ferror(file)) {
            fprintf(stderr, "Invalid file format\n");
            exit(EXIT_FAILURE);
        }
    }

    fclose(file);
}