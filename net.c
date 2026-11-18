#include "net.h"
#include "util_net.h"
#include "string.h"
#include "graphics.h"
#include <stddef.h>
#include <stdint.h>
#include "endian.h"
#include "dns.h"
#include "tcp.h"
#include "stdio.h"
#include "dhcp.h"
#include "drivers/usb/usb_host.h"
#include "scheduler.h"


/* --- constants --- */
#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP  0x0800
#define IP_PROTO_UDP 17
#define IP_PROTO_TCP 6

/* --- headers --- */
#pragma pack(push,1)
struct eth_hdr { uint8_t dst[6]; uint8_t src[6]; uint16_t type; };
struct arp_ipv4 {
    uint16_t htype, ptype; uint8_t hlen, plen; uint16_t oper;
    uint8_t sha[6]; uint32_t spa; uint8_t tha[6]; uint32_t tpa;
};
struct ip_hdr {
    uint8_t ver_ihl, tos; uint16_t tot_len, id, frag;
    uint8_t ttl, proto; uint16_t hdr_chksum; uint32_t src, dst;
};
struct udp_hdr { uint16_t src,dst,len,chksum; };
struct tcp_hdr {
    uint16_t src, dst; uint32_t seq, ack;
    uint8_t offset_reserved; uint8_t flags; uint16_t win; uint16_t chksum; uint16_t urg_ptr;
};
#pragma pack(pop)

/* --- globals --- */
struct net_if g_netif_list[2];
int g_netif_count = 0;
struct net_if *g_active_netif = NULL;
static const uint8_t bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
struct arp_cache_entry { uint32_t ip; uint8_t mac[6]; };
static struct arp_cache_entry arp_cache[8];
static uint32_t g_dns_ip = 0;
// pending packets waiting for ARP resolution
struct pending_pkt { uint32_t dst_ip; uint8_t proto; int payload_len; uint8_t payload[1500]; };
static struct pending_pkt pending[8];

static uint32_t checksum_add_bytes(uint32_t sum, const uint8_t *data, int len) {
    while (len > 1) {
        sum += ((uint16_t)data[0] << 8) | data[1];
        data += 2;
        len -= 2;
    }
    if (len == 1) {
        sum += ((uint16_t)data[0] << 8);
    }
    return sum;
}

