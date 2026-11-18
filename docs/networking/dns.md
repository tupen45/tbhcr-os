# DNS Resolver (`dns.c`)

MyOS includes an **asynchronous-aware** DNS client focused on A-record
(IPv4) lookups. It caches results, tracks outstanding queries by transaction
ID, and exposes both async and blocking entry points.

## Data structures

```c
struct dns_cache_entry { char name[128]; uint32_t ip; };  // 8 entries
struct qtrack { uint16_t qid; char name[128]; };          // 8 outstanding
```

- `dns_cache[]` — resolved name→IP mapping (evicts slot 0 when full).
- `qtracks[]` — maps a query ID to the requested name so responses can be
  attributed even though the question section is skipped.

## Blocking resolver — `dns_resolve(name, &ip)`

The primary API used by the browser and JSON viewer:

```c
int dns_resolve(const char *name, uint32_t *out_ip) {
    if (dns_get_cached(name, out_ip)) return 1;
    servers = [ dhcp_dns, gateway, 1.1.1.1, 8.8.8.8 ];
    for (s in servers) {
        dns_query_async_to(name, s);
        for (i = 0; i < 30000; i++) {
            net_poll();                       // pump NIC/USB
            if (dns_get_cached(name, out_ip)) return 1;
        }
    }
    return 0;
}
```

It tries up to four servers (DHCP-provided DNS, the gateway, and the public
resolvers `1.1.1.1` / `8.8.8.8`) and spins `net_poll()` up to 30000
times per server waiting for the cache to populate.

## Async helper — `dns_query_async_to(name, dns_ip)`

Builds a standard query:

- 2-byte `id` (from `qid_counter`, starts at `0x4000`, incremented).
- Flags `0x0100` (recursion desired).
- One question: `QTYPE=A (1)`, `QCLASS=IN (1)`.
- Hostname encoded via `pack_name()` into length-prefixed labels ending in a
  zero octet.

Posts it with `net_send_udp_ipv4(dns_ip, 53, 50000, buf, off)`.

## Response parsing — `dns_on_response(src, data, len)`

Invoked from `net_rx()` whenever a UDP datagram arrives from **source port
53**. It:

1. Reads `id`, `qdcount`, `ancount`.
2. Skips the question section (walk labels / pointers).
3. For each answer, follows name pointers/labels, and if
   `atype==1 && aclass==1 && rdlen==4` stores the IPv4 into the cache keyed
   by `qtrack_getname(id)`.
4. Clears the outstanding query with `qtrack_clear(id)`.

## Cache API

| Function | Purpose |
|----------|---------|
| `dns_get_cached(name, &ip)` | Fast lookup before sending a query |
| `dns_cache_store(name, ip)` | Insert/evict on answer |
| `dns_query_async(name)` | Convenience: picks DNS IP (DHCP → gateway → `10.0.2.3`) |

The fallback DNS of `10.0.2.3` matches QEMU's slirp user-networking
default, keeping resolution working even before DHCP completes.

## Limitations

- **No CNAME chasing.** Only direct A records are stored; a CNAME answer
  without a paired A record yields nothing.
- **No TCP fallback** for large/truncated responses (TC bit ignored).
- The cache is tiny (8 entries) and never expires — acceptable for a demo
  session.

Next: [TCP](tcp.md).
