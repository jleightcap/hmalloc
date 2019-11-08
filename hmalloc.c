
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>

#include "hmalloc.h"

int test = 1; // a bad substitute for gdb

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
free_list *flist;

void
print_flist()
{
    for (free_list* curr = flist; curr != 0; curr = curr->next) {
        printf("Size %d, address %ld, end address %ld, next %ld\n", curr->size, curr, (void*)curr + curr->size + sizeof(size_t), curr->next);
    }
}

// a pointer nn pages of memory
void* 
make_page(int nn) 
{
    // if (test) { printf("%d page mapped\n", nn); }
    stats.pages_mapped += nn;
    return mmap(0, PAGE_SIZE * nn, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
}

long
free_list_length()
{
    long size = 0;
    for (free_list* curr = flist; curr; curr = curr->next) {
        size += 1;
    }
    return size;
}

// The first slot where size fits in free list.
// If no large enough slot in list, return null.
void* first_fit(size_t size) 
{
    for (free_list* curr = flist; curr; curr = curr->next) {
        // first slot with adequate size
        if (curr->size >= size + sizeof(size_t)) {
            // if (test) { printf("%ld > %ld, insert at %ld\n", curr->size, size + sizeof(size_t), curr); }
            return curr;
        }
    }
    // if not found return null
    return 0;
}

void 
coalesce() {
    // O(n)?
    // if (test) { printf("Coalesce...\n"); }
    for (free_list* curr = flist; curr; curr = curr->next) {
        if ((void*)curr + curr->size + sizeof(size_t) == (void*)curr->next) {
            // if (test) { printf("Combining...\n"); }
            long sum = curr->size + curr->next->size;
            curr->next = curr->next->next;
            curr->size = sum;
        }
    }
    // if (test) { print_flist(); }
}

void 
free_list_insert(free_list* added)
{
    // insert with sorted memory location
    if (!flist) {
        flist = added;
        return;
    }
    if ((size_t*)flist > (size_t*)added) {
        // if (test) { printf("Inserting at head...\n"); }
        added->next = flist;
        flist = added;
    }
    else {
        // if (test) { printf("Inserting...\n"); }
        for (free_list* curr = flist; curr; curr = curr->next) {
            if ((void*)curr < (void*)added) {
                added->next = curr->next;
                curr->next = added;
            }
        }
    }
    coalesce();
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

void*
hmalloc(size_t size)
/*
{
    stats.chunks_allocated += 1;
    size += sizeof(size_t);
    void* usr; // pointer to return to user
    if (test) { printf("malloc %d\n", size); }

    free_list* slot = first_fit(size);
    if (slot) {
        if (test) { printf("Found fit...\n"); }
        // small allocations, size in free list

        free_list* fragment = (void*)slot + size;
        fragment->size = slot->size - size;
        fragment->next = slot->next;
        slot->next = fragment;
        slot->size = size;
        
        if (test) { print_flist(); }

        usr = (void*)slot + sizeof(size_t);
    }
    else {
        // Allocation needing new page
        if (test) { printf("large...\n"); }

        long pages = div_up(size, PAGE_SIZE);
        free_list* added = make_page(pages);
        *(size_t*)added = size;

        if (test) { print_flist(); }

        usr = (void*)added + sizeof(size_t);
    }
    return usr; // return pointer to user.
}
*/
{
    stats.chunks_allocated += 1;
    size += sizeof(size_t);
    void* usr; // pointer to return to user
    // if (test) { printf("malloc %d\n", size); }

    free_list* slot = first_fit(size);
    if (slot) {
        // if (test) { printf("fragment...\n"); }

        if (slot == flist) {
            free_list* tmp = slot->next;
            free_list* add = ((void*)slot) + size;
            add->size = slot->size - size;
            add->next = tmp;
            flist = add;
        }
        else {
            // General insertion
        }

        // if (test) { print_flist(); }

        usr = (void*)slot + sizeof(size_t);
    }
    else {
        // Allocation needing new page
        // if (test) { printf("slot not found, new page...\n"); }

        long pages = div_up(size, PAGE_SIZE);
        free_list* added = make_page(pages);
        added->size = size;
        free_list* fragment = (void*)added + sizeof(size_t) + size;
        fragment->size = (pages * PAGE_SIZE) - size - 2 * sizeof(size_t);
        assert(added->size + fragment->size + 2*sizeof(size_t) == pages * PAGE_SIZE);
        if (!flist) {
            flist = fragment;
            // if (test) { print_flist(); }
        }
        else {
            free_list_insert((void*)fragment);
        }

        usr = (void*)added + sizeof(size_t);
    }
    return usr; // return pointer to user.
}

void
hfree(void* item)
{
    stats.chunks_freed += 1;

    free_list* slot = item - sizeof(size_t);
    size_t size = *((size_t*)slot);
    // if (test) { printf("free %d\n", size); }

    if (size < PAGE_SIZE) {
        // if (test) { printf("freeing small...\n"); }
        // insert slot into flist
        slot->size = size;
        slot-> next = 0;
        free_list_insert(slot);
    }
    else {
        long pages = div_up(size, PAGE_SIZE);
        stats.pages_unmapped += pages;
        // if (test) { printf("freeing large, %ld pages...\n", pages); }
        munmap(slot, (pages - 1) * PAGE_SIZE);
        // if (test) { print_flist(); }
    }
}

