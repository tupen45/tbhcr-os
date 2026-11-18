# C Coding Style & Conventions

Guidance for anyone editing the MyOS source, derived from the existing code.

## File layout

- One responsibility per `.c`/`.h` pair (e.g. `net.c` only does
  Ethernet/IP/UDP; `dhcp.c` only does DHCP).
- Public API lives in a matching `.h`; the kernel's big `kernel.c` is the
  exception (it holds the desktop + apps).
- Use `#pragma pack(push,1)` for all wire/on-the-wire structs.

## Naming

- Globals: `g_` prefix (`g_active_netif`, `g_sock`, `g_netif_list`).
- Static helpers: `lower_snake()` (`arp_lookup`, `dhcp_send_discover`).
- Types: `snake_t` or `t` suffix (`net_if`, `tcp_socket_t`,
  `jsmntok_t`).
- Macros: `UPPER_SNAKE` (`ETH_TYPE_IP`, `DHCP_MSG_ACK`).
- Magic numbers that matter get a named constant.

## Types & endianness

- Use `uint8_t/16_t/32_t` (`<stdint.h>`), not `int`/`long`, for
  wire fields and fixed layouts.
- Network headers are little-endian on the wire; convert with `htons/
  ntohs/htonl/ntohl` (`endian.h`). Store IPs host-order in structs.
- `u8/u16/u32/u64` are project typedefs from `common.h`.

## Control style

- Event loops: `while (1) { net_poll(); if (key) ...; if (mouse) ... }`.
- Always `cursor_restore_under()` before `return` from a UI function.
- Keep handlers short; mutate state, then repaint.

## Comments

The codebase is **sparingly commented** — names and structure carry
meaning. Add a comment when a non-obvious invariant (e.g. "ARP the
gateway, not the remote IP") would otherwise be missed.

## Build hygiene

- Compile with `-m32 -ffreestanding`. No hosted libc calls.
- If you need a standard function, add it to `string.c`/`kmalloc.c` or the
  stdlib stub — do not link the host libc.
- Keep `linker.ld` load base at 1 MiB; keep the kernel heap below/around
  16 MiB.

## Safety

This is a teaching OS: no ASLR, no stack canaries, no memory protection
between "user" and kernel. Do not treat any boundary as a security
boundary. See the [FAQ](faq.md).
