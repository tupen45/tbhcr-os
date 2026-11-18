# DNS Wire Reference

Companion to [DNS](dns.md) with the exact byte layout MyOS builds and
parses.

## Query packet

```
0  id[2]            (qid_counter, starts 0x4000)
2  flags[2] = 0x0100  (QR=0, OPCODE=0, RD=1)
4  qdcount[2] = 0x0001
6  ancount[2] = 0x0000
8  nscount[2] = 0x0000
10 arcount[2] = 0x0000
12 <QNAME>  length-prefixed labels, ends 0x00
   <QTYPE>[2] = 0x0001  (A)
   <QCLASS>[2] = 0x0001 (IN)
```

`pack_name()` encodes `host` as labels: for each dot-separated part,
emit `len` then the bytes; finish with a zero octet.

## Response parsing (`dns_on_response`)

```
0  id[2]
2  flags[2]
4  qdcount[2]
6  ancount[2]
8  nscount[2]
10 arcount[2]
12 <QUESTION>  skip: walk labels to 0x00, then +4 (QTYPE/QCLASS)
   for each answer (ancount):
     <NAME>     pointer (0xC0xx) = 2 bytes, else labels to 0x00
     <TYPE>[2]
     <CLASS>[2]
     <TTL>[4]
     <RDLENGTH>[2]
     <RDATA>[RDLENGTH]   (4 bytes for A)
```

An A record (`TYPE==1 && CLASS==1 && RDLENGTH==4`) is stored as
`(b0<<24)|(b1<<16)|(b2<<8)|b3` into the cache keyed by the
outstanding query name (`qtrack_getname(id)`).

## Server selection (`dns_resolve`)

Tries, in order, until the cache populates:

1. DHCP-provided DNS (`net_get_dns()`)
2. Gateway (`net_get_gateway()`), if different
3. `1.1.1.1`
4. `8.8.8.8`

Public resolvers 3–4 mean name resolution often works even before DHCP
completes. A `10.0.2.3` fallback also exists in `dns_query_async()`
for QEMU slirp.

## Outstanding-query tracking

`qtracks[8]` maps `qid → name`. `dns_query_async_to()` adds the
mapping; `dns_on_response()` clears it (`qtrack_clear`) once answered.
If the table is full, slot 0 is evicted.

See also: [Network Core](../networking/network-core.md),
[HTTP](../networking/http.md).
