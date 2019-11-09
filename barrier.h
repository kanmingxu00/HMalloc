// Author: Nat Tuck
// CS3650 starter code + pthread mutices

#ifndef BARRIER_H
#define BARRIER_H

#include <pthread.h>

typedef struct barrier {
    // Need some synchronization stuff.
    int   count;
    int   seen;
    pthread_mutex_t mutex;
    pthread_cond_t condv;
} barrier;

barrier* make_barrier(int nn);
void barrier_wait(barrier* bb);
void free_barrier(barrier* bb);


#endif
