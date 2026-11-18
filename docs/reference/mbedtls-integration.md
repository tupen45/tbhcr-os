# mbedTLS Integration

How the vendored TLS library is wired into the freestanding kernel. See
also [TLS](../networking/tls.md) and [Build Internals](../reference/build-internals.md).

## Vendored tree

```
vendor/mbedtls/
  include/mbedtls/   config_mini.h, platform.h, platform_stubs.h,
                       net_sockets.h, ssl.h, ctr_drbg.h, ...
  library/             entropy.c, ctr_drbg.c, ssl_*.c, cipher.c,
                       aes.c, sha*.c, bignum.c, ecp*.c, ecdh.c,
                       md.c, gcm.c, ...
  platform_shim.c      (in repo root, compiled separately)
```

`build.sh` compiles ~24 `library/*.c` files plus `platform_shim.c` and
`tls_mbedtls.c` with the freestanding mbedTLS macros.

## Compile macros

```
-DMBEDTLS_CONFIG_FILE=\"mbedtls/config_mini.h\"
-DMBEDTLS_PLATFORM_TIME_TYPE_MACRO=long
-DMBEDTLS_PLATFORM_TIME_MACRO=mbedtls_time_stub
-DMBEDTLS_PLATFORM_NO_STD_FUNCTIONS
-DMBEDTLS_NO_PLATFORM_ENTROPY
-include vendor/mbedtls/include/mbedtls/platform_needed.h
```

- `config_mini.h` — a minimal feature set (client TLS, a few ciphersuites).
- `platform_needed.h` — force-includes `INT_MAX`, string prototypes, etc.
- `platform_shim.c` — satisfies the few platform hooks mbedTLS needs
  (`mbedtls_printf`, timing stub, memory).

## Platform hooks

| Hook | Provided by | Purpose |
|-------|--------------|----------|
| `mbedtls_printf` | `platform_shim.c` | Debug/print sink |
| `mbedtls_time_stub(long*)` | `tls_mbedtls.c` | Returns 0; satisfies `MBEDTLS_PLATFORM_TIME_*` |
| `malloc`/`free` | `kmalloc.c` (`stdlib.h`) | Memory (no real `calloc` needed here) |
| entropy | `my_entropy()` LFSR | **Not** cryptographically strong |

## Debug visibility

`mbedtls_debug_cb()` forwards mbedTLS debug strings via `mbedtls_printf`
into a platform buffer retrievable through `mbedtls_get_debug()` (declared
`extern` in `kernel.c`). With `-serial mon:stdio`, handshake failures
surface on the host console.

## Why a shim at all?

mbedTLS is written for hosted environments. In a freestanding kernel it
has no libc, no `time()`, and a custom allocator. The shim + minimal
config + `NO_STD_FUNCTIONS` let it compile and run against MyOS's own
`kmalloc` and `printf`. Swapping mbedTLS versions requires updating the
shim and config together.

See also: [TLS](../networking/tls.md), [API: Stdlib](../reference/api-stdlib.md).
