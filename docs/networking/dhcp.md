# DHCP Client (`dhcp.c`)

The DHCP client obtains an IPv4 address, subnet mask, default gateway, and DNS
server from a DHCP server (typically QEMU's built-in slirp server, or a phone
tether's gateway).

## State machine

```c
enum dhcp_state {
    DHCP_STATE_OFF = 0,
    DHCP_STATE_SELECTING,   // sent DISCOVER, awaiting OFFER
    DHCP_STATE_REQUESTING,  // sent REQUEST, awaiting ACK
    DHCP_STATE_BOUND         // lease active
};
```

The visible welcome-screen status codes map onto these plus USB conditions
(`welcome_status_code()` in `kernel.c`).

## Message flow

```
Client                                  Server
  ── DISCOVER (broadcast) ─────────────►
  ◄──────── OFFER (yiaddr, opts) ────────
  ── REQUEST (req ip, server id) ───────►
  ◄──────────── ACK (lease) ───────────────
```

The transaction ID `g_xid` (seeded from `rand()`) is echoed in every
reply so responses are matched.

## Key functions

| Function | Role |
|----------|------|
| `dhcp_start()` | Reset offers, set SELECTING, send DISCOVER |
| `dhcp_stop()` | Return to OFF |
| `dhcp_poll()` | Every ~5000 ticks, re-send or time out (after 8 retries) |
| `dhcp_on_udp(src, data, len)` | Parse OFFER/ACK, extract options |
| `dhcp_is_bound()` | True when a lease is active |
| `dhcp_apply_slirp_fallback()` | Hard-code `10.0.2.15/24`, gw `.2`, DNS `.3` |

## Options parsed

The client requests `SUBNET_MASK (1)`, `ROUTER (3)`, `DNS (6)`, and
`SERVER_ID (54)`. It also handles `MSG_TYPE (53)` and `REQ_IP (50)`.
On `ACK` it applies the lease via `net_set_ipv4(ip, mask, router)` and
`net_set_dns(dns)`, then transitions to `BOUND`.

## Timeout & fallback

If `dhcp_poll()` retries 8 times without success:

- **PCI/NIC mode** (no USB tether active): applies the **slirp fallback**
  so the browser still works under QEMU user networking.
- **USB tether mode** (`rndis_active` or `ipheth_active`): gives up with
  a message (no fallback — the phone's gateway is required).

A `NAK` reply restarts the process from `SELECTING` with a fresh DISCOVER.

## Wire details

- Uses `DHCP_CLIENT_PORT 68` / `DHCP_SERVER_PORT 67`.
- Broadcasts to `255.255.255.255` with the "broadcast reply requested"
  flag (`0x8000`) so replies arrive without a configured IP.
- Magic cookie `0x63825363` precedes the options block.
- Minimum packet length padded to 300 bytes for compatibility.

Next: [DNS](dns.md).
