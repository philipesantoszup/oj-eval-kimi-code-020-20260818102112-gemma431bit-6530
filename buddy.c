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
static int *ranks = NULL; // Stores rank for every page. 0 = unallocated/unknown
static int free_counts[MAX_RANK + 1];

static void add_free_block(int rank, int page_idx) {
    free_block_t *block = (free_block_t *)((char *)base_addr + (size_t)page_idx * PAGE_SIZE);
    block->prev = NULL;
    block->next = free_lists[rank];
    if (free_lists[rank]) {
        free_lists[rank]->prev = block;
    }
    free_lists[rank] = block;
    
    int block_size = 1 << (rank - 1);
    for (int i = page_idx; i < page_idx + block_size && i < total_pages; i++) {
        ranks[i] = rank;
    }
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
    
    int block_size = 1 << (rank - 1);
    for (int i = page_idx; i < page_idx + block_size && i < total_pages; i++) {
        ranks[i] = 0;
    }
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

    int size = 1 << (target_rank - 1);
    for (int i = page_idx; i < page_idx + size && i < total_pages; i++) {
        ranks[i] = target_rank;
    }
    return (void *)block;
}

int return_pages(void *p) {
    if (!p) return -EINVAL;
    if ((char *)p < (char *)base_addr || (char *)p >= (char *)base_addr + (size_t)total_pages * PAGE_SIZE) return -EINVAL;
    
    int page_idx = ((char *)p - (char *)base_addr) / PAGE_SIZE;
    if (page_idx * PAGE_SIZE != ((char *)p - (char *)base_addr)) return -EINVAL;
    
    int rank = ranks[page_idx];
    if (rank == 0) return -EINVAL;
    
    // Since it was allocated, we need to find the start of the block to free it.
    // The pointer p returned by alloc_pages is always the start of the block.
    // So page_idx is the start_idx.
    
    int block_size = 1 << (rank - 1);
    for (int i = page_idx; i < page_idx + block_size && i < total_pages; i++) {
        ranks[i] = 0;
    }
    
    int current_rank = rank;
    int current_idx = page_idx;
    
    while (current_rank < MAX_RANK) {
        int current_block_size = 1 << (current_rank - 1);
        int buddy_idx = current_idx ^ current_block_size;
        
        if (buddy_idx >= total_pages || ranks[buddy_idx] != current_rank) {
            break;
        }
        
        // The buddy is free and has the same rank.
        // Note: our ranks array already stores the rank if it's free.
        // But we must remove it from the free list.
        // Since buddy_idx is the start of the buddy block, it should be in free_lists[current_rank].
        
        // We need to make sure buddy_idx is indeed the START of a free block of rank current_rank.
        // Because buddy_idx might be in the middle of a larger free block.
        // But the buddy algorithm ensures that if the buddy is free and has the same rank,
        // it must be a block of that rank.
        
        // However, we only store rank for every page. To remove from free_list,
        // we need the buddy_idx to be the start of the block.
        // In Buddy system, buddy_idx is always the start of the buddy block.
        
        remove_free_block(current_rank, buddy_idx);
        current_idx = current_idx & ~current_block_size;
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
    
    return ranks[page_idx];
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) return -EINVAL;
    return free_counts[rank];
}
