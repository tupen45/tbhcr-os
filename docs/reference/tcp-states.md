# TCP State Machine Reference

A companion to [TCP](tcp.md) showing the exact transitions implemented in
`tcp.c` for the single global socket `g_sock`. Only a **client** role is
supported.

## States

```c
typedef enum {
    TCP_CLOSED=0, TCP_SYN_SENT, TCP_ESTABLISHED,
    TCP_FIN_WAIT1, TCP_FIN_WAIT2, TCP_TIME_WAIT
} tcp_state_t;
```

## Transition table (as coded in `tcp_on_rx`)

| From | Trigger (flags / event) | Action | To |
|------|---------------------------|---------|-----|
| `SYN_SENT` | `SYN+ACK` (0x12) from peer | `rcv_nxt = seq+1`; send `ACK` (0x10) | `ESTABLISHED` |
| `ESTABLISHED` | data, `seq == rcv_nxt` | buffer; `rcv_nxt += len`; send `ACK` | `ESTABLISHED` |
| `ESTABLISHED` | `FIN` (0x01) | `ACK` then `FIN+ACK` (0x11); `snd_nxt++` | `FIN_WAIT1` |
| `FIN_WAIT1` | `ACK` (0x10) | — | `FIN_WAIT2` |
| `FIN_WAIT2` | `FIN` (0x01) | `rcv_nxt++`; `ACK`; | `TIME_WAIT` |
| any | `RST` (0x04) | clear `in_use` | `CLOSED` |

## Flag bits (`tcp_hdr.flags`)

| Bit | Name |
|-----|------|
| 0x01 | FIN |
| 0x02 | SYN |
| 0x04 | RST |
| 0x08 | PSH |
| 0x10 | ACK |
| 0x18 | PSH+ACK (used for data) |
| 0x11 | FIN+ACK |

## Connection setup (`tcp_connect`)

1. `state = SYN_SENT`, `snd_iss = iss()` (`0x12340000 + 0x1000`/call).
2. `snd_nxt = snd_iss`; `local_port = src_port ? src_port : pick_ephemeral()`
   (from `0x40000` upward).
3. Send `SYN`; `snd_nxt += 1`.
4. `g_sock = *s`. Caller then spins `net_poll()` until `ESTABLISHED`.

## Send / close / poll

| Op | Effect |
|----|---------|
| `tcp_send(s, data, len)` | Only if `ESTABLISHED`; send `PSH+ACK`; `snd_nxt += len`. |
| `tcp_close(s)` | If `ESTABLISHED`: `FIN+ACK`, `snd_nxt++`, → `FIN_WAIT1`. Else → `CLOSED`. |
| `tcp_recv(s, out, max)` | Copy `rx_buf`, `memmove` remainder; returns bytes (0 = none). |
| `tcp_poll()` | No-op (no timers / retransmit). |

## Not implemented

- Simultaneous open / passive `LISTEN`/`SYN_RCVD`.
- Retransmission & RTO; keep-alive; window scaling.
- URG handling; selective acknowledgments.
- More than one concurrent socket.

These are intentional simplifications for an HTTP/HTTPS client.
