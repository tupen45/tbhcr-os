# HTTP/TLS Wire Reference

Byte-level detail for the two web clients, complementing
[HTTP](../networking/http.md) and [TLS](../networking/tls.md).

## Plain HTTP request (`http.c`)

```
GET <path> HTTP/1.1\r\n
Host: <host_header>\r\n
User-Agent: MyOS/0.1\r\n
Connection: close\r\n
\r\n
```

Built by `snprintf` into a 512-byte `req[]`. `Connection: close`
lets the client detect end-of-body from the peer's FIN.

## Response handling

1. Accumulate raw bytes into a static 32 KiB `raw[]` (cap 32768),
   pumping `net_poll()` + `tcp_recv()` until FIN or cap.
2. `parse_http_status(raw)` reads the 3-digit code after `HTTP/x.y `.
3. If `300 ≤ status < 400`: parse `Location:` and possibly recurse
   (≤ `MAX_REDIRECTS` = 5).
4. Else: find `"\r\n\r\n"`, copy the body (cap `out_cap-1`), null-terminate.

## Redirect rules

| `Location` form | Outcome |
|-------------------|----------|
| `https://...` | return `-11` (no TLS in plain path) |
| `http://<IP>/...` | update `ip`/`host`/`path`, continue |
| `http://<domain>/...` == host | update `path`, continue |
| `http://<other domain>/...` | return `-13` (no DNS here) |
| `/abs/path` | update `path`, continue |
| other | return `-14` |

## HTTPS via mbedTLS (`tls_mbedtls.c`)

```
tcp_connect(s, ip, 443, 0)
  └─ seed CTR-DRBG (LFSR entropy — NOT secure)
mbedtls_ssl_config_defaults(client, stream, default)
mbedtls_ssl_conf_rng(...)
mbedtls_ssl_conf_authmode(VERIFY_NONE)     // cert check OFF
mbedtls_ssl_set_hostname(&ssl, host)        // SNI
mbedtls_ssl_set_bio(&ssl, s, net_send, net_recv, NULL)
loop mbedtls_ssl_handshake() until 0 (WANT_* -> net_poll)
mbedtls_ssl_write(&ssl, req, n)
loop mbedtls_ssl_read(&ssl, out+total, cap-total) until EOF
mbedtls_ssl_close_notify(&ssl); free all
```

The BIO callbacks (`net_send`/`net_recv`) wrap `tcp_send`/`tcp_recv`
and translate `0`/error returns into `MBEDTLS_ERR_SSL_WANT_READ/
WANT_WRITE` so the handshake loop can pump `net_poll()`.

## Security caveats (repeat, because they matter)

- **Certificate verification is disabled.** MITM is possible; do not send
  secrets.
- **Entropy is a toy LFSR** in `my_entropy()` — not for real keys.
- No redirect across hostnames over TLS (no DNS in that path).

See also: [TCP](../networking/tcp.md), [Network Core](../networking/network-core.md).
