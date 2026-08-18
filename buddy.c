#include "buddy.h"
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#define PAGE_SIZE 4096
#define MAX_RANK 16

typedef struct free_block {
    struct free_block *next;
    struct free_block *prev;
} free_block_t;

static void *base_addr = NULL;
static int total_pages = 0;
static free_block_t *free_lists[MAX_RANK + 1];
static int *ranks = NULL; // ranks[i] = r (allocated), ranks[i] = -r (free), ranks[i] = 0 (none)
static int free_counts[MAX_RANK + 1];

static void add_free_block(int rank, int page_idx) {
    free_block_t *block = (free_block_t *)((char *)base_addr + (size_t)page_idx * PAGE_SIZE);
    block->prev = NULL;
    block->next = free_lists[rank];
    if (free_lists[rank]) {
        free_lists[rank]->prev = block;
    }
    free_lists[rank] = block;
    ranks[page_idx] = -rank;
    free_counts[rank]++;
}

static void remove_free_block(int rank, int page_idx) {
    free_block_t *block = (free_block_t *)((char *)base_addr + (size_t)page_idx * PAGE_SIZE);
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        free_lists[rank] = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    ranks[page_idx] = 0;
    free_counts[rank]--;
}

int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;
    
    if (ranks) free(ranks);
    ranks = (int *)calloc(pgcount, sizeof(int));
    
    memset(free_lists, 0, sizeof(free_lists));
    memset(free_counts, 0, sizeof(free_counts));

    int current_offset = 0;
    int remaining_pages = pgcount;
    for (int r = MAX_RANK; r >= 1; r--) {
        int block_size = 1 << (r - 1);
        while (remaining_pages >= block_size) {
            add_free_block(r, current_offset);
            current_offset += block_size;
            remaining_pages -= block_size;
        }
    }
    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) return ERR_PTR(-EINVAL);

    int target_rank = rank;
    int r = rank;
    while (r <= MAX_RANK && !free_lists[r]) {
        r++;
    }

    if (r > MAX_RANK) return ERR_PTR(-ENOSPC);

    free_block_t *block = free_lists[r];
    int page_idx = ((char *)block - (char *)base_addr) / PAGE_SIZE;
    remove_free_block(r, page_idx);

    while (r > target_rank) {
        r--;
        int block_size = 1 << (r - 1);
        int buddy_idx = page_idx + block_size;
        add_free_block(r, buddy_idx);
    }

    ranks[page_idx] = target_rank;
    return (void *)block;
}

int return_pages(void *p) {
    if (!p) return -EINVAL;
    if ((char *)p < (char *)base_addr || (char *)p >= (char *)base_addr + (size_t)total_pages * PAGE_SIZE) return -EINVAL;
    
    int page_idx = ((char *)p - (char *)base_addr) / PAGE_SIZE;
    if (page_idx * PAGE_SIZE != ((char *)p - (char *)base_addr)) return -EINVAL;
    
    int rank = ranks[page_idx];
    if (rank <= 0) return -EINVAL;
    
    ranks[page_idx] = 0;
    
    int current_rank = rank;
    int current_idx = page_idx;
    
    while (current_rank < MAX_RANK) {
        int block_size = 1 << (current_rank - 1);
        int buddy_idx = current_idx ^ block_size;
        
        if (buddy_idx >= total_pages || ranks[buddy_idx] != -current_rank) {
            break;
        }
        
        remove_free_block(current_rank, buddy_idx);
        current_idx = current_idx & ~block_size;
        current_rank++;
    }
    
    add_free_block(current_rank, current_idx);
    return OK;
}

int query_ranks(void *p) {
    if (!p) return -EINVAL;
    if ((char *)p < (char *)base_addr || (char *)p >= (char *)base_addr + (size_t)total_pages * PAGE_SIZE) return -EINVAL;
    
    int page_idx = ((char *)p - (char *)base_addr) / PAGE_SIZE;
    if (page_idx * PAGE_SIZE != ((char *)p - (char *)base_addr)) return -EINVAL;

    if (page_idx >= total_pages) return -EINVAL;
    
    for (int r = MAX_RANK; r >= 1; r--) {
        int block_size = 1 << (r - 1);
        int start_idx = page_idx & ~(block_size - 1);
        if (start_idx < total_pages) {
            int val = ranks[start_idx];
            if (val == r || val == -r) {
                return r;
            }
        }
    }
    
    return -EINVAL;
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) return -EINVAL;
    return free_counts[rank];
}
