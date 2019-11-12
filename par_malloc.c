

#include <stdlib.h>
#include <unistd.h>

#include "xmalloc.h"
#include "hmalloc.h"


void*
xmalloc(size_t bytes)
{
    //return opt_malloc(bytes);
    return hmalloc(bytes);
}

void
xfree(void* ptr)
{
    //opt_free(ptr);
	hfree(ptr);
}

void*
xrealloc(void* prev, size_t bytes)
{
    //return opt_realloc(prev, bytes);
	return hrealloc(prev, bytes);
}

