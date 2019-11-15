#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#include "opt_malloc.h"

/*
 typedef struct hm_stats {
 long pages_mapped;
 long pages_unmapped;
 long chunks_allocated;
 long chunks_freed;
 long free_length;
 } hm_stats;
 */

// preface: sorry it's so messy, leaving the notes in so i can see
// my thought process during the challenge problem.

// have pthread id
// use this as index for access
// how to ensure that we have {access} free lists?
// per-thread arena:
// - track the threads (and ids?) we have seen in a global variable)
// - when a new thread is added, create an arena
// - this arena contains a series of buckets (how od buckets work ????)
// - these buckets contain memory which was allocated in this thread
// - 1 arena per thread: lock ur thread when modifying the mutex
// - if i see a free list node, how do i know which thread i am in?
// - sol: give each node a pointer to its arena
// - this allows the arena to be locked from any of the nodes of its free list


// alternative:
// - every node has its own lock.
// - the nodes are only locked when modified,
// - so locking isn't a big deal at all.

// big q: how do we free across threads?
// - return the memory allocated to that item to our thread's free list
// - return the memory allocated to another thread's free list (this requires saving the thread in which it was created)

// alternatively, implement strategy similar to fb allocator
// this means allowing for arenas = 4x cpu cores
// further, allocate many chunks instead of one at a time

// how to coalesce across arenas? do we care?

// arena: data structure

// Free List Node
// Your allocator should maintain a free list of available blocks of memory.
// This should be a singly linked list sorted by block address.
typedef struct free_list_node {
	size_t size;
	int thread_id;
	struct free_list_node *prev;
	struct free_list_node *next;
} free_list_node;


const size_t PAGE_SIZE = 4096;

__thread pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static long ids[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static pthread_mutex_t mutexs[32] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER, };
static free_list_node* free_lists[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static size_t div_up(size_t xx, size_t yy) {
	// This is useful to calculate # of pages
	// for large allocations.
	size_t zz = xx / yy;

	if (zz * yy == xx) {
		return zz;
	} else {
		return zz + 1;
	}
}

void coalesce_helper(free_list_node* node, int thread_id) {
	node->prev = 0;
	node->next = 0;
	pthread_mutex_lock(&mutexs[thread_id]);
	if (free_lists[thread_id] == 0) {
		free_lists[thread_id] = node;
	} else {
		free_list_node* temp = free_lists[thread_id];
		while (temp != 0) {
			if ((void*) temp > (void*) node) { // inserts after the current index
				if (temp->prev == 0 && temp->next == 0) {
					node->next = temp;
					temp->prev = node;
					free_lists[thread_id] = node;
					free_lists[thread_id]->prev = 0;
					free_lists[thread_id]->next = 0;
				} else if (temp->prev != 0) {
					if ((void*) temp->prev + temp->prev->size == (void*) node
							&& (void*) node + node->size == (void*) temp) { // both sites touching

						temp->prev->size = temp->prev->size + node->size
								+ temp->size;
						temp->prev->next = temp->next;
						if (temp->next != 0) {
							temp->next->prev = temp->prev;
						}
					} else if ((void*) temp->prev + temp->prev->size
							== (void*) node) { // previous touches
						temp->prev->size = temp->prev->size + node->size;
					} else if ((void*) node + node->size == (void*) temp) { // node is before the current temp
						node->size = node->size + temp->size;
						temp->prev->next = node;
						node->prev = temp->prev; // remove temp effectively
						node->next = temp->next;
					} else { // tempprev, node, temp
						temp->prev->next = node;
						node->prev = temp->prev;
						node->next = temp;
						temp->prev = node;
					}
				} else if (temp->prev == 0) {
					node->next = temp;
					temp->prev = node;
					node->prev = 0;
					free_lists[thread_id] = node;

				}

				break;
			}
			temp = temp->next;
		}
	}
	pthread_mutex_unlock(&mutexs[thread_id]);
}

int
thread_get(long id) {
	for (int ii = 0; ii < 5; ++ii) {
		if (ids[ii] == 0) {
			ids[ii] = id;
			return ii;
		} else if (ids[ii] == id) {
			return ii;
		}
	}
	assert(0);
}






void*
opt_malloc(size_t size) {
	int thread_id = thread_get(pthread_self());
	size += 12;
	if (size < PAGE_SIZE) {
		pthread_mutex_lock(&mutexs[thread_id]);
		free_list_node *temp = free_lists[thread_id];
		free_list_node *free_block = 0;
		while (temp != 0) { // finding where to put the thing
			if (temp->size >= size) {
				free_block = temp;
				if (temp->prev == 0) { // first element
					free_lists[thread_id] = temp->next;
					if (free_lists[thread_id] != 0) {
						free_lists[thread_id]->prev = 0; // this is the key
					}
					// delete first so can update with real size
					// this is doing correctly on size debug
				} else {
					temp->prev->next = temp->next;
					if (temp->next != 0) {
						temp->next->prev = temp->prev;
					}
					// get rid of node so
					// other nodes can fill it in during coalescing
				}
				pthread_mutex_unlock(&mutexs[thread_id]);
				break;
			}
			temp = temp->next;
		}
		if (free_block == 0) { // not found free block big enough
			pthread_mutex_unlock(&mutexs[thread_id]);
			free_block = mmap(0, PAGE_SIZE, PROT_WRITE | PROT_READ,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			assert(free_block != MAP_FAILED);
			free_block->size = PAGE_SIZE;
		} // proceed to fill in free list
		if (free_block->size > size + sizeof(free_list_node)) {

			void* temp_address = (void*)free_block + size;
			free_list_node* remaining = (free_list_node*)temp_address;
			remaining->prev = 0;
			remaining->next = 0;
			remaining->size = free_block->size - size;
			coalesce_helper(remaining, thread_id); // this inserts to FL while coalescing
			free_block->size = size;
		} // case for = individually not necessary -- node will just be 0 size
		free_block->thread_id = thread_id;
		return (void*) free_block + 12;
    } else {int pages = div_up(size, PAGE_SIZE);
		free_list_node *free_block = mmap(0, pages * PAGE_SIZE,
				PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		assert(free_block != MAP_FAILED);
		free_block->size = pages * 4096;
		return (void*) free_block + 12;
	}
}

void opt_free(void *item) {
	void* temp_address = (void*) (item - 12);
	free_list_node *free_block = (free_list_node*) temp_address;
	int thread_id = free_block->thread_id;

	if (free_block->size < PAGE_SIZE) {
		free_block->next = 0;
		free_block->prev = 0;
		coalesce_helper(free_block, thread_id);
	} else {
		int pages = div_up(free_block->size, PAGE_SIZE);
		int rv = munmap((void*)free_block, free_block->size);
		assert(rv != -1);
	}
}

void* opt_realloc(void* prev, size_t bytes) {
	void* add = (void*)(prev - 12);
	free_list_node* node = (free_list_node*)add;
	if (node->size < bytes + 12) {
		void *temp = opt_malloc(bytes);
		memcpy(temp, prev, bytes);
		opt_free(prev);
		return temp;
	} else if (node->size == bytes + 12) {
		return prev;
	} else {
		size_t size = node->size;
		int thread_id = node->thread_id;
		node->size = bytes + 12;
		free_list_node* tmp = (free_list_node*)(node + (bytes + 12));
		tmp->size = size - bytes - 12;
		tmp->thread_id = thread_id;
		coalesce_helper(tmp, thread_id);
		return prev;
	}
}
