# System Overview

MyOS is organized as a single freestanding 32-bit C kernel plus a small amount
of assembly. There is no separate user-space process scheduler — the kernel runs
all code in ring 0 and the graphical shell is a cooperative, event-loop driven
state machine. This keeps the design approachable while still demonstrating real
operating-system primitives: hardware enumeration, DMA-capable NIC drivers, a
working IPv4/TCP/TLS stack, and an in-kernel ELF loader that runs position
independent user programs via a syscall gate.

## Design goals

1. **Boot on commodity virtual hardware (QEMU).** The OS must boot from a
   GRUB ISO under `qemu-system-i386` with an `e1000` or `rtl8139` NIC and a
   USB controller.
2. **Show real networking.** The browser and JSON viewer perform live DNS
   resolution and HTTP/HTTPS GETs to public endpoints
   (`jsonplaceholder.typicode.com`).
3. **Demonstrate a GUI desktop.** A welcome screen, desktop, taskbar, start
   menu, and ten applications driven by mouse and keyboard.
4. **Be a teaching artifact.** Each subsystem is isolated into its own `.c/.h`
   pair with a clear, single-responsibility API.

## High-level component map

```
                         +-----------------------------+
                         |         kernel.c           |
                         |  (kmain, desktop, apps)  |
                         +------+----------------------+
                                | calls
        +-----------------------+---------------------------+
        |                       |                           |
 +------v------+      +--------v---------+      +--------v----------+
 |  Graphics   |      |   Networking     |      |  USB / Drivers    |
 |  graphics.c |      | net/dhcp/dns/  |      | usb_host, xhci,   |
 |  font.c     |      | tcp/http/tls    |      | e1000e, rtl8139   |
 |  mouse.c    |      +--------+---------+      +--------+----------+
 +------+------+               |                           |
        |              +--------v---------+                 |
        |              |   PCI / kmalloc  |                 |
        +------------>|   syscalls/ELF  |<----------------+
                       +-----------------+
```

## Source tree

| Path | Responsibility |
|------|----------------|
| `boot.asm` | Multiboot2 header, GDT, stack setup, calls `kmain` |
| `linker.ld` | Linker script (1 MiB load base, multiboot/text/rodata/data/bss) |
| `kernel.c` | `kmain`, desktop, taskbar, start menu, all 10 applications |
| `graphics.c` / `font.c` | Framebuffer pixel/rect/text drawing, bitmap font |
| `mouse.c` | PS/2 mouse init + packet decoding |
| `kmalloc.c` | Fixed-pool + bump allocator, `malloc`/`free`/`calloc` |
| `net.c` | Ethernet/ARP/IP/UDP send-receive, interface registry |
| `dhcp.c` | DHCP client (discover/request/ack, slirp fallback) |
| `dns.c` | DNS A-record resolver + cache |
| `tcp.c` | Minimal single-socket TCP state machine |
| `http.c` | HTTP/1.1 GET client with redirect following |
| `tls_mbedtls.c` | HTTPS GET via vendored mbedTLS |
| `drivers/usb/*` | USB EHCI/xHCI host, RNDIS, iPhone tether, CDC-ECM |
| `drivers/e1000e.c`, `rtl8139.c` | NIC drivers (MMIO / IO-port) |
| `pci.c` | PCI config-space access and device lookup |
| `syscalls.c` | `int 0x80` gate, IDT, on-screen console |
| `exec_elf.c` | Minimal 32-bit ELF loader |
| `json.h` | JSMN JSON parser (vendored) |
| `vendor/mbedtls` | Vendored mbedTLS cryptographic/TLS library |

Continue to [Boot Flow](boot-flow.md).
