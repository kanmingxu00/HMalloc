#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#include "hmalloc.h"

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

// Free List Node
// Your allocator should maintain a free list of available blocks of memory.
// This should be a singly linked list sorted by block address.
typedef struct free_list_node {
	size_t size;
	struct free_list_node *prev;
	struct free_list_node *next;
} free_list_node;

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
static free_list_node *free_list = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

long free_list_length() {
	// TODO: Calculate the length of the free list.
	long length = 0;
	free_list_node* temp = free_list;
	// use free_list here and just free everything in that
	while (temp != 0) {
		temp = temp->next;
		length += 1;
	}
	return length;
}

hm_stats*
hgetstats() {
	stats.free_length = free_list_length();
	return &stats;
}

void hprintstats() {
//	printf("head size: %ld", free_list->size);
//	puts("Here");
	stats.free_length = free_list_length();
	fprintf(stderr, "\n== husky malloc stats ==\n");
	fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
	fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
	fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
	fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
	fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

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

void coalesce_helper(free_list_node* node) {
	node->prev = 0;
	node->next = 0;
	if (free_list == 0) {
		free_list = node;
//		printf("free_list: %ld", free_list_length());
	} else {
		free_list_node* temp = free_list;
		while (temp != 0) {
			if ((void*) temp > (void*) node) { // inserts after the current index
//				puts("now");
				if (temp->prev == 0 && temp->next == 0) {
					node->next = temp;
					temp->prev = node;
					free_list = node;
					free_list->prev = 0;
					free_list->next = 0;
//					puts("ho");
				} else if (temp->prev != 0) {
//					printf("size: %ld\n", sizeof(temp->prev));
//					puts("kil me");
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
//						if (temp->prev != 0) { // if temp isn't the head
						temp->prev->next = node;
						node->prev = temp->prev; // remove temp effectively
//						}
						node->next = temp->next;
					} else { // tempprev, node, temp
						temp->prev->next = node;
						node->prev = temp->prev;
						node->next = temp;
						temp->prev = node;
//											puts("here");
					}
				} else if (temp->prev == 0) { // else if (temp->prev == 0) {
//					node->next = free_list;
					node->next = temp;
					temp->prev = node;
					node->prev = 0;
//					puts("relative");
					free_list = node;

				}

				break;
			}
			temp = temp->next;
		}
	}
//	printf("%ld", free_list->size);
}

void*
hmalloc(size_t size) {
	// printf("%ld\n", pthread_self());
	int ret = pthread_mutex_lock(&mutex);
	assert(ret != -1);
	stats.chunks_allocated += 1;
	size += sizeof(free_list_node);
//	if (free_list != 0) {
////		if (free_list->next == 0) {
////			puts("wow");
////		}
//		printf("head_size: %ld, size: %ld, mapped: %ld\n",
//			free_list->size, size, stats.pages_mapped);
//	}

	if (size < 4096) {
		free_list_node *temp = free_list;
		free_list_node *free_block = 0;
		while (temp != 0) { // finding where to put the thing
			if (temp->size >= size) {
				free_block = temp;
//				if (temp->next == 0) {
//					free_block->next = 0;
//				} else {
//					free_block->next = temp->next;
//				}
				if (temp->prev == 0) { // first element
					free_list = temp->next;
					if (free_list != 0) {
						free_list->prev = 0; // this is the key
					}
					// delete first so can update with real size
					// this is doing correctly on size debug

//					printf("%ld", free_list->size);
//					printf("%ld\n", size);
				} else {
					temp->prev->next = temp->next;
					if (temp->next != 0) {
						temp->next->prev = temp->prev;
					}
					// get rid of node so
					// other nodes can fill it in during coalescing
				}
				break;
			}
			temp = temp->next;
		}
		if (free_block == 0) { // not found free block big enough
//			if (free_list != 0) {
//			//		if (free_list->next == 0) {
//			//			puts("wow");
//			//		}
//					printf("head_size: %ld, size: %ld, mapped: %ld\n",
//						free_list->size, size, stats.pages_mapped);
//				}
			free_block = mmap(0, 4096, PROT_WRITE | PROT_READ,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			assert(free_block != MAP_FAILED);
			free_block->size = 4096;
			stats.pages_mapped = stats.pages_mapped + 1;
		} // proceed to fill in free list
//		else {
//			printf("free_block size: %ld", free_block->size);
//			printf("length: %ld", free_list_length());
//		}
		if (free_block->size > size + sizeof(free_list_node)) {

			void* temp_address = (void*)free_block + size;
			free_list_node* remaining = (free_list_node*)temp_address;
			remaining->prev = 0;
			remaining->next = 0;
			remaining->size = free_block->size - size;
//			printf("%ld\n", remaining->size);

//			printf("%ld", free_block->next);
//			if (remaining == 0) {
//			printf("%ld\n", remaining->size);
//			}
			coalesce_helper(remaining); // this inserts to FL while coalescing
			free_block->size = size;
		} // case for = individually not necessary -- node will just be 0 size

		ret = pthread_mutex_unlock(&mutex);
		assert(ret != -1);
		return (void*) free_block + sizeof(size_t);
	} else {
		int pages = div_up(size, 4096);
		free_list_node *free_block = mmap(0, pages * 4096,
				PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		assert(free_block != MAP_FAILED);
		stats.pages_mapped += pages;
		free_block->size = pages * 4096;
		ret = pthread_mutex_unlock(&mutex);
		assert(ret != -1);
		return (void*) free_block + sizeof(size_t);
	}
}

void hfree(void *item) {
	int ret = pthread_mutex_lock(&mutex);
	assert(ret != -1);
	stats.chunks_freed += 1;
	void* temp_address = (void*) (item - sizeof(size_t));
	free_list_node *free_block = (free_list_node*) temp_address;
//	printf("%ld: %ld\n", stats.chunks_freed, free_block->size); // why failing on 184?
//    free_block->prev = 0;
//    free_block->next = 0;
	// commenting these out fix my free issue
	// leaving this in because important to realize that the structure
	// comes in with prev and next already set.

	if (free_block->size < 4096) {
		free_block->next = 0;
		free_block->prev = 0;
		coalesce_helper(free_block);
	} else {
		int pages = div_up(free_block->size, 4096);
		stats.pages_unmapped += pages;
		int rv = munmap((void*)free_block, free_block->size);

		assert(rv != -1);
	}
	ret = pthread_mutex_unlock(&mutex);
	assert(ret != -1);
}

void* hrealloc(void* prev, size_t bytes) {
	void* temp = hmalloc(bytes);
	memcpy(temp, prev, bytes);
	hfree(prev);
    return temp;

}
