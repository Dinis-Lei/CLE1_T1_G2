/**
 * @file chunk_reader.h (interface file)
 * 
 * @author Dinis Lei (dinislei@ua.pt), Martinho Tavares (martinho.tavares@ua.pt)
 * 
 * @brief Monitor for mutual exclusion in Program 1.
 * 
 * Implements a Lampson/Redell monitor to allow concurrent word counting on a
 * list of files that are sequentially processed.
 * 
 * Defines the following procedures for the main thread:
 * \li storeFiles
 * \li monitorFreeMemory
 * \li printResults
 * Defines the following procedures for the workers:
 * \li readChunk
 * \li updateCounters
 * 
 * @date March 2023
 * 
 */

#ifndef PROG1_CHUNK_READER_H
#define PROG1_CHUNK_READER_H

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
 * @brief Free all memory allocated to the files' names and word counters.
 * 
 */
void monitorFreeMemory();

/**
 * @brief Store the path of the files to process into shared memory.
 * 
 * Additionally, allocate memory for the word counters for each of the stored file paths.
 * 
 * @param file_names_in array of file paths
 */
void storeFiles(char** file_names_in);

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
 * @brief Print the formatted word count results to standard output
 * @param file_names Array with the name of the files to show results
 */
void printResults();

#endif /* PROG1_CHUNK_READER_H */