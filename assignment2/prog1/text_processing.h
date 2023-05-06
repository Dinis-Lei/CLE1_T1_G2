/**
 * @file text_processing.h (interface file)
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