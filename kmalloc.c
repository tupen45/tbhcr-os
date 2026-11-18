// A simple fixed-size block memory allocator for newos.

#include "kmalloc.h"
#include <stdint.h>
#include <stddef.h>

#define HEAP_START ((uint8_t*)0x01000000) // 16MB start of heap
#define HEAP_SIZE  (4 * 1024 * 1024)      // 4MB heap size

// Block sizes: 32, 64, 128, 256, 512, 1024, 2048, 4096 bytes
#define NUM_POOLS 8
#define MAX_BLOCK_SIZE 4096

// --- Data Structures ---
typedef struct free_block {
    struct free_block* next;
} free_block_t;

static free_block_t* free_lists[NUM_POOLS];
static uint8_t* pool_starts[NUM_POOLS];
static size_t pool_sizes[NUM_POOLS] = { 32, 64, 128, 256, 512, 1024, 2048, 4096 };
static uint8_t* large_alloc_ptr;

// Helper to get the pool index for a given size
static int get_pool_index(size_t size) {
    for (int i = 0; i < NUM_POOLS; ++i) {
        if (size <= pool_sizes[i]) {
            return i;
        }
    }
    return -1; // Too large for any pool
}

// --- Allocator Initialization ---
void kmalloc_init() {
    uint8_t* heap_ptr = HEAP_START;
    large_alloc_ptr = HEAP_START + HEAP_SIZE / 2; // Dedicate half the heap to large allocs

    // Initialize each pool
    for (int i = 0; i < NUM_POOLS; ++i) {
        free_lists[i] = NULL;
        pool_starts[i] = heap_ptr;
        size_t block_size = pool_sizes[i];
        
        // Divide the pool area into blocks and add to the free list
        // Allocate a portion of the heap to each pool
        int num_blocks = (HEAP_SIZE / 2) / (NUM_POOLS * block_size);
        
        for (int j = 0; j < num_blocks; ++j) {
            free_block_t* block = (free_block_t*)heap_ptr;
            block->next = free_lists[i];
            free_lists[i] = block;
            heap_ptr += block_size;
        }
    }
}

// --- Allocation ---
void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    int pool_idx = get_pool_index(size);
    
    // Handle large allocations with a simple bump allocator
    if (pool_idx == -1) {
        // Round up to 16-byte boundary
        size = (size + 15) & ~15;
        void* ptr = large_alloc_ptr;
        large_alloc_ptr += size;
        if (large_alloc_ptr > HEAP_START + HEAP_SIZE) {
            return NULL; // Out of memory for large allocations
        }
        return ptr;
    }

    // Allocate from a pool
    if (free_lists[pool_idx] == NULL) {
        // In a real scenario, we might try to get memory from the OS
        // or a larger pool. For now, we fail.
        return NULL; // Out of memory for this block size
    }

    free_block_t* block = free_lists[pool_idx];
    free_lists[pool_idx] = block->next;
    return (void*)block;
}

// --- Freeing ---
void kfree(void* ptr) {
    if (ptr == NULL) return;

    // Figure out which pool this block belongs to by checking address range
    for (int i = 0; i < NUM_POOLS; i++) {
        uintptr_t p = (uintptr_t)ptr;
        uintptr_t pool_start = (uintptr_t)pool_starts[i];
        size_t block_size = pool_sizes[i];
        int num_blocks = (HEAP_SIZE / 2) / (NUM_POOLS * block_size);
        uintptr_t pool_end = pool_start + num_blocks * block_size;

        if (p >= pool_start && p < pool_end) {
            free_block_t* block = (free_block_t*)ptr;
            block->next = free_lists[i];
            free_lists[i] = block;
            return;
        }
    }
    // If we get here, it was a large allocation or an invalid pointer.
    // The large allocation scheme doesn't support freeing, so we do nothing.
}

// --- Aligned Allocation ---
void* kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL; // Invalid alignment
    }

    // Allocate extra memory to allow for alignment
    size_t extra = alignment - 1 + sizeof(void*);
    void* p1 = kmalloc(size + extra);
    if (p1 == NULL) return NULL;
    
    // Find the aligned address
    void* p2 = (void*)(((uintptr_t)p1 + extra) & ~(alignment - 1));
    
    // Store the original pointer just before the aligned address
    *((void**)p2 - 1) = p1;
    
    return p2;
}

void kfree_aligned(void* ptr) {
    if (ptr == NULL) return;
    // Retrieve the original pointer and free it
    void* p1 = *((void**)ptr - 1);
    kfree(p1);
}


// --- Standard Library Compatibility ---
void* malloc(size_t size) {
    return kmalloc(size);
}

void free(void* ptr) {
    kfree(ptr);
}

void* calloc(size_t num, size_t size) {
    size_t total_size = num * size;
    void* ptr = kmalloc(total_size);
    if (ptr) {
        // Simple memset implementation for now
        char* p = (char*)ptr;
        for (size_t i = 0; i < total_size; ++i) {
            p[i] = 0;
        }
    }
    return ptr;
}

// --- Other Stubs ---
void abort(void) {
    // A real abort would halt the system
    for (;;);
}

static unsigned int rng_state = 1;

int rand(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (int)(rng_state >> 16);
}

void srand(unsigned int seed) {
    rng_state = seed ? seed : 1;
}
