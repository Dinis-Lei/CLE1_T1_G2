/**
 * @file sort_control.h (interface file)
 * 
 * @author Dinis Lei (you@domain.com), Martinho Tavares (martinho.tavares@ua.pt)
 * 
 * @brief Monitor for mutual exclusion in Program 2.
 * 
 * Implements a Lampson/Redell monitor to allow concurrent distribution and requests of sorting work.
 * The sorting work is distributed among all threads that had previously made a request.
 *
 * @date March 2023
 * 
 */

#ifndef PROG2_SORT_CONTROL_H
#define PROG2_SORT_CONTROL_H

/**
 * @brief Structure holding the work details that the sorter thread should use when sorting at the current stage
 * 
 * @param array address of the array to sort
 * @param array_size number of elements to sort
 * @param ascending whether the sort should be in ascending or descending order
 * @param should_work whether the sorter thread should perform work or terminate at this stage
 * @param skip_sort whether the bitonic sorting should skip to the merge step
 */
struct SorterWork {
    int* array;
    int array_size;
    bool ascending;
    bool should_work;
    bool skip_sort;
};

/**
 * @brief Store the name of the binary file containing the integer array to sort
 * @param name name of the file
 */
void storeFilename(char* name);

/**
 * @brief Requests and fetches work assigned by the distributor
 * @param id worker thread's application id
 * @param work the work details to be fetched
 */
void fetchWork(int id, struct SorterWork* work);

/**
 * @brief Request work from the distributor
 * @param id application defined worker thread id
 */
void requestWork(int id);

/** @brief Signal that the sorting work has been finished. To be done by the worker threads */
void reportWork();

/** @brief Read the contents of the file in Shared Memory */
void readIntegerFile();

/**
 * @brief Continuously checks which workers have requested work and assigns it to them until the stored integer array is sorted
 * @param work_to_distribute array of sorting work to distribute among workers
 * @param n_workers number of work to distribute
 */
void distributeWork(struct SorterWork* work_to_distribute, int n_workers);

/**
 * @brief Obtain the division of the integer array into exclusive, evenly-sized subsequences.
 * 
 * Allows the population of the SorterWork array and array_size fields, specifying on which subsequence
 * the work should be performed.
 * The caller is thus abstracted from the location of the integer array and its size
 * 
 * @param number_of_subsequences number of subsequences
 * @param subsequence_idx index of the subsequence to obtain
 * @param subsequence address on which the subsequence address will be written
 * @param subsequence_size address on which the size of the defined subsequence will be written
 */
void defineIntegerSubsequence(int number_of_subsequences, int subsequence_idx, int** subsequence, int* subsequence_size);

/**
 * @brief Check if the stored integer array is properly sorted in ascending order
 * 
 * @return true if array is sorted in ascending order, false otherwise
 */
bool validateSort();

#endif /* PROG2_SORT_CONTROL_H */