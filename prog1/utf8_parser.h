/**
 * @file utf8_parser.h (interface file)
 * 
 * @author Dinis Lei (you@domain.com), Martinho Tavares (martinho.tavares@ua.pt)
 * 
 * @brief Helper functions for parsing and analyzing an UTF-8 byte stream.
 * 
 * @date March 2023
 * 
 */

#ifndef PROG1_UTF8_PARSER_H
#define PROG1_UTF8_PARSER_H

/**
 * @brief Read a single UTF-8 character from a byte stream.
 * @param byte_stream the stream from which to read a character
 * @param code the buffer to hold the character (should be a 4-byte array)
 * @param code_size actual number of bytes that were read to the buffer
 */
void readUTF8Character(unsigned char* byte_stream, unsigned char* code, int* code_size);

/**
 * @brief Receives an utf-8 code and asserts if it is alphanumeric or '_'
 * @param symbol array of bytes corresponding to an utf-8 code
 * @return assertion
 */
bool isalphanum(unsigned char* code);

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

#endif /* PROG1_UTF8_PARSER_H */