#ifndef OPT_MALLOC_H
#define OPT_MALLOC_H

#include <stddef.h>

// Husky Malloc Interface
// cs3650 Starter Code

typedef struct hm_stats {
    long pages_mapped;
    long pages_unmapped;
    long chunks_allocated;
    long chunks_freed;
    long free_length;
} hm_stats;

hm_stats* opt_getstats();
void opt_printstats();

void* opt_malloc(size_t size);
void opt_free(void* item);
void* opt_realloc(void* prev, size_t bytes);

#endif
