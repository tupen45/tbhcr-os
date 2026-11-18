# TCP Implementation (`tcp.c`)

MyOS implements a **minimal, single-socket, blocking-on-connect TCP** stack
sufficient for HTTP/HTTPS client requests. It is *not* a general-purpose
TCP — there are no timers, no retransmit, no window scaling, and only one
active connection (`g_sock`).

## Socket structure

```c
typedef struct {
    uint8_t  in_use;
    tcp_state_t state;
    uint16_t local_port, remote_port;
    uint32_t local_ip, remote_ip;
    uint32_t snd_iss, snd_nxt;   // initial / next send seq
    uint32_t rcv_nxt;             // next expected seq
    uint8_t *rx_buf; int rx_cap, rx_len;  // 8 KiB static buffer
} tcp_socket_t;
```

States: `TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED, TCP_FIN_WAIT1,
TCP_FIN_WAIT2, TCP_TIME_WAIT`.

## Segment transmission — `tcp_send_segment()`

Builds a TCP header, computes the **pseudo-header checksum** over
`src, dst, zero, proto=6, tcp_len` + header + payload, and hands the
segment to `net_send_ip(remote_ip, 6, ...)`. Window is fixed at 4096.

## Connection setup — `tcp_connect()`

```c
int tcp_connect(s, dst_ip, dst_port, src_port) {
    s->state = TCP_SYN_SENT;
    s->snd_iss = iss();           // 0x12340000, +0x1000 each call
    s->snd_nxt = s->snd_iss;
    s->local_port = src_port ?: pick_ephemeral();   // from 40000 up
    s->local_ip = net_get_ip();
    tcp_send_segment(s, 0x02 /*SYN*/, NULL, 0);
    s->snd_nxt += 1;
    g_sock = *s;
    return 0;
}
```

The actual three-way handshake completion happens in `tcp_on_rx()` once the
peer's SYN-ACK arrives.

## Receive state machine — `tcp_on_rx()`

```
SYN_SENT   + SYN,ACK  → set rcv_nxt = seq+1; send ACK; → ESTABLISHED
ESTABLISHED + data (seq==rcv_nxt)
                       → buffer into rx_buf; rcv_nxt += len; send ACK
ESTABLISHED + FIN     → ACK; send FIN+ACK; → FIN_WAIT1
FIN_WAIT1  + ACK      → FIN_WAIT2
FIN_WAIT2  + FIN      → ACK; → TIME_WAIT
+ RST any time         → CLOSED
```

Only segments from `g_sock.remote_ip` and the expected ports are processed;
everything else is ignored.

## Data transfer

| Function | Semantics |
|----------|-----------|
| `tcp_send(s, data, len)` | Only when ESTABLISHED; sends `PSH+ACK`, advances `snd_nxt` by `len` |
| `tcp_recv(s, out, maxlen)` | Copies from `rx_buf`, `memmove`s remainder, returns bytes (0 = none yet) |
| `tcp_close(s)` | If ESTABLISHED, send `FIN+ACK` → `FIN_WAIT1`; else mark closed |
| `tcp_poll()` | No-op (no timers implemented) |

## How HTTP uses it (`http.c`)

`http_get_by_ip_port()` calls `tcp_connect()`, then **spins `net_poll()`
up to 300000 iterations** until `g_sock.state == TCP_ESTABLISHED`. It then
sends the GET, repeatedly `tcp_recv()`s into a 32 KiB raw buffer while
pumping `net_poll()`, and closes once the socket reaches a FIN state.

## Known simplifications

- **No retransmission / RTO.** A dropped SYN or data segment stalls the
  spin loop until its iteration cap.
- **No simultaneous open / listen.** Pure client only.
- **Single socket.** `tcp_init()` zeroes `g_sock`; the browser/TLS paths each
  re-init before connecting.
- **`rx_buf` is a single static 8 KiB array** — large responses rely on
  the application reading promptly and the peer's window.
- Sequence numbers use a trivial LCG ISS; not RFC 6528 compliant.

Next: [HTTP](http.md).
