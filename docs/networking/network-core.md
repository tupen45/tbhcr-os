# Network Core (`net.c`)

`net.c` is the heart of MyOS's networking. It owns the **interface registry**,
builds and demultiplexes **Ethernet / ARP / IPv4 / UDP / TCP** frames, and
exposes a small set of send/receive helpers used by DHCP, DNS, TCP/HTTP, and
TLS.

## Interface model

```c
struct net_if {
    uint8_t mac[6];
    uint32_t ip;        // host order
    uint32_t gw_ip;     // host order
    uint32_t netmask;   // host order
    net_send_func_t send; // driver TX: int(*)(const void*, int)
    net_poll_func_t poll;// driver RX drain
};

struct net_if g_netif_list[2];          // up to 2 interfaces
struct net_if *g_active_netif = NULL;  // the one we use
```

A driver (RTL8139, e1000e, or a USB tether) registers itself with
`netif_register(mac, send, poll)`. The first to register becomes
`g_active_netif`; USB interfaces are registered first by `kmain`, so they
take precedence over PCI NICs. `netif_activate(idx)` and
`netif_activate_other(avoid_send)` let code switch the active interface
(e.g. after a USB tether disconnect).

## Packet header structures

All headers are `#pragma pack(push,1)` and little-endian on the wire:

| Header | Key fields |
|--------|------------|
| `eth_hdr` | `dst[6]`, `src[6]`, `type` (0x0806 ARP, 0x0800 IPv4) |
| `arp_ipv4` | `oper`, `sha[6]`, `spa`, `tha[6]`, `tpa` |
| `ip_hdr` | `ver_ihl`, `tot_len`, `ttl`, `proto` (17 UDP, 6 TCP), `src`, `dst` |
| `udp_hdr` | `src`, `dst`, `len`, `chksum` |
| `tcp_hdr` | `src`, `dst`, `seq`, `ack`, `offset_reserved`, `flags`, `win` |

## ARP cache & pending queue

```c
struct arp_cache_entry { uint32_t ip; uint8_t mac[6]; };  // 8 entries
struct pending_pkt { uint32_t dst_ip; uint8_t proto; int payload_len; uint8_t payload[1500]; }; // 8
```

- `arp_lookup(ip, mac)` / `arp_store(ip, mac)` maintain the 8-slot cache.
- `send_arp_request(target_ip)` broadcasts an ARP who-has.
- When a packet must be sent but the next-hop MAC is unknown, it is queued in
  `pending[]` and an ARP request is sent. `flush_pending_for_ip()` replays
  queued packets once the MAC is learned.

## Receive path — `net_rx(frame, len)`

```
Ethernet frame
  ├─ type == ARP  → arp_store(spa, sha); flush_pending_for_ip(spa)
  └─ type == IPv4
        ├─ proto == UDP
        │     ├─ src port 53        → dns_on_response(...)
        │     ├─ src 67 / dst 68    → dhcp_on_udp(...)
        │     └─ → udp_on_datagram(...)   (weak hook)
        └─ proto == TCP → tcp_on_rx(tcp_seg, len, src, dst)
```

## Send helper — `net_send_ip(dst_ip, proto, payload, len)`

1. Compute the **next hop**: if the destination is off-subnet
   (`(dst ^ ip) & netmask != 0`), send via `gw_ip`; otherwise it is on-link.
2. ARP-resolve the next-hop MAC; if unknown, enqueue + send ARP request.
3. Build the Ethernet + IPv4 headers, compute the IP checksum
   (`ip_checksum` via `checksum_add_bytes`/`checksum_finish`), and call
   `g_active_netif->send()`.

## UDP convenience — `net_send_udp_ipv4()`

Wraps `net_send_ip()` with a UDP header and a **pseudo-header checksum**
(`udp_checksum_ipv4`). Used by DHCP and DNS clients.

## TCP SYN helper — `net_send_tcp_syn()`

A minimal helper that emits a bare SYN segment; the full TCP state machine
lives in `tcp.c` and calls `net_send_ip()` directly with protocol `6`.

## The network heartbeat — `net_poll()`

```c
void net_poll(void) {
    usb_poll();                                  // USB enumeration
    if (g_active_netif && g_active_netif->poll)
        g_active_netif->poll();                  // drain NIC RX → net_rx()
    dhcp_poll();                                // advance DHCP
}
```

Every UI loop calls this so received frames are processed regardless of which
application is on screen.

## Weak callback hooks

`udp_on_datagram()` and `tcp_on_segment()` are declared `__attribute__((weak))`
with empty bodies. They let future modules hook raw datagrams/segments
without modifying `net.c`.

Next: [DHCP](dhcp.md).
