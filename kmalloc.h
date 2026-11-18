#pragma once

#include <stddef.h>

// Initialize the kernel memory allocator
void kmalloc_init();

// Allocate a block of memory
void* kmalloc(size_t size);

// Free a previously allocated block of memory
void kfree(void* ptr);

// Allocate a block of memory with a specific alignment
void* kmalloc_aligned(size_t size, size_t alignment);
void kfree_aligned(void* ptr);

// Standard library compatibility wrappers
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t num, size_t size);

// Other stubs that were in kmalloc_stub.c
void abort(void);
int rand(void);
void srand(unsigned int seed);
