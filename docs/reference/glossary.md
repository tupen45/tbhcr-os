# Glossary

A quick reference for terms used throughout this documentation.

| Term | Definition |
|------|------------|
| **Multiboot2** | Boot standard (GRUB) that loads `kernel.bin` and provides a framebuffer + info tags. |
| **GRUB** | Boot loader that reads `grub.cfg` and multiboots the kernel from the ISO. |
| **GDT** | Global Descriptor Table — x86 segmentation descriptors; MyOS uses a flat 32-bit code/data pair. |
| **Protected mode** | 32-bit x86 mode (ring 0) MyOS runs in. No long mode / 64-bit. |
| **Freestanding** | C compiled without a hosted libc (no `printf`/`malloc` from the OS); MyOS supplies its own. |
| **Framebuffer** | Linear pixel buffer (32 bpp) the kernel draws into directly. |
| **NIC** | Network Interface Card — `e1000`/`rtl8139` in QEMU. |
| **Ethernet / ARP** | Link layer (MAC) + Address Resolution Protocol to map IP↔MAC. |
| **IPv4 / UDP / TCP** | Internet-layer and transport protocols implemented in `net.c`/`tcp.c`. |
| **DHCP** | Dynamic Host Configuration Protocol — auto IP/gateway/DNS lease. |
| **DNS** | Domain Name System — resolves hostnames to IPv4 (A records). |
| **HTTP / HTTPS** | Web protocols; HTTPS adds TLS (via mbedTLS). |
| **TLS** | Transport Layer Security — encrypted transport; here wrapped by mbedTLS. |
| **mbedTLS** | Vendored TLS/crypto library used for HTTPS. |
| **USB EHCI / xHCI** | USB 2.0 / 3.0 host controllers. EHCI is the kernel's primary path. |
| **RNDIS** | Remote NDIS — Android USB tethering network class. |
| **iPheth** | Apple iPhone USB Ethernet tether driver. |
| **CDC-ECM** | USB CDC Ethernet Control Model class. |
| **PCI** | Peripheral Component Interconnect — bus used to find NIC/USB devices. |
| **BAR** | Base Address Register — PCI register locating a device's IO/MMIO region. |
| **kmalloc** | MyOS's fixed-pool + bump kernel allocator. |
| **Syscall / `int 0x80`** | Software interrupt the user program uses to call `write`/`exit`. |
| **IDT** | Interrupt Descriptor Table — maps interrupt vectors to handlers. |
| **ELF (ET_DYN)** | Executable and Linkable Format; MyOS loads 32-bit PIE/PIE objects. |
| **JSMN** | Minimal JSON parser bundled as `json.h`. |
| **net_poll** | The single network heartbeat called in every UI loop. |
| **slirp** | QEMU user-mode networking; default guest subnet `10.0.2.0/24`. |
| **PIE** | Position-Independent Executable — required for loaded user programs. |

Next: [Troubleshooting](troubleshooting.md).
