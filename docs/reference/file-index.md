# Project File Index

A flat map of every source artifact in the repository root and what it does.
Generated for navigation; paired with the subsystem docs.

## Build & boot

| File | Role |
|------|------|
| `build.sh` | Full compile+link+ISO build pipeline (bash). |
| `Makefile.inc` | Opt-in `make` rules for the user program + embedding. |
| `fetch_mbedtls.sh` | (Helper) fetches the vendored mbedTLS tree. |
| `boot.asm` | Multiboot2 header, GDT, stack, `kmain` call. |
| `linker.ld` | LD script (1 MiB base; multiboot/text/rodata/data/bss). |
| `irqstubs.S` | AT&T-syntax IRQ/syscall stub (assembled via GCC). |
| `multiboot.h` | Multiboot2 tag/info structs. |
| `common.h` | Shared typedefs (`u8/u16/u32/u64`). |
| `io.h` | `inb/outb/inw/outw/inl/outl` port I/O inline asm. |
| `endian.h` | `htons/ntohs/htonl/ntohl` byte-swap helpers. |

## Kernel core

| File | Role |
|------|------|
| `kernel.c` | `kmain`, desktop, taskbar, start menu, all 10 apps. |
| `graphics.c` / `font.c` | Framebuffer drawing + 8×8 font. |
| `graphics.h` | Graphics API declarations. |
| `mouse.c` | PS/2 mouse init + packet decode. |
| `mouse.h` | Mouse API. |
| `kmalloc.c` | Fixed-pool + bump allocator, stdlib aliases. |
| `kmalloc.h` | Allocator API. |
| `syscalls.c` | `int 0x80` gate, IDT, console. |
| `syscalls.h` | Syscall API. |
| `exec_elf.c` | Minimal 32-bit ELF loader. |
| `exec_elf.h` | ELF loader API. |
| `string.c` / `string.h` | Freestanding string/mem routines. |
| `stdio.h` / `stdlib.h` | Minimal stdio/stdlib stubs. |

## Networking

| File | Role |
|------|------|
| `net.c` / `net.h` | Ethernet/ARP/IP/UDP core + interface registry. |
| `dhcp.c` / `dhcp.h` | DHCP client. |
| `dns.c` / `dns.h` | DNS A-record resolver + cache. |
| `tcp.c` / `tcp.h` | Single-socket TCP state machine. |
| `http.c` / `http.h` | HTTP/1.1 GET client (+ redirects). |
| `tls.h` / `tls_mbedtls.c` | HTTPS via vendored mbedTLS. |
| `tls_stub.c` | Stub when mbedTLS unavailable. |
| `util_net.h` | Shared networking helpers/macros. |
| `net_demo.c` | Standalone networking demo (not in main ISO path). |

## Drivers & hardware

| File | Role |
|------|------|
| `pci.c` / `pci.h` | PCI config-space access + device lookup. |
| `rtl8139.c` / `rtl8139.h` | Realtek RTL8139 IO-port NIC driver. |
| `drivers/e1000e.c` / `e1000e.h` | Intel e1000(e) MMIO NIC driver. |
| `drivers/usb/usb_host.c` / `.h` | USB host core + enumeration. |
| `drivers/usb/xhci.c` / `.h` | xHCI controller driver. |
| `drivers/usb/ehci.h` | EHCI defs + `usb_setup_packet_t`. |
| `drivers/usb/usb_rndis.c` / `.h` | Android RNDIS tether. |
| `drivers/usb/usb_ipheth.c` / `.h` | iPhone iPheth tether. |
| `drivers/usb/usb_cdc_ecm.c` / `.h` | CDC-ECM class driver. |

## Userland & data

| File | Role |
|------|------|
| `userprog/hello.c` | Sample ELF user program. |
| `userprog_blob.c` | Generated embedded blob (optional). |
| `userprog_hello.h` | `get_hello_ptr/len` declarations. |
| `json.c` / `json.h` | Bundled JSMN JSON parser. |
| `tools/embed_hello.sh` | Generates the embedded user-program blob. |

## Vendored

| Path | Role |
|------|------|
| `vendor/mbedtls/` | Vendored mbedTLS (TLS/crypto). |
| `vendor/.../config_mini.h` | Minimal mbedTLS config. |
| `vendor/.../platform_shim.c` | Freestanding platform hooks for mbedTLS. |

Continue from the [System Overview](../architecture/overview.md).
