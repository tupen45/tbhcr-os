# Memory Model & Address Space

MyOS uses a **single, flat, identity-mapped 32-bit address space**. There
is no paging, no segmentation-based protection, and no separate user/kernel
address ranges. This page explains what that means and where things live.

## The flat space

```
0x00000000  ┌─────────────────────────┐
           │ (low memory / BIOS / GRUB) │
0x00001000 ├─────────────────────────┤
           │ kernel image (text/rodata/  │  loaded by GRUB at 1 MiB
           │  data/bss) via linker.ld   │
0x00100000 ├─────────────────────────┤
           │ kmalloc heap (1 MiB)       │  HEAP_START = 0x01000000
           │  ├ 8 fixed pools (lower)  │  up to HEAP_SIZE-512KiB
           │  └ bump region (upper)     │  large_alloc_ptr
0x01100000 ├─────────────────────────┤
           │ framebuffer (MMIO)         │  framebuffer_addr from MB2
           │ NIC/USB MMIO + DMA bufs   │  kmalloc'd low (virt==phys)
           │ ...free...                 │
0xFFFFFFFF └─────────────────────────┘
```

## Why identity mapping works here

- `kmalloc` allocates from a static 1 MiB window at `0x01000000`.
- NIC DMA buffers (`rtl8139.c`, `e1000e.c`) are `kmalloc`'d, and the
  `VIRT_TO_PHYS` macro is identity (`(uint32_t)(uintptr_t)v`) because
  the kernel is loaded below 4 GiB with no IOMMU/MMU remap.
- The framebuffer address comes straight from the Multiboot2 tag and is
  written as if it were normal RAM.

In a production OS you would enable paging, give each process its own
address space, and IOMMU-map DMA. MyOS deliberately skips all of that.

## Stack

The boot stack is a 16 KiB `resb` region in `.bss` (`boot.asm`),
topped by `ESP = stack_top` before `kmain`. There is one stack for the
whole kernel (no per-thread stacks).

## The heap (`kmalloc.c`)

Covered in detail in [Memory](kernel/memory.md). Summary:

- 8 fixed-size pools (32 B → 4096 B) carve the **lower** half.
- A bump allocator owns the **upper** half for large (>4096 B) allocations
  that can never be freed.
- `malloc`/`free`/`calloc`/`rand`/`srand`/`abort` are provided here
  for the C code and mbedTLS.

## Implications for user programs

Because there is no per-process memory, the [ELF loader](userland/elf-loader.md)
loads a user program into the **same** heap and jumps to it in ring 0. The
program must be a PIE (`ET_DYN`) and may only use the `int 0x80`
[syscalls](userland/syscalls.md). It shares the entire address space with the
kernel — a stray write can corrupt the OS.

See also: [Boot Flow](../architecture/boot-flow.md),
[Control Flow](../architecture/control-flow.md).
