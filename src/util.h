#ifndef _UTIL_H
#define _UTIL_H

#include <stdint.h>

int read_size(uint8_t *buffer);
void write_size(uint8_t *buffer, int len);
uint32_t read_size_32(uint8_t *buffer);
void write_size_32(uint8_t *buffer, int len);

#endif // for #ifndef _UTIL_H
