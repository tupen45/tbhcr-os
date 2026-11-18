# Networking API Reference (`net.h`)

The public interface declared in `net.h` and implemented in `net.c`. All
IPv4 addresses are **host-order packed** as `(A<<24)|(B<<16)|(C<<8)|D`
unless noted.

## Types

```c
typedef int  (*net_send_func_t)(const void *data, int len);
typedef void (*net_poll_func_t)(void);

struct net_if {
    uint8_t mac[6];
    uint32_t ip, gw_ip, netmask;   // host order
    net_send_func_t send;
    net_poll_func_t poll;
};
extern struct net_if *g_active_netif;
```

## Interface registry

| Function | Description |
|----------|-------------|
| `int netif_register(uint8_t mac[6], net_send_func_t send, net_poll_func_t poll)` | Register a NIC; first becomes active. Max 2. |
| `void net_register_if(...)` | Legacy alias for `netif_register`. |
| `int netif_activate(int idx)` | Switch active interface; clears ARP/pending. |
| `int netif_activate_other(net_send_func_t avoid_send)` | Activate any interface except `avoid_send`. |

## Address configuration

| Function | Description |
|----------|-------------|
| `void net_set_ipv4(ip, netmask, gw)` | Set addresses on the active iface. |
| `uint32_t net_get_ip(void)` | Active interface IPv4. |
| `int net_has_ipv4(void)` | True if active iface has a non-zero IP. |
| `uint32_t net_get_gateway(void)` | Gateway address. |
| `void net_set_dns(uint32_t dns_ip)` | Configure DNS server. |
| `uint32_t net_get_dns(void)` | Current DNS server. |

## Send / receive

| Function | Description |
|----------|-------------|
| `void nic_tx(const void *data, int len)` | Low-level TX (driver registered send). |
| `void net_send_udp_ipv4(dst_ip, dst_port, src_port, payload, len)` | Send a UDP datagram (with pseudo-header checksum). |
| `void net_send_ip(dst_ip, proto, payload, payload_len)` | Send a raw IPv4 packet (handles ARP + gateway). |
| `void net_rx(const uint8_t *frame, int len)` | RX entry from a NIC driver's poll(). |
| `void net_poll(void)` | Heartbeat: usb_poll + NIC poll + dhcp_poll. |

## Weak hooks

```c
__attribute__((weak)) void udp_on_datagram(uint32_t src_ip, uint16_t src_port,
                                          const uint8_t *d, int l);
__attribute__((weak)) void tcp_on_segment(uint32_t src_ip, uint16_t src_port,
                                          const struct tcp_hdr *th,
                                          const uint8_t *pl, int len);
```

Both default to empty bodies; override them to snoop raw traffic.

See also: [Network Core](../networking/network-core.md).
