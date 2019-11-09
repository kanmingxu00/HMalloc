// Author: Nat Tuck
// CS3650 starter code

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include "barrier.h"

/* 
typedef struct barrier {
    // Need some synchronization stuff.
    int   count;
    int   seen;
    pthread_mutex_t mutex;
    pthread_cond_t condv;
} barrier;
*/

barrier*
make_barrier(int nn)
{
    barrier* bb = malloc(sizeof(barrier));
    assert(bb != 0);

    bb->count = nn;  // 'These can't be right.''
    bb->seen  = 0;
    
    pthread_mutex_init(&bb->mutex, 0);
    pthread_cond_init(&bb->condv, 0);
    return bb;
}

void
barrier_wait(barrier* bb)
{
    // lock so we can write to the structure
    pthread_mutex_lock(&bb->mutex);
    // increment seen, as we are currently seeing this value
    bb->seen++;
    // if the seen value == count (that is, the last thread has been seen)
    // we broadcast that all of the rest of the threads should end
    if(bb->seen == bb->count) {
        pthread_cond_broadcast(&bb->condv);
    } else {
        // else,
        // while the number of seen threads is less than the total, 
        while (bb->seen < bb->count) {
            // wait for the threads to end
            pthread_cond_wait(&bb->condv, &bb->mutex);
        }
    }

    // if the thred is not waiting anymore,
    // unlock it and allow others to increment 'seen'''
    pthread_mutex_unlock(&bb->mutex);
}

void
free_barrier(barrier* bb)
{
    // fairly straightforward
    // frees the barrier
    free(bb);
}

