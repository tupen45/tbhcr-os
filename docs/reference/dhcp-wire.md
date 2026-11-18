# DHCP Options & Wire Reference

A companion to [DHCP](dhcp.md) with the exact constants and packet shape
used by `dhcp.c`.

## Constants (`dhcp.c`)

```c
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY   2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6

#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_FIXED_LEN 240
```

## Option codes

| Code | Name | Handled |
|------|------|----------|
| 1 | Subnet Mask | yes |
| 3 | Router (gateway) | yes |
| 6 | DNS server | yes |
| 50 | Requested IP | sent in REQUEST |
| 53 | Message Type | yes |
| 54 | Server Identifier | yes |
| 55 | Parameter Request List | sent in DISCOVER/REQUEST |
| 255 | End | terminator |

## Message types

| Type | Constant | Direction |
|------|-----------|-------------|
| 1 | `DHCP_MSG_DISCOVER` | C→S |
| 2 | `DHCP_MSG_OFFER` | S→C |
| 3 | `DHCP_MSG_REQUEST` | C→S |
| 4 | `DHCP_MSG_DECLINE` | C→S |
| 5 | `DHCP_MSG_ACK` | S→C |
| 6 | `DHCP_MSG_NAK` | S→C |

## Packet structure (`struct dhcp_packet`, `#pragma pack`)

```
0   op        (1)
1   htype     (1)
2   hlen      (1)
3   hops      (1)
4   xid       (4)   <- matched against g_xid
8   secs      (2)
10  flags     (2)   <- 0x8000 = broadcast reply
12  ciaddr    (4)
16  yiaddr    (4)   <- offered/acked IP
20  siaddr    (4)
24  giaddr    (4)
28  chaddr[16] (16)  <- client MAC
44  sname[64] (64)
108 file[128] (128)
236 cookie    (4)   <- 0x63825363
240 options[312]
```

## Client state transitions

```
OFF ── dhcp_start() ─────────────► SELECTING  (send DISCOVER)
SELECTING ── OFFER (msg 2) ───────► REQUESTING (send REQUEST)
REQUESTING ── ACK (msg 5) ─────────► BOUND       (apply lease)
REQUESTING ── NAK (msg 6) ─────────► SELECTING  (re-DISCOVER)
* ── 8× poll timeout ─────────────► slirp fallback (NIC) / OFF (USB)
```

`dhcp_poll()` advances only every ~5000 ticks; retries cap at 8.

## slirp fallback values

Applied by `dhcp_apply_slirp_fallback()` when no USB tether is active
and retries are exhausted:

```
ip   = 10.0.2.15
mask = 255.255.255.0
gw   = 10.0.2.2
dns  = 10.0.2.3
```

These match QEMU's `user` networking defaults, so the browser works even
without a successful DHCP exchange.
