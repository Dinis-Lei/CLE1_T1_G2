#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

static void print_usage (char *cmdName);

// TODO: specify monitor's data structures and the routines to access it

struct {
    FILE* file_pointer;
    int* vowels;
} accumulationInfo;

int main (int argc, char *argv[]) {

    int opt;
    while((opt = getopt(argc, argv, ":h")) != -1) {
        switch (opt) {
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
	
	for(; optind < argc; optind++){	
		printf("File name: %s\n", argv[optind]);
        //TODO: Add Files pointers to shared memory
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
static void print_usage (char *cmdName)
{
  fprintf (stderr, "\nSynopsis: %s OPTIONS [FILE]...\n"
           "  OPTIONS:\n"
           "  -h      --- print this help\n", cmdName);
}