# Boot Flow

MyOS uses the **Multiboot2** specification so that GRUB can load the kernel
directly from an ISO. This page traces execution from power-on to the first
pixel being drawn.

## 1. The Multiboot2 header (`boot.asm`)

The assembly bootstrap defines a Multiboot2 header in its own section:

```asm
section .multiboot
align 8
multiboot2_header:
    dd 0xE85250D6          ; magic
    dd 0
    dd header_end - multiboot2_header
    dd -(0xE85250D6 + 0 + (header_end - multiboot2_header))  ; checksum
    dw 5, 0               ; type=5 (framebuffer), flags=0
    dd 24
    dd 1450, 1000         ; requested framebuffer 1450x1000
    ...
```

GRUB scans the kernel image for this magic, configures a linear framebuffer
(here requested at 1450×1000), and passes a Multiboot2 info pointer in `EBX`
plus the magic in `EAX`.

## 2. Global Descriptor Table and stack

`_start` immediately:

1. Clears interrupts (`cli`).
2. Loads a flat 32-bit GDT via `lgdt` — three descriptors: null, 32-bit code
   (`0x00CF9A000000FFFF`), and 32-bit data (`0x00CF92000000FFFF`).
3. Loads the data selector (`0x10`) into `DS/ES/FS/GS/SS`.
4. Sets `ESP` to `stack_top`, a 16 KiB `resb` area in `.bss`.

```asm
_start:
    cli
    lgdt [gdt_descr]
    mov ax, 0x10
    mov ds, ax
    ...
    mov esp, stack_top
    push ebx          ; multiboot info ptr
    push eax          ; magic
    call kmain
.hang:
    hlt
    jmp .hang
```

## 3. Entering C — `kmain`

`kmain(unsigned magic, unsigned addr)` (`kernel.c:1995`) receives the
Multiboot magic and info address. Its startup sequence is:

1. `init_graphics((void*)addr)` — walk the Multiboot2 tags, find the
   `FRAMEBUFFER` tag (type 8), and cache `framebuffer_addr`, `width`,
   `height`, `bpp`, and `pitch` globally. See [Graphics](kernel/graphics.md).
2. `init_mouse()` — enable the PS/2 auxiliary device and IRQ12. See
   [Input](kernel/input.md).
3. `usb_host_init()` followed by 8 `usb_poll()` iterations to detect
   already-connected devices.
4. If a USB network interface was found (RNDIS / iPhone / CDC-ECM), use it;
   otherwise fall back to a PCI NIC (`e1000e_init()`, then `rtl8139_init()`).
5. If a network interface is available, `dhcp_start()`.
6. Draw the cursor at screen center, then call `show_welcome()`.

## 4. The application loop

After the welcome screen's **Start** button is pressed, `kmain` enters an
infinite desktop loop:

```c
while (1) {
    usb_poll();
    net_poll();
    if (no network yet) try NIC again + dhcp_start();
    int app = show_desktop();
    switch (app) { ... launch the chosen application ... }
}
```

Each application (calculator, browser, ...) is a self-contained function that
draws a window, processes mouse/keyboard events, and returns when its close
button is clicked, handing control back to the desktop. This cooperative design
means there is no pre-emptive scheduler — concurrency is simulated by polling
`net_poll()` inside every event loop so packets keep flowing while the UI is
idle.

## Build-time boot artifact

`build.sh` assembles `boot.asm` with NASM, compiles `irqstubs.S` with GCC,
then links everything with `-Wl,-T,linker.ld`. The link script places the
multiboot header first so GRUB recognizes the binary:

```ld
ENTRY(_start)
SECTIONS {
  . = 1M;
  .multiboot : { KEEP(*(.multiboot)) }
  .text :     { *(.text*) }
  .rodata :   { *(.rodata*) }
  .data :     { *(.data*) }
  .bss :      { *(.bss*) *(COMMON) }
}
```

The resulting `kernel.bin` is copied to `iso/boot/` and wrapped into `myos.iso`
via `grub-mkrescue`.

Next: [Control Flow](control-flow.md).
