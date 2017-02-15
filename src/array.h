#ifndef _ARRAY_H
#define _ARRAY_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
    void      *elts;
    void      *extra;
    uint32_t   nelts;
    uint32_t   nalloc;
    size_t     size;
} array_t;

array_t * array_create(uint32_t n, size_t size);
void array_destroy(array_t *a);
int array_init_extra(array_t *array, uint32_t n, size_t size);
void array_clean(array_t *array);

#endif // for #ifndef _ARRAY_H
