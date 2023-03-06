#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>
#include <time.h>


// TODO: specify monitor's data structures and the routines to access it

struct {
    FILE* file_pointer;
    int* vowels;
} accumulationInfo;

int main (int argc, char *argv[]) {

    if (argc < 2) {
        printf("Input some files to process!\n");
        return 0;
    }

    
    return 0;
}