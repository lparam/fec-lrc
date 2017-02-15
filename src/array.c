
#include <string.h>
#include "array.h"

void
array_clean(array_t *array) {
    memset(array->elts, 0, array->nalloc * array->size);
}

int
array_init(array_t *array, uint32_t n, size_t size) {
    array->extra = NULL;
    array->nelts = 0;
    array->nalloc = n;
    array->size = size;

    array->elts = malloc(n * size);
    if (array->elts == NULL) {
        return 1;
    }

    return 0;
}

int
array_init_extra(array_t *array, uint32_t n, size_t size) {
    array->extra = malloc(n * size);
    if (array->elts == NULL) {
        return 1;
    }
    return 0;
}

array_t *
array_create(uint32_t n, size_t size) {
    array_t *a;

    a = malloc(sizeof(array_t));
    if (a == NULL) {
        return NULL;
    }

    if (array_init(a, n, size) != 0) {
        return NULL;
    }

    return a;
}

void
array_destroy(array_t *a) {
    if (a->extra != NULL) {
        free(a->extra);
    }
    free(a->elts);
    free(a);
}
