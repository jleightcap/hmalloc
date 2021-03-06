

#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <math.h>


#include "xmalloc.h"

typedef struct list_node {
   size_t size;
   struct list_node* next;
} list_node;

//const size_t PAGE_SIZE = 4096;
const size_t PAGE_SIZE = 65536; // more than a page
static hm_stats stats; // This initializes the stats to 0.

static __thread list_node* heads[11] = {0}; // buckets

/*
bucket documentation:
the smallest bucket is 64 bytes; 48 usable
this keeps increasing in powers of two up to
index 10: 2^16 - 8 = ?

how to get size from index:
true size: 2^(6 + ii)
user size: true_size - sizeof(list_node);

this is because 2^6 == 64, which we've chosen as the smallest
bucket.

*/

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

static
int
conv_size_bucket(size_t size)
{
    // find the index to be used
    int bucket = ceil(log(size)/log(2)) - 6;
    if (bucket < 0)
    {
        bucket = 0;
    }
    return bucket;
}

static
int
conv_bucket_size(int bucket)
{
    return 1 << (bucket + 6);
}

static
void
print_bucket(int ii)
{
    for (list_node* curr = heads[ii]; curr && curr->next; curr = curr->next) {
      printf("{%p}\n", curr);
    }
    printf("\n");
}

static
void
print_heads()
{
    for (int ii = 0; ii < 11; ++ii) {
      printf("head %p\n", heads[ii]);
      // print_bucket(ii);
    }
    printf("\n");
}

// fills the given bucket with chunks of the right size
static
void
fill_bucket(int bucket)
{
    size_t bucket_true_space = conv_bucket_size(bucket);

    void* new_space = mmap(NULL,
        PAGE_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC,
        MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if((long)new_space == -1)
    {
        perror("filling bucket");
    }

    int num_chunks = PAGE_SIZE / bucket_true_space;
    for (int ii = 0; ii < num_chunks; ii++)
    {
        list_node* new_node = (list_node*)(new_space + ii * bucket_true_space);
        new_node->size = bucket_true_space;
        if (ii == num_chunks - 1)
        {
             // last new_node has null pointer
             new_node->next = 0;
        }
        else
        {
            new_node->next = (list_node*)(new_space + (ii + 1) * bucket_true_space);
        }
    }
    heads[bucket] = (list_node*)new_space;
}

static
void*
hmalloc_large(size_t size)
{
    // here, the size is the true size we need.

    int num_pages = div_up(size, PAGE_SIZE);

    // mmap enough pages for the big thing
    void* new_addr = mmap(NULL,
        num_pages * PAGE_SIZE,
        PROT_READ|PROT_WRITE|PROT_EXEC,
        MAP_SHARED|MAP_ANONYMOUS,-1, 0);

    if((long)new_addr == -1)
    {
        perror("mapping new LARGE page");
    }

    list_node* new_chunk = (list_node*)new_addr;
    // set its size
    new_chunk->size = num_pages * PAGE_SIZE;
    new_chunk->next = 0;


    return new_addr + sizeof(size_t);
}


void*
xmalloc(size_t bytes)
{
    size_t true_bytes = bytes + sizeof(size_t);

    // handle mapping for large chunks
    if (bytes > PAGE_SIZE)
    {
        //mmap the entire page.
        return hmalloc_large(true_bytes);
    }

    int bucket = conv_size_bucket(true_bytes);

    if(!heads[bucket])
    {
        fill_bucket(bucket);
    }

    void* mem_addr = (void*)heads[bucket];
    heads[bucket] = heads[bucket]->next;

    ((list_node*)mem_addr)->size = conv_bucket_size(bucket);

    return mem_addr + sizeof(size_t);
}

void
xfree(void* item)
{
    list_node* chunk = (list_node*)(item - sizeof(size_t));

    //if larger than a page
    if (chunk->size > PAGE_SIZE)
    {
        int pages = div_up(chunk->size, PAGE_SIZE);
        //unmap the page divided up
        int rv = munmap(chunk, chunk->size);
        if (rv == -1)
        {
            perror("unmapping large page");
        }
    }
    else
    {
        int bucket = conv_size_bucket(chunk->size);
        chunk->next = heads[bucket];
        heads[bucket] = chunk;
    }

}

void*
xrealloc(void* prev, size_t bytes)
{
    list_node* chunk = (list_node*)(prev - sizeof(size_t));
    size_t true_bytes = bytes + sizeof(size_t);

    if (!chunk)
    {
        return xmalloc(bytes);
    }
    else if (bytes == 0)
    {
        xfree(prev);
        return 0;
    }
    else if (true_bytes == chunk->size)
    {
        // do nothing
        return prev;
    }
    else if (true_bytes < chunk->size)
    {
        /*
        // return the difference to the freelist
        size_t excess_amt = prev_size - true_bytes;
        void* excess_addr = (void*)prev + true_bytes;
        list_node* excess = (list_node*)excess_addr;
        excess->size = excess_amt;
        excess->next = 0;
        free_list_insert(excess);
        */

        // the unreliable xmalloc strikes again
        // you can't trust what it says
        // OooOOOOOOOOooooo

        // (this is for speed)

        // return old pointer
        return prev;
    }
    else
    {
        // we need more space than we have
        void* new_mem = xmalloc(bytes);
        memcpy(new_mem, prev, (chunk->size - sizeof(size_t)));
        xfree(prev);
        return new_mem;
    }
}
