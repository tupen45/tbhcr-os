# Standard Library Stubs Reference

MyOS is freestanding, so it provides its own minimal `<stdio.h>`,
`<stdlib.h>`, and `<string.h>` plus the allocator in `kmalloc.c`. These
supply just enough for the kernel, mbedTLS, and JSMN to compile and run.

## `stdio.h` (project header)

A tiny `printf`-like surface used by the network stack for debug output.
Declares `printf`, `snprintf`, and `console_puts`. The implementation is
limited (no floating point, no field widths beyond what the callers use).

- `printf(fmt, ...)` — formatted print to the serial/console sink.
- `snprintf(buf, sz, fmt, ...)` — bounded format into `buf`
  (used heavily by the UI to compose status strings).
- `console_puts(s)` — defined in `syscalls.c`; draws `s` on the
  on-screen console (used by user programs via syscall `write`).

## `stdlib.h` (project header)

Provides `malloc`/`free`/`calloc` aliases (→ `kmalloc`), `rand`/`srand`,
`abort`, and the `MBEDTLS_TIME_TYPE_MACRO`/`mbedtls_time_stub` hooks.

## `string.h` (`string.c`)

| Function | Notes |
|----------|-------|
| `strlen(s)` | Standard. |
| `strcpy` / `strncpy` | Standard (callers avoid overflow). |
| `strcmp` / `strncmp` | Standard. |
| `strchr(s, c)` | Used by DNS name packing. |
| `strstr(hay, needle)` | Used by HTTP redirect parsing. |
| `memcpy` / `memmove` | `memmove` used by `tcp_recv` shift. |
| `memset` | Used by DHCP/HTTP zero-fill. |
| `memcmp` | Standard. |

These are hand-written freestanding routines (no SIMD, no builtins).

## `kmalloc.c` symbols

| Symbol | Maps to |
|--------|---------|
| `malloc(size)` | `kmalloc(size)` |
| `free(ptr)` | `kfree(ptr)` |
| `calloc(num,size)` | zero-filled `kmalloc` |
| `abort()` | infinite loop |
| `rand()` / `srand(seed)` | LCG (`state*1103515245+12345`, returns `state>>16`) |

> `mbedTLS` is compiled with `-DMBEDTLS_PLATFORM_NO_STD_FUNCTIONS`
> and uses these symbols via the platform shim; it does not pull in the
> host libc.

See also: [Memory (kmalloc)](../kernel/memory.md).
