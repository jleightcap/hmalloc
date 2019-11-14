

#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <math.h>


#include "xmalloc.h"
//#include "hmem.h"

typedef struct list_node {
   size_t size;
   struct list_node* next;
} list_node;

//const size_t PAGE_SIZE = 4096;
const size_t PAGE_SIZE = 65536; // NOT A PAGE!
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

// mutex
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

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
    int bucket = log(size)/log(2) - 6;
    if (bucket < 0)
    {
        bucket = 0;
    }
    return ceil(bucket);
}

static
int
conv_bucket_size(int bucket)
{
    return 1 << (bucket + 6);
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

    for (int offset = 0; offset < PAGE_SIZE; )
    {
        list_node* new_node = (list_node*)(new_space + offset);
        new_node->size = bucket_true_space;
        // TODO: be careful around this
        offset += bucket_true_space; // don't have to calculate it twice this way
        new_node->next = (list_node*)(new_space + offset);
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
    //pthread_mutex_lock(&lock);

    // handle mapping for large chunks
    if (bytes > PAGE_SIZE)
    {
        //mmap the entire page.
        //pthread_mutex_unlock(&lock);
        return hmalloc_large(true_bytes);
    }

    int bucket = conv_size_bucket(true_bytes);

    //void* mem_addr = get_free_chunk(size);

    if(!heads[bucket])
    {
        fill_bucket(bucket);
    }

    void* mem_addr = heads[bucket];
    heads[bucket] = heads[bucket]->next;

    ((list_node*)mem_addr)->size = conv_bucket_size(bucket);

    //pthread_mutex_unlock(&lock);
    return mem_addr + sizeof(size_t);
}

void
xfree(void* item)
{
    //pthread_mutex_lock(&lock);

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
        //free_list_insert(chunk);
        int bucket = conv_size_bucket(chunk->size);
        chunk->next = heads[bucket];
        heads[bucket] = chunk;
    }

    //pthread_mutex_unlock(&lock);
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
        xfree(chunk);
        return 0;
    }
    else if (true_bytes == chunk->size)
    {
        // do nothing
        return chunk;
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

        // return old pointer
        return chunk;
    }
    else
    {
        // we need more space than we have
        void* new_mem = xmalloc(bytes);
        memcpy(new_mem, chunk, (chunk->size - sizeof(size_t)));
        xfree(chunk);
        return new_mem;
    }
}
