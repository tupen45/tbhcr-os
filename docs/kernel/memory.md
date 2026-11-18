# Memory Management (`kmalloc`)

MyOS does not have virtual memory management or paging enabled for the kernel
heap — it uses a **single flat, identity-mapped 32-bit address space**. The
allocator in `kmalloc.c` provides a simple but effective scheme for a
freestanding kernel.

## Layout

```
HEAP_START = 0x01000000   (16 MiB)
HEAP_SIZE  = 1 MiB
  ┌────────────────────────────┐ 0x01000000
  │ 8 fixed-size block pools   │ (lower half)
  ├────────────────────────────┤
  │ bump allocator (large)     │ (upper half, starts at +512 KiB)
  └────────────────────────────┘ 0x01100000
```

## Fixed-size block pools

Eight pools cover power-of-two-ish sizes:

| Pool | Block size |
|------|-----------|
| 0 | 32 B |
| 1 | 64 B |
| 2 | 128 B |
| 3 | 256 B |
| 4 | 512 B |
| 5 | 1024 B |
| 6 | 2048 B |
| 7 | 4096 B |

Each pool is a singly-linked free list. `kmalloc_init()` carves the lower
half of the heap into `num_blocks = (HEAP_SIZE/2) / (NUM_POOLS * block_size)`
blocks per pool and pushes them onto the free list.

## Allocation algorithm

```c
void* kmalloc(size_t size) {
    int pool_idx = get_pool_index(size);   // smallest pool that fits
    if (pool_idx == -1) {
        // Large allocation -> simple bump allocator
        size = (size + 15) & ~15;          // 16-byte align
        void* ptr = large_alloc_ptr;
        large_alloc_ptr += size;
        if (large_alloc_ptr > HEAP_START + HEAP_SIZE) return NULL;
        return ptr;
    }
    if (free_lists[pool_idx] == NULL) return NULL;   // OOM for this size
    free_block_t* block = free_lists[pool_idx];
    free_lists[pool_idx] = block->next;
    return (void*)block;
}
```

- **Internal fragmentation:** small requests waste up to ~50% (you always get
  the next power-of-two pool).
- **No coalescing:** freed blocks simply return to their pool's free list.
- **Large allocations cannot be freed** — `kfree` only handles pool blocks;
  large pointers fall through the range check and are silently ignored.

## Freeing and aligned alloc

- `kfree(ptr)` determines the owning pool by checking the pointer against each
  pool's address range, then pushes the block back on that pool's free list.
- `kmalloc_aligned(size, alignment)` allocates `size + alignment - 1 +
  sizeof(void*)` extra, finds the aligned address, and stashes the original
  pointer just before it so `kfree_aligned()` can recover it.

## C standard-library compatibility

`kmalloc.c` also provides the symbols the C code (and mbedTLS) expect:

| Symbol | Behavior |
|--------|----------|
| `malloc(size)` | → `kmalloc(size)` |
| `free(ptr)` | → `kfree(ptr)` |
| `calloc(num,size)` | `kmalloc` then zero-fill |
| `abort()` | infinite loop (no panic screen yet) |
| `rand()` / `srand()` | LCG: `state = state*1103515245 + 12345`, returns `state >> 16` |

> **Gotcha:** `rand()` is seeded at `1` by default. DHCP uses it for the
> transaction ID (`g_xid`); games use ad-hoc LCGs seeded from framebuffer
> dimensions. None of these are cryptographically secure.

## Usage notes

- The ELF loader (`exec_elf.c`) uses `kmalloc` to allocate the program's
  address space; because everything shares one address space, the loaded
  user program runs in-place.
- NIC drivers (`rtl8139.c`, `e1000e.c`) use `kmalloc` for RX/TX DMA
  buffers. (In a real IOMMU/MMU system these would need physical
  contiguity guarantees; here virtual == physical for low memory.)

Next: [Graphics](graphics.md).
