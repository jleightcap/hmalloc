
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include "hmalloc.h" 
typedef struct list_node {
   size_t size;
   struct list_node* next;
} list_node;

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
static list_node* free_list = 0;

long
free_list_length()
{
    int len = 0;
    list_node* curr = free_list;
    while(curr)
    {
        len++;
        curr = curr->next;
    }
    return len;
}

static
int
is_sorted()
{
    for (list_node* curr = free_list; curr; curr = curr->next)
    {
        if (!curr->next)
        {
            return 1;
        }
        if (curr > curr->next)
        {
            return 0;
        } 
    }
    return 1;
}

static
void
print_flist()
{
    printf("==================== This is a newline ========================\n");
    printf("size of freelist: %ld\n", free_list_length());
    for (list_node* curr = free_list; curr; curr = curr->next) {
        printf("Size %ld, address %p, end address %p, next %p\n", curr->size, curr, (void*)curr + curr->size, curr->next);
    }
}

static
void
coalesce()
{
    list_node* curr = free_list;
    while(curr)
    {
        if((void*)curr + curr->size == (void*)curr->next)
        {
            curr->size = curr->size + curr->next->size;
            curr->next = curr->next->next;
            //coalesce();
        }
        else
        {
            curr = curr->next;
        }
    }
    if (!is_sorted())
    {
        print_flist();
        printf("Not sorted.\n");
        exit(1);
    }
}

static
void
free_list_insert(list_node* new)
{
    // we assume the new node has the correct size but no pointer
    // to next initialized
    list_node* curr = free_list;

    if (!curr) {
        // there's no free list, new is the free list now
        new->next = 0;
        free_list = new;
    }
    else if (new < curr) {
        // insert the new item at the head
        new->next = curr;
        free_list = new;
    }
    else {
        while(1) {
            // either the next doesn't exist or the new fits between
            // the current and the next of the current
            if(!curr->next || (new > curr && new < curr->next)) {
                new->next = curr->next;
                curr->next = new;
                return;
            }
            // increment
            curr = curr->next;
        }
    }
}

static
void
add_page()
{
    // maps a new page
    void* mem_addr = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

    if((long)mem_addr == -1)
    {
        perror("mapping new page");
    }

    // call the new page a node
    list_node* new_chunk = (list_node*)mem_addr;
    // set its size
    new_chunk->size = PAGE_SIZE;
    new_chunk->next = 0;
    // let the insert function set its pointer
    free_list_insert(new_chunk);

    stats.pages_mapped += 1;
}

static
list_node*
get_free_chunk(size_t size)
{

    list_node* curr_node = free_list;
    list_node* prev_node = 0;
    list_node* behind_best = 0;

    // so this is a good flag value
    list_node* best_chunk = 0;

/*
    while(curr_node)
    {
        if(curr_node->size >= size && curr_node->size < min_size)
        {
            min_size = curr_node->size;
            best_chunk = curr_node;
            behind_best = prev_node;
        }

        prev_node = curr_node;
        curr_node = curr_node->next;
    }
    */
    while(curr_node)
    {
        if(curr_node->size >= size)
        {
            best_chunk = curr_node;
            behind_best = prev_node;
            break;
        }

        prev_node = curr_node;
        curr_node = curr_node->next;
    }

    // if we didn't find one large enough, add another page to the free_list
    // and try again to get a free chunk
    if (!best_chunk) // if null pointer
    {
        add_page();
        return get_free_chunk(size);
    }

    // remove the chunk from the free list
    // before we return it to the user
    if (behind_best) {
        behind_best->next = best_chunk->next;
    }
    else {
        free_list = best_chunk->next;
    }

    // BUG
    //
    long excess_amt = best_chunk->size - size;

    // return excess to free list, if there's enough space
    if (excess_amt > sizeof(list_node*) + sizeof(size_t))
    {
        best_chunk->size = size;
        void* next_free_addr = (void*)best_chunk + size;
        list_node* excess = (list_node*)next_free_addr;
        excess->size = excess_amt;
        excess->next = 0;
        free_list_insert(excess);
    }


    return best_chunk;
}



hm_stats*
hgetstats() {
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


static
void*
hmalloc_large(size_t size)
{
    // here, the size is the true size we need.

    int num_pages = div_up(size, PAGE_SIZE);

    // mmap enough pages for the big thing
    void* new_addr = mmap(NULL, num_pages * PAGE_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

    if((long)new_addr == -1)
    {
        perror("mapping new LARGE page");
    }

    list_node* new_chunk = (list_node*)new_addr;
    // set its size
    new_chunk->size = num_pages * PAGE_SIZE;
    new_chunk->next = 0;

    stats.pages_mapped += num_pages;

    return new_addr + sizeof(size_t);
}


void*
hmalloc(size_t size)
{
    stats.chunks_allocated += 1;

    size_t og_size = size;

    // get the actual size we need
    size += sizeof(size_t);
    if (size < (sizeof(list_node*) + sizeof(size_t)))
    {
        size = (sizeof(list_node*) + sizeof(size_t));
    }
    // we will only deal with this 'true' size from here on

    // handle mapping for large chunks
    if (size > PAGE_SIZE)
    {
        //mmap the entire page.
        return hmalloc_large(size);
    }

    void* mem_addr = get_free_chunk(size);

    return mem_addr + sizeof(size_t);
}

void
hfree(void* item)
{
    stats.chunks_freed += 1;

    list_node* chunk = (list_node*)(item - sizeof(size_t));

    // initialize the next pointer
    chunk->next = 0;

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

        stats.pages_unmapped += pages;
    }
    else
    {
        free_list_insert(chunk);
    }

    // coalesce it all
    coalesce();
}
