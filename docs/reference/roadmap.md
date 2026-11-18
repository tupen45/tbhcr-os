# Roadmap & Known Limitations

An honest status page for the project. MyOS is a teaching/research OS;
many subsystems are intentionally "good enough for the demo."

## What works today

- Boots via GRUB/Multiboot2 under QEMU (`qemu-system-i386`).
- Graphical desktop: welcome, taskbar, start menu, 10 apps.
- PS/2 mouse + keyboard input with a software cursor.
- Real networking: Ethernet/ARP, DHCP, DNS, TCP, HTTP, HTTPS (mbedTLS).
- NIC drivers: Intel e1000(e) (MMIO) and Realtek RTL8139 (IO-port).
- USB tethering: RNDIS (Android), iPheth (iPhone), CDC-ECM.
- In-kernel ELF (PIE) loader + `int 0x80` syscall gate + sample program.
- JSON parsing (JSMN) used by the JSON Viewer.

## Known limitations

### Kernel / memory
- No paging, no ASLR, no user/kernel memory separation.
- Single flat address space; the "user" program runs in ring 0.
- `kmalloc` large allocations are never freed; no coalescing.

### Scheduling / I/O
- No pre-emptive scheduler, no threads, no timers.
- Entirely polling-based; no real interrupt handlers in the active path.
- No filesystem; only embedded/optional user blob.

### Networking
- TCP: single socket, no retransmit/RTO, no listen, no window scaling.
- DHCP: slirp fallback only; no lease renewal/rebind.
- DNS: no CNAME chasing, no TCP fallback, tiny non-expiring cache.
- HTTP: body truncated to ~8 KiB; limited redirect handling.
- TLS: **certificate verification disabled**, **toy LFSR entropy** — not
  secure; no SNI-dependent cert validation, no resumption.

### Drivers
- USB: EHCI preferred; xHCI secondary; hub traversal limited.
- PCI: legacy only, single domain, no bridge traversal.
- No SATA/disk, no audio, no multi-core/SMP.

## Suggested next steps

1. Enable **paging** + a real user/kernel split (would make the syscall
   gate a true boundary).
2. Add a **timer + pre-emptive scheduler** and turn apps into processes.
3. Harden **TCP** (timers, retransmit, listen) and add a **listen()**
   server path.
4. Turn on **TLS certificate verification** + a real hardware RNG.
5. Add a **filesystem** (e.g. initrd/tmpfs) so user programs can load
   from disk.
6. Improve **USB** hub support and make xHCI first-class.

## Project context

This is a **North Bengal University** capstone/research OS. The branding
("North Bengal University OS", "NBU Project Console") appears throughout
the UI. The goal is demonstrable, readable systems behavior rather than
production hardening.

Return to [System Overview](../architecture/overview.md).
