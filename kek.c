#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
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

// Define a struct for a free_list node
// Contains the size of the
typedef struct free_list_node {
  size_t size;
  struct free_list_node* next;
} free_list_node;

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
static free_list_node* free_list_head;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/*
Caluclate the length of the free list
*/
long
free_list_length()
{
    long length = 0;
    free_list_node* cur = free_list_head;

    while (cur != 0) {
      length += 1;
      cur = cur->next;
    }

    return length;
}

hm_stats*
hgetstats()
{
    stats.free_length = free_list_length();
    return &stats;
}

void
hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

static
size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx) {
        return zz;
    }
    else {
        return zz + 1;
    }
}

/*
Inserts the given node into the free_list, ensuring the following variants:
1. The free list is sorted by memory address of the blocks.
2. Any two adjacent blocks on the free list get coalesced (joined together) into one bigger block.
*/
void
insert_and_coalesce(free_list_node* node) {
//	pthread_mutex_lock(&mutex);
  // if the list is empty, just insert it
  if (free_list_head == 0) {
    free_list_head = node;
    return;
  }

  free_list_node* cur = free_list_head;
  free_list_node* prev = 0;
  while (cur != 0) {
    // if the current node is greater than the given address,
    // insert the given node after the previous node
    if ((void*) cur > (void*) node) {
      //TODO: tring to coalesce on insertion... could be wrong.
      // commented out old stuff so if all fails, just uncomment that and delete whatever else here

      // Get the size of the previous node, unless it is null
      size_t prev_size = 0;
      if (prev != 0) {
        prev_size = prev->size;
      }

      // The given node is adjacent to the nodes on both sides of it (prev and cur)
      // Need to coalesce on both sides
      if (((void*) prev + prev_size == (void*) node) && ((void*) node + node->size == (void*) cur)) {
        // basically, discard the given node and the current node
        // just keep the previous node, and adjust size accordingly
        prev->size = prev->size + node->size + cur->size;
        prev->next = cur->next;
      }

      // The given node is adjacent to the end of the previous node
      // Need to coalesce
      else if ((void*) prev + prev_size == (void*) node) {
        // just increase the size of the previous node to include the given node
        // basically, discard the given node b/c it is now part of the prev node
        prev->size = prev->size + node->size;
      }

      // The given node is adjacent to the beginning of the current node
      // Need to coalesce
      else if ((void*) node + node->size == (void*) cur) {
        // increase the size of the given node to include the current node size
        node->size = node->size + cur->size;

        // basically, discard the current node and replace it with the given node
        if (prev != 0) {
          prev->next = node; //point the previous node to the given node if prevous is not null
        }
        node->next = cur->next; //point the given node to the current's next node
      }

      // No coalescing necessary because the given node is not adjacent to any existing blocks
      // Just insert the node
      else {
        // set the previous' next node to the given node if previous is not null
        if (prev != 0) {
          prev->next = node;
        }
        // set the given node's next to the current node
        node->next = cur;
      }

      // If the previous node was null, then we are inserting at the beginning of the free list
      // So we need to update free_list_head
      if (prev == 0) {
        free_list_head = node;
      }

      // break because the node has been inserted or coalesced
      break;

      //TODO delete below
      /*
      // set the previous' next node to the given node
      prev->next = node;
      // set the given node's next to the current node
      node->next = cur;
      break;
      */
    }
    prev = cur; // set the prev node to the current node
    cur = cur->next; // set the current node to the next node
  }
//pthread_mutex_unlock(&mutex);
}


void*
hmalloc(size_t size)
{
    stats.chunks_allocated += 1;

    size += sizeof(size_t); //make space to track the size of the block

    if (size < PAGE_SIZE) {
//    	pthread_mutex_lock(&mutex);
      free_list_node* big_enough_block = 0; //initialize the big enough block to null
      free_list_node* cur = free_list_head;
      free_list_node* prev = 0;
      while (cur != 0) {
        if (cur->size >= size) {
          big_enough_block = cur;
          if (prev != 0) {
            prev->next = cur->next;
          } else {
        	  free_list_head = cur->next;
          }

          break;
        }
        prev = cur;
        cur = cur->next;
      }


      if (big_enough_block == 0) {
        big_enough_block = mmap(0, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        assert(big_enough_block != MAP_FAILED);
        stats.pages_mapped += 1;
        big_enough_block->size = PAGE_SIZE;
      }

//  	pthread_mutex_unlock(&mutex);
      if ((big_enough_block->size > size) && (big_enough_block->size - size >= sizeof(free_list_node))) {
        void* address = (void*) big_enough_block + size;
        free_list_node* leftover = (free_list_node*) address;
        leftover->size = big_enough_block->size - size;

        big_enough_block->size = size;
      }

      return (void*) big_enough_block + sizeof(size_t);
    }

    else {
      int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
      size_t size = num_pages * PAGE_SIZE;
      free_list_node* block = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
      assert(block != MAP_FAILED);
      stats.pages_mapped += num_pages;
      block->size = size;
      return (void*) block + sizeof(size_t);
    }
}

/*
To free a block of memory,
first find the beginning of the block by subtracting sizeof(size_t) from the provided pointer.
If the block is < 1 page then stick it on the free list.
If the block is >= 1 page, then munmap it.
*/
void
hfree(void* item)
{
    stats.chunks_freed += 1;

    // find the beginning of the block
    free_list_node* block = (free_list_node*) (item - sizeof(size_t));

    // block is less than 1 page in size
    if (block->size < PAGE_SIZE) {
      // stick the block back on the free list
      insert_and_coalesce(block);
    }

    // block is greater than or equal to 1 page in size
    else {
      // munmap the block
      int num_pages = (block->size + PAGE_SIZE - 1) / PAGE_SIZE;
      int rv = munmap((void*) block, block->size);
      assert(rv != -1);
      stats.pages_unmapped += num_pages;
    }
}


void* hrealloc(void* prev, size_t bytes) {
	stats.chunks_freed += 1;
	stats.chunks_allocated += 1;
	hfree(prev); // this is just using free and hmalloc
    return hmalloc(bytes);

}