static uint16_t checksum_finish(uint32_t sum) {
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint16_t udp_checksum_ipv4(uint32_t src_ip, uint32_t dst_ip,
                                  const uint8_t *udp, int udp_len) {
    uint8_t pseudo[12];
    pseudo[0] = (uint8_t)(src_ip >> 24);
    pseudo[1] = (uint8_t)(src_ip >> 16);
    pseudo[2] = (uint8_t)(src_ip >> 8);
    pseudo[3] = (uint8_t)src_ip;
    pseudo[4] = (uint8_t)(dst_ip >> 24);
    pseudo[5] = (uint8_t)(dst_ip >> 16);
    pseudo[6] = (uint8_t)(dst_ip >> 8);
    pseudo[7] = (uint8_t)dst_ip;
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_UDP;
    pseudo[10] = (uint8_t)(udp_len >> 8);
    pseudo[11] = (uint8_t)udp_len;

    uint32_t sum = checksum_add_bytes(0, pseudo, sizeof(pseudo));
    sum = checksum_add_bytes(sum, udp, udp_len);
    uint16_t csum = checksum_finish(sum);
    return csum ? csum : 0xFFFFu;
}

/* --- Pluggable NIC driver --- */

int netif_register(uint8_t mac[6], net_send_func_t send, net_poll_func_t poll) {
    if (g_netif_count >= 2) return -1; // Only support 2 interfaces for now
    struct net_if *iface = &g_netif_list[g_netif_count];
    memcpy(iface->mac, mac, 6);
    iface->send = send;
    iface->poll = poll;
    iface->ip = 0;
    iface->netmask = 0;
    iface->gw_ip = 0;
    g_netif_count++;
    // Prefer USB (first registered), fallback to NIC (second)
    if (!g_active_netif) g_active_netif = iface;
    memset(arp_cache, 0, sizeof(arp_cache));
    return g_netif_count - 1;
}

// Legacy API for single netif
void net_register_if(uint8_t mac[6], net_send_func_t send, net_poll_func_t poll) {
    netif_register(mac, send, poll);
}

int netif_activate(int idx) {
    if (idx < 0 || idx >= g_netif_count) return -1;
    g_active_netif = &g_netif_list[idx];
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(pending, 0, sizeof(pending));
    return 0;
}

int netif_activate_other(net_send_func_t avoid_send) {
    for (int i = 0; i < g_netif_count; ++i) {
        if (g_netif_list[i].send == avoid_send) continue;
        return netif_activate(i);
    }
    return -1;
}


void net_poll(void) {
    static int in_net_poll = 0;
    if (in_net_poll) {
        return;
    }
    in_net_poll = 1;
    // Keep USB host enumeration progressing even while the UI is sitting in
    // an event loop. Real hardware is slower than QEMU and needs repeated
    // polls before tethered phones finish attaching.
    usb_poll();
    if (g_active_netif && g_active_netif->poll) {
        g_active_netif->poll();
    }
    dhcp_poll();
    in_net_poll = 0;
    sched_yield();
}

/* weak callbacks */
__attribute__((weak))
void udp_on_datagram(uint32_t src_ip,uint16_t src_port,const uint8_t *d,int l){
    (void)src_ip;(void)src_port;(void)d;(void)l;
}
__attribute__((weak))
void tcp_on_segment(uint32_t src_ip,uint16_t src_port,const struct tcp_hdr *th,
                    const uint8_t *pl,int len){
    (void)src_ip;(void)src_port;(void)th;(void)pl;(void)len;
}

/* --- ARP cache helpers --- */
static int arp_lookup(uint32_t ip, uint8_t mac_out[6]){
    for (int i=0;i<8;i++) if (arp_cache[i].ip==ip){
        for(int j=0;j<6;j++) mac_out[j]=arp_cache[i].mac[j];
        return 1;
    }
    return 0;
}
static void arp_store(uint32_t ip,const uint8_t mac[6]){
    for (int i=0;i<8;i++) if (arp_cache[i].ip==ip){
        for(int j=0;j<6;j++) arp_cache[i].mac[j]=mac[j];
        return;
    }
    for (int i=0;i<8;i++) if (arp_cache[i].ip==0){
        arp_cache[i].ip=ip; for(int j=0;j<6;j++) arp_cache[i].mac[j]=mac[j]; return;
    }
    arp_cache[0].ip=ip; for(int j=0;j<6;j++) arp_cache[0].mac[j]=mac[j];
}

// flush pending packets for an IP once we learn its MAC
static void flush_pending_for_ip(uint32_t ip){
    for (int i=0;i<8;i++){
        if (pending[i].dst_ip == ip){
            // attempt to send now that ARP entry exists
            printf("net: flushing pending pkt for %u\n", ip);
            net_send_ip(pending[i].dst_ip, pending[i].proto, pending[i].payload, pending[i].payload_len);
            // clear slot
            pending[i].dst_ip = 0;
            pending[i].payload_len = 0;
        }
    }
}

/* --- init --- */
void net_set_ipv4(uint32_t ip,uint32_t netmask,uint32_t gw){
    if (g_active_netif) {
        g_active_netif->ip=ip;
        g_active_netif->netmask=netmask;
        g_active_netif->gw_ip=gw;
    }
}
uint32_t net_get_ip(void){ return g_active_netif ? g_active_netif->ip : 0; }
int net_has_ipv4(void){ return g_active_netif && g_active_netif->ip != 0; }
uint32_t net_get_gateway(void){ return g_active_netif ? g_active_netif->gw_ip : 0; }
void net_set_dns(uint32_t dns_ip){ g_dns_ip = dns_ip; }
uint32_t net_get_dns(void){ return g_dns_ip; }

/* --- ARP --- */
static void send_arp_request(uint32_t target_ip){
    uint8_t buf[42];
    struct eth_hdr *e=(struct eth_hdr*)buf;
    memcpy(e->dst,bcast,6); memcpy(e->src,g_active_netif ? g_active_netif->mac : bcast,6);
    e->type=htons(ETH_TYPE_ARP);

    struct arp_ipv4 *a=(struct arp_ipv4*)(buf+sizeof(*e));
    a->htype=htons(1); a->ptype=htons(0x0800);
    a->hlen=6; a->plen=4; a->oper=htons(1);
    if (g_active_netif) {
        memcpy(a->sha,g_active_netif->mac,6);
        a->spa=htonl(g_active_netif->ip);
    }
    memset(a->tha,0,6);
    a->tpa=htonl(target_ip);
    if (g_active_netif && g_active_netif->send) g_active_netif->send(buf, sizeof(buf));
}

/* --- RX demux --- */
void net_rx(const uint8_t *frame,int len){
    if (len < 14) return;
    const struct eth_hdr *e=(const struct eth_hdr*)frame;
    uint16_t type=ntohs(e->type);

    if (type == ETH_TYPE_ARP){
        if (len < 14 + (int)sizeof(struct arp_ipv4)) return;
        const struct arp_ipv4 *a=(const struct arp_ipv4*)(frame+14);
        if (ntohs(a->htype)==1 && ntohs(a->ptype)==0x0800 && a->hlen==6 && a->plen==4){
            uint32_t spa=ntohl(a->spa), tpa=ntohl(a->tpa);
            if (spa) {
                arp_store(spa, a->sha);
                // flush any queued packets for this IP
                flush_pending_for_ip(spa);
            }
            (void)tpa; // you could reply if tpa == (g_active_netif ? g_active_netif->ip : 0)
        }
        return;
    }

    if (type == ETH_TYPE_IP){
        if (len < 14+20) return;
        const struct ip_hdr *ip=(const struct ip_hdr*)(frame+14);
        if ((ip->ver_ihl>>4) != 4) return;
        int ihl=(ip->ver_ihl & 0xF)*4; if (len < 14+ihl) return;
        int pl_len = (int)ntohs(ip->tot_len) - ihl;
        if (pl_len < 0) return;
        const uint8_t *pl = (const uint8_t*)ip + ihl;

        if (ip->proto == IP_PROTO_UDP){
            if (pl_len < (int)sizeof(struct udp_hdr)) return;
            const struct udp_hdr *u = (const struct udp_hdr*)pl;
            uint16_t src_port = ntohs(u->src);
            uint16_t dst_port = ntohs(u->dst);
            // If DNS response from server (src port 53), hand to dns module
            if (src_port == 53) {
                dns_on_response(ntohl(ip->src), pl+8, pl_len-8);
            }
            // DHCP replies (server port 67 -> client port 68)
            if (src_port == 67 && dst_port == 68) {
                dhcp_on_udp(ntohl(ip->src), pl+8, pl_len-8);
            }
            udp_on_datagram(ntohl(ip->src), ntohs(u->src), pl+8, pl_len-8);
        } else if (ip->proto == IP_PROTO_TCP){
            if (pl_len < (int)sizeof(struct tcp_hdr)) return;
            // pl points to the TCP header followed by payload; pass whole tcp segment
            const uint8_t *tcp_seg = pl;
            // hand the tcp header+payload buffer to tcp layer (tcp_on_rx expects header at start)
            tcp_on_rx(tcp_seg, pl_len, ntohl(ip->src), ntohl(ip->dst));
        }
    }
}

/* --- SEND helpers --- */

/* send an L4 payload as an IPv4 packet (proto set by caller) */
void net_send_ip(uint32_t dst_ip, uint8_t proto,
                 const uint8_t *payload, int payload_len)
{
    if (!g_active_netif || !g_active_netif->send) return;

    uint32_t next_hop = dst_ip;
    // if outside subnet, send via gateway
    if (g_active_netif && ((dst_ip ^ g_active_netif->ip) & g_active_netif->netmask) != 0) {
        next_hop = g_active_netif->gw_ip;
    }

    uint8_t buf[1514]; int off=0;

    /* Ethernet */
    struct eth_hdr *e=(struct eth_hdr*)buf;
    uint8_t dst_mac[6];
    if (dst_ip == 0xFFFFFFFFU) {
        memcpy(e->dst, bcast, 6);
    } else if (!arp_lookup(next_hop, dst_mac)) {
        // enqueue while ARP resolves
        for (int i=0;i<8;i++){
            if (pending[i].dst_ip == 0){
                pending[i].dst_ip = dst_ip;   // still track original dst
                pending[i].proto = proto;
                if (payload_len > (int)sizeof(pending[i].payload))
                    payload_len = (int)sizeof(pending[i].payload);
                pending[i].payload_len = payload_len;
                memcpy(pending[i].payload, payload, (size_t)payload_len);
                break;
            }
        }
        send_arp_request(next_hop);  // ARP the gateway, not remote IP
        return;
    } else {
        memcpy(e->dst, dst_mac, 6);
    }
    memcpy(e->src, g_active_netif->mac, 6);
    e->type = htons(ETH_TYPE_IP);
    off += sizeof(*e);

    /* IPv4 header */
    struct ip_hdr *ip=(struct ip_hdr*)(buf+off);
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->tot_len = htons((uint16_t)(sizeof(*ip) + payload_len));
    ip->id = htons(1);
    ip->frag = 0;
    ip->ttl = 64;
    ip->proto = proto;
    ip->src = htonl(g_active_netif->ip);
    ip->dst = htonl(dst_ip);
    ip->hdr_chksum = 0;
    ip->hdr_chksum = ip_checksum(ip, sizeof(*ip));
    off += sizeof(*ip);

    memcpy(buf+off, payload, (size_t)payload_len);
    off += payload_len;

    g_active_netif->send(buf, off);
}


/* convenience UDP builder (kept for your existing code) */
void net_send_udp_ipv4(uint32_t dst_ip,uint16_t dst_port,uint16_t src_port,
                       const void *payload,int len){
    uint8_t seg[8 + 1472];
    struct udp_hdr *u = (struct udp_hdr*)seg;
    int udp_len = 8 + len;
    u->src = htons(src_port);
    u->dst = htons(dst_port);
    u->len = htons((uint16_t)udp_len);
    u->chksum = 0;
    memcpy(seg+8, payload, (size_t)len);
    if (g_active_netif && g_active_netif->ip != 0) {
        u->chksum = htons(udp_checksum_ipv4(g_active_netif->ip, dst_ip, seg, udp_len));
    }
    net_send_ip(dst_ip, IP_PROTO_UDP, seg, udp_len);
}

/* optional: minimal SYN helper */
void net_send_tcp_syn(uint32_t dst_ip,uint16_t dst_port,uint16_t src_port,uint32_t seq){
    uint8_t seg[sizeof(struct tcp_hdr)];
    struct tcp_hdr *t=(struct tcp_hdr*)seg;
    t->src = htons(src_port);
    t->dst = htons(dst_port);
    t->seq = htonl(seq);
    t->ack = 0;
    t->offset_reserved = (5 << 4);
    t->flags = 0x02; /* SYN */
    t->win = htons(65535);
    t->chksum = 0;   /* your tcp.c should compute pseudo-header checksum if needed */
    t->urg_ptr = 0;
    net_send_ip(dst_ip, IP_PROTO_TCP, seg, sizeof(*t));
}
