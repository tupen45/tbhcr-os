# HTTP Client (`http.c`)

`http.c` is a small **HTTP/1.1 GET client** layered on top of `tcp.c`. It
fetches a resource by IPv4 address, follows up to 5 redirects, and returns
the response **body** (headers stripped) into a caller-provided buffer.

## API

```c
int http_get_by_ip(uint32_t ip, const char *host_header,
                    const char *path, char *out, int out_cap);
int http_get_by_ip_port(uint32_t ip, uint16_t port,
                        const char *host_header, const char *path,
                        char *out, int out_cap);
```

- `ip` — host-order packed IPv4 (`(A<<24)|(B<<16)|(C<<8)|D`).
- `host_header` — value for the `Host:` header (and SNI-free plain HTTP).
- `path` — e.g. `"/posts/2"`.
- Returns bytes written (null-terminated), `0` for empty, `<0` for error
  (see `http_last_ret` codes below).

`http_get_by_ip` is a wrapper that forces port 80.

## Request shape

```
GET <path> HTTP/1.1\r\n
Host: <host_header>\r\n
User-Agent: MyOS/0.1\r\n
Connection: close\r\n
\r\n
```

`Connection: close` lets the client detect end-of-body when the peer sends
FIN.

## Redirect handling

If the status code is `300–399`, the client scans for a `Location:` header:

| Location form | Behavior |
|----------------|------------|
| `https://...` | Abort (`-11`) — no TLS in plain HTTP path |
| `http://host/path` with **IP literal** host | Update `ip`/`host`/`path`, `continue` |
| `http://host/path` with **domain** host | If host equals current `Host`, update path and continue; else abort (`-13`) — no DNS in this path |
| absolute `/path` | Update path, `continue` |
| other | Abort (`-14`) |

A `MAX_REDIRECTS` of 5 caps the loop; exceeding it returns `-15`.

## Parsing helpers

- `parse_ipv4_literal(s, &ip)` — strict dotted-quad parser (0–255 per
  octet) used for redirect host literals.
- `parse_http_status(raw)` — extracts the 3-digit status after `HTTP/x.y `.

## Body extraction

After the final (non-redirect) response, the body starts after the first
`"\r\n\r\n"`. Up to `out_cap-1` bytes are copied and null-terminated.

## Debug exports

```c
int http_last_ret;       // final return code
int http_last_raw_len;   // total raw bytes (headers + body)
int http_last_tcp_state; // last observed g_sock.state during connect
```

## Error codes (`http_last_ret`)

| Code | Meaning |
|------|---------|
| `-1` | Bad argument |
| `-2` | `tcp_connect` failed |
| `-3` | Timed out waiting for ESTABLISHED |
| `-4` | Could not build request |
| `0` | Connection closed with no body |
| `-11` | Redirect to HTTPS |
| `-12` | Redirect missing/odd Location |
| `-13` | Redirect to different hostname (no DNS) |
| `-14` | Unknown redirect form |
| `-15` | Too many redirects |

## Relationship to DNS/TLS

The browser (`kernel.c`) resolves the host via `dns_resolve()` **before**
calling this function, passing the resulting IP. If plain HTTP fails but the
IP resolved, `kernel.c` falls back to `tls_http_get_by_ip()` (see
[TLS](tls.md)).

Next: [TLS](tls.md).
