#include <stdlib.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#include "opt_malloc.h"

typedef struct free_list_node {
	size_t size;
	int thread_id;
	struct free_list_node *prev;
	struct free_list_node *next;
} free_list_node;


const size_t PAGE_SIZE = 20480;
static long ids[64] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0}; // can handle 64 threads, and would be able to handle more if more were added
static pthread_mutex_t mutexs[64] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
		, PTHREAD_MUTEX_INITIALIZER};
static free_list_node* free_lists[64] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0};

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
	if (free_lists[thread_id] == 0) {
		free_lists[thread_id] = node;
	} else {
		free_list_node* temp = free_lists[thread_id];
		while (temp != 0) {
			if ((void*) temp > (void*) node) {
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
						node->prev = temp->prev;
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
}

int
thread_get(long id) {
	for (int ii = 0; ii < 64; ++ii) {
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
	int thread_id = 2 * thread_get(pthread_self());
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
						free_lists[thread_id]->prev = 0;
					}
				} else {
					temp->prev->next = temp->next;
					if (temp->next != 0) {
						temp->next->prev = temp->prev;
					}
				}
				break;
			}
			temp = temp->next;
		}
		if (free_block == 0) {
			free_block = mmap(0, PAGE_SIZE, PROT_WRITE | PROT_READ,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			assert(free_block != MAP_FAILED);
			free_block->size = PAGE_SIZE;
		}
		if (free_block->size > size + sizeof(free_list_node)) {
			void* temp_address = (void*)free_block + size;
			free_list_node* remaining = (free_list_node*)temp_address;
			remaining->prev = 0;
			remaining->next = 0;
			remaining->size = free_block->size - size;
			coalesce_helper(remaining, thread_id); // this inserts to FL while coalescing
			free_block->size = size;
		}
		free_block->thread_id = thread_id;
		pthread_mutex_unlock(&mutexs[thread_id]);
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
	pthread_mutex_lock(&mutexs[thread_id]);

	if (free_block->size < PAGE_SIZE) {
		free_block->next = 0;
		free_block->prev = 0;
		coalesce_helper(free_block, thread_id);
		pthread_mutex_unlock(&mutexs[thread_id]);
	} else {
		pthread_mutex_unlock(&mutexs[thread_id]);
		int pages = div_up(free_block->size, PAGE_SIZE);
		int rv = munmap((void*)free_block, pages*PAGE_SIZE);
		assert(rv != -1);
	}
}

void* opt_realloc(void* prev, size_t bytes) {
	void* temp = opt_malloc(bytes);
	memcpy(temp, prev, bytes);
	opt_free(prev);
    return temp;
}

