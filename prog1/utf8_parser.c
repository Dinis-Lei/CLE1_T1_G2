/**
 * @file utf8_parser.c (implementation file)
 * 
 * @author Dinis Lei (you@domain.com), Martinho Tavares (martinho.tavares@ua.pt)
 * 
 * @brief Helper functions for parsing and analyzing an UTF-8 byte stream.
 * 
 * @date March 2023
 * 
 */

#include <stdbool.h>

#include "utf8_parser.h"


void readUTF8Character(unsigned char* byte_stream, unsigned char* code, int* code_size) {
    *code_size = 0;
    // Byte is 1-byte Code    
    if (!(byte_stream[0] & 0x80)) {
        *code_size = 1;
    }
    // Byte is 1st byte of a 2-byte code
    else if ((byte_stream[0] & 0xe0) == 0xc0) {
        *code_size = 2;
    }
    // Byte is 1st byte of a 3-byte code
    else if ((byte_stream[0] & 0xf0) == 0xe0) {
        *code_size = 3;
    }
    // Byte is 1st byte of a 4-byte code
    else if ((byte_stream[0] & 0xf8) == 0xf0) {
        *code_size = 4;
    }

    // Grab code
    for (int i = 0; i < *code_size; i++)
        code[i] = byte_stream[i];
}

bool isalphanum(unsigned char* code) {
    
    return
        (code[0] >= 0x30     && code[0] <= 0x39)    || 
        (code[0] >= 0x41     && code[0] <= 0x5a)    ||
        (code[0] == 0x5f)                           ||
        (code[0] >= 0x61     && code[0] <= 0x7a)    ||
        (code[0] >= 0x61     && code[0] <= 0x7a)    ||
        (code[0] == 0xc3     && (
            (code[1] >= 0x80 && code[1] <= 0x83)    ||
            (code[1] >= 0x87 && code[1] <= 0x8a)    ||
            (code[1] >= 0x8c && code[1] <= 0x8d)    ||
            (code[1] >= 0x92 && code[1] <= 0x95)    ||
            (code[1] >= 0x99 && code[1] <= 0x9a)    ||
            (code[1] >= 0xa0 && code[1] <= 0xa3)    ||
            (code[1] >= 0xa7 && code[1] <= 0xaa)    ||
            (code[1] >= 0xac && code[1] <= 0xad)    ||
            (code[1] >= 0xb2 && code[1] <= 0xb5)    ||
            (code[1] >= 0xb9 && code[1] <= 0xba) 
        ));
}

bool isapostrofe(unsigned char* code) {
    return (code[0] == 0x27) || (code[0] == 0xe2 && code[1] == 0x80 && (code[2] == 0x98 || code[2] == 0x99));
}

int whatVowel(unsigned char* code) {
    if ( code[0] == 0x41 || code[0] == 0x61 || (code[0] == 0xc3 && ((code[1] >= 0x80 && code[1] <= 0x83) || (code[1] >= 0xa0 && code[1] <= 0xa3)))) {
        return 1;
    }
    if ( code[0] == 0x45 || code[0] == 0x65 || (code[0] == 0xc3 && ((code[1] >= 0x88 && code[1] <= 0x8a) || (code[1] >= 0xa8 && code[1] <= 0xaa)))) {
        return 2;
    }
    if ( code[0] == 0x49 || code[0] == 0x69 || (code[0] == 0xc3 && ((code[1] >= 0x8c && code[1] <= 0x8d) || (code[1] >= 0xac && code[1] <= 0xad)))) {
        return 3;
    }
    if ( code[0] == 0x4f || code[0] == 0x6f || (code[0] == 0xc3 && ((code[1] >= 0x92 && code[1] <= 0x95) || (code[1] >= 0xb2 && code[1] <= 0xb5)))) {
        return 4;
    }
    if ( code[0] == 0x55 || code[0] == 0x75 || (code[0] == 0xc3 && ((code[1] >= 0x99 && code[1] <= 0x9a) || (code[1] >= 0xb9 && code[1] <= 0xba)))) {
        return 5;
    }
    if ((code[0] == 0x59 || code[0] == 0x79)) {
        return 6;
    }

    return -1;
}