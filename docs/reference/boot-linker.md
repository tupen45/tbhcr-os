# Boot & Linker Reference

Low-level artifacts that turn C into a bootable Multiboot2 kernel.

## `boot.asm`

| Section | Contents |
|---------|-----------|
| `.multiboot` | Multiboot2 header: magic `0xE85250D6`, checksum, framebuffer request (1450×1000). |
| `.text` | `_start`: `cli`, `lgdt`, load data selectors, set `ESP`, push `EBX`/`EAX`, `call kmain`, then `hlt` loop. |
| `.gdt` | Null + 32-bit code (`0x00CF9A000000FFFF`) + 32-bit data (`0x00CF92000000FFFF`). |
| `.bss` | 16 KiB stack (`resb 16384` → `stack_top`). |

Assembled with `nasm -f elf32 boot.asm -o boot.o`.

## `linker.ld`

```ld
ENTRY(_start)
SECTIONS {
  . = 1M;
  .multiboot : { KEEP(*(.multiboot)) }   ; must be first for GRUB
  .text      : { *(.text*) }
  .rodata    : { *(.rodata*) }
  .data      : { *(.data*) }
  .bss       : { *(.bss*) *(COMMON) }
}
```

- Load base **1 MiB** (`0x100000`) — above the typical BIOS/GRUB region.
- The `.multiboot` `KEEP` ensures GRUB finds the header at the image
  start.
- Linked via the compiler driver with
  `-Wl,-melf_i386 -Wl,-T,linker.ld -Wl,-z,max-page-size=0x1000`.

## `irqstubs.S`

Assembly (GAS/AT&T) providing the `irq80_stub` referenced by
`init_syscalls()`. It pushes the general registers and calls
`syscall_handler_c`. Built with `i686-elf-gcc -m32 -c irqstubs.S`.

## `multiboot.h`

Defines `multiboot_tag_t`, `multiboot_info_t`, and the framebuffer tag
(`type 8`). `init_graphics()` uses these to read `framebuffer_addr`,
`width`, `height`, `bpp`, and `pitch` from the boot tags.

## `io.h`

Inline port-I/O for x86:

```c
outb(port, val);  inb(port);     // byte
outw(port, val);  inw(port);     // word
outl(port, val);  inl(port);     // dword
```

Used by PS/2 (`mouse.c`), PCI (`pci.c`), and NIC drivers.

## `endian.h`

Byte-swap helpers used pervasively on the wire:
`htons`, `ntohs`, `htonl`, `ntohl`. All network headers are
little-endian on the wire but stored host-order in structs.

See also: [Boot Flow](../architecture/boot-flow.md).
