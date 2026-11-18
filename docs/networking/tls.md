# TLS / HTTPS (`tls_mbedtls.c`)

For encrypted endpoints, MyOS wraps its minimal TCP socket with the vendored
**mbedTLS** library. `tls_http_get_by_ip()` performs a full TLS 1.2 client
handshake and HTTPS GET over the existing `tcp.c` transport.

## Why mbedTLS

mbedTLS (formerly PolarSSL) is designed for constrained, freestanding
environments. The project vendors a minimal build (`vendor/mbedtls`,
`config_mini.h`) that compiles cleanly with `-ffreestanding` and
`-DMBEDTLS_NO_PLATFORM_ENTROPY`. A `platform_shim.c` supplies the few
platform hooks the library needs (printf, timing stub, memory).

## API

```c
int tls_http_get_by_ip(uint32_t ip, const char *host_header,
                         const char *path, char *out, int out_cap);
```

Returns body bytes written (null-terminated), or `<0` on failure. Called
from `kernel.c` as a fallback when plain HTTP returns nothing but the IP
resolved.

## BIO glue — the bridge to `tcp.c`

mbedTLS talks to the network through two callbacks:

```c
static int net_send(void *ctx, const unsigned char *buf, size_t len) {
    tcp_socket_t *s = ctx;
    int ret = tcp_send(s, (const char*)buf, (int)len);
    return (ret < 0) ? MBEDTLS_ERR_SSL_WANT_WRITE : ret;
}
static int net_recv(void *ctx, unsigned char *buf, size_t len) {
    tcp_socket_t *s = ctx;
    int ret = tcp_recv(s, (char*)buf, (int)len);
    if (ret == 0) return MBEDTLS_ERR_SSL_WANT_READ;  // would block
    if (ret < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    return ret;
}
```

`mbedtls_ssl_set_bio(&ssl, s, net_send, net_recv, NULL)` wires them in.
Because `tcp_recv()` is non-blocking, a `WANT_READ`/`WANT_WRITE` result
drives a `net_poll()` retry loop.

## Handshake & request

1. `tcp_connect(s, ip, 443, 0)`.
2. Seed `mbedtls_ctr_drbg` with a **lightweight LFSR entropy callback**
   (`my_entropy`). *This is not cryptographically strong* — it exists only
   to make the prototype functional; do not rely on it for real security.
3. `mbedtls_ssl_config_defaults(client, stream, default)`.
4. `mbedtls_ssl_conf_rng(...)`, optional debug callback, and
   `MBEDTLS_SSL_VERIFY_NONE` (certificate verification disabled for the
   demo).
5. `mbedtls_ssl_set_hostname(&ssl, host_header)` for SNI.
6. Loop `mbedtls_ssl_handshake()` until it returns 0, pumping `net_poll()`
   on `WANT_*` conditions.
7. `mbedtls_ssl_write()` the GET, then `mbedtls_ssl_read()` into `out`
   until EOF/error.
8. `mbedtls_ssl_close_notify()` and free all contexts.

## Security caveats (important)

- **Certificate verification is disabled** (`VERIFY_NONE`). The OS does
  **not** authenticate the server — it is vulnerable to MITM. This is
  acceptable for a teaching demo but must never be used for real secrets.
- **Entropy is a toy LFSR**, not a hardware RNG. Key material quality is
  poor by design of the prototype.
- There is no session resumption or client certificate support.

## Debugging

`mbedtls_debug_cb` forwards mbedTLS's debug strings into the platform
buffer (retrievable via `mbedtls_get_debug()`, declared in `kernel.c`).
With QEMU `+ -serial mon:stdio`, this surfaces handshake failures on the
host console.

Next: [USB Host](drivers/usb-host.md).
