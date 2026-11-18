# MyOS — North Bengal University Operating System

Welcome to the developer and reference documentation for **MyOS**, a from-scratch,
32-bit, graphical operating system written in C and assembly for the `i686-elf`
target. MyOS boots with Multiboot2, enumerates USB and PCI hardware, obtains an
IP address over DHCP, resolves DNS, fetches real web content over HTTP/HTTPS, and
presents a mouse/keyboard driven desktop with a calculator, web browser, JSON
viewer, text editor, several games, and a grocery shop demo.

This documentation set contains **50+ pages** covering every subsystem:

- **Overview** — what MyOS is, its history, and design philosophy
- **Architecture** — how the kernel is layered and how control flows
- **Build & Run** — toolchain, build script, QEMU invocation, USB passthrough
- **Kernel Subsystems** — boot, memory, graphics, input, syscalls, ELF loader
- **Networking** — Ethernet/ARP, DHCP, DNS, TCP, HTTP, TLS
- **Drivers** — USB host stack, NIC drivers, PCI
- **Applications** — the desktop, start menu, and every bundled app
- **Reference** — API references, glossary, troubleshooting

## Quick links

| Area | Entry point |
|------|-------------|
| Getting started | [Installation & Build](getting-started/build.md) · [Running in QEMU](getting-started/running.md) |
| Architecture | [System Overview](architecture/overview.md) · [Boot Flow](architecture/boot-flow.md) · [Control Flow](architecture/control-flow.md) |
| Kernel | [Memory (kmalloc)](kernel/memory.md) · [Graphics](kernel/graphics.md) · [Input](kernel/input.md) |
| Networking | [Network Core](networking/network-core.md) · [DHCP](networking/dhcp.md) · [DNS](networking/dns.md) · [TCP](networking/tcp.md) · [HTTP](networking/http.md) · [TLS](networking/tls.md) |
| Drivers | [USB Host](drivers/usb-host.md) · [NIC Drivers](drivers/nic-drivers.md) · [PCI](drivers/pci.md) |
| Userland | [Syscalls](userland/syscalls.md) · [ELF Loader](userland/elf-loader.md) · [User Programs](userland/user-programs.md) |
| Apps | [Desktop & Shell](apps/desktop.md) · [App Catalog](apps/app-catalog.md) |
| Reference | [Glossary](reference/glossary.md) · [Troubleshooting](reference/troubleshooting.md) |

## At a glance

| Property | Value |
|----------|-------|
| Target architecture | `i686` (32-bit x86, protected mode) |
| Toolchain | `i686-elf-gcc` cross-compiler, NASM, GNU ld, grub-mkrescue |
| Boot standard | Multiboot2 (GRUB) → `kernel.bin` on a bootable ISO |
| Language | C (freestanding) + x86 assembly |
| TLS library | mbedTLS (vendored, minimal config) |
| Display | Linear framebuffer (32 bpp) from Multiboot2 framebuffer tag |
| Network | Realtek RTL8139 / Intel e1000e NICs + USB RNDIS / iPhone tether |
| License | Educational / project use |

> **Note on scope:** MyOS is a university capstone / research project. It deliberately
> values readability and demonstrable behavior over production hardening. Several
> subsystems (e.g. the heap, TLS entropy, USB hub traversal) are intentionally
> simplified "good enough for the demo" implementations.

Continue to [System Overview](architecture/overview.md).
