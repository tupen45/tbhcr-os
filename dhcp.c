#include "dhcp.h"
#include "net.h"
#include "string.h"
#include "endian.h"
#include "stdio.h"
#include "stdlib.h"
#include "drivers/usb/usb_ipheth.h"
#include "drivers/usb/usb_rndis.h"
#include <stdint.h>

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY   2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6

#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER      3
#define DHCP_OPTION_DNS         6
#define DHCP_OPTION_REQ_IP      50
#define DHCP_OPTION_MSG_TYPE    53
#define DHCP_OPTION_SERVER_ID   54
#define DHCP_OPTION_PARAM_REQ   55
#define DHCP_OPTION_END         255

#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_DECLINE  4
#define DHCP_MSG_ACK      5
#define DHCP_MSG_NAK      6

#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_FIXED_LEN 240

enum dhcp_state {
    DHCP_STATE_OFF = 0,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND
};

#pragma pack(push,1)
struct dhcp_packet {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t cookie;
    uint8_t options[312];
};
#pragma pack(pop)

static enum dhcp_state g_state = DHCP_STATE_OFF;
static uint32_t g_xid = 0;
static uint32_t g_offer_ip = 0;
static uint32_t g_server_id = 0;
static uint32_t g_offer_mask = 0;
static uint32_t g_offer_router = 0;
static uint32_t g_offer_dns = 0;
static int g_poll_ticks = 0;
static int g_retries = 0;

static void dhcp_apply_slirp_fallback(void) {
    // QEMU usernet defaults: guest 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3
    uint32_t ip = (10u << 24) | (0u << 16) | (2u << 8) | 15u;
    uint32_t nm = (255u << 24) | (255u << 16) | (255u << 8) | 0u;
    uint32_t gw = (10u << 24) | (0u << 16) | (2u << 8) | 2u;
    uint32_t dns = (10u << 24) | (0u << 16) | (2u << 8) | 3u;
    net_set_ipv4(ip, nm, gw);
    net_set_dns(dns);
    g_state = DHCP_STATE_BOUND;
    printf("dhcp: using QEMU slirp fallback ip=10.0.2.15 gw=10.0.2.2 dns=10.0.2.3\n");
}

static void dhcp_reset_offer(void) {
    g_offer_ip = 0;
    g_server_id = 0;
    g_offer_mask = 0;
    g_offer_router = 0;
    g_offer_dns = 0;
}

static void dhcp_send_discover(void) {
    if (!g_active_netif) return;

    uint8_t pkt[576];
    memset(pkt, 0, sizeof(pkt));
    struct dhcp_packet *d = (struct dhcp_packet*)pkt;

    d->op = DHCP_OP_BOOTREQUEST;
    d->htype = DHCP_HTYPE_ETHERNET;
    d->hlen = DHCP_HLEN_ETHERNET;
    d->xid = htonl(g_xid);
    d->flags = htons(0x8000); // broadcast reply
    memcpy(d->chaddr, g_active_netif->mac, 6);
    d->cookie = htonl(DHCP_MAGIC_COOKIE);

    uint8_t *o = d->options;
    *o++ = DHCP_OPTION_MSG_TYPE; *o++ = 1; *o++ = DHCP_MSG_DISCOVER;
    *o++ = DHCP_OPTION_PARAM_REQ; *o++ = 4;
    *o++ = DHCP_OPTION_SUBNET_MASK;
    *o++ = DHCP_OPTION_ROUTER;
    *o++ = DHCP_OPTION_DNS;
    *o++ = DHCP_OPTION_SERVER_ID;
    *o++ = DHCP_OPTION_END;

    int plen = (int)(o - (uint8_t*)pkt);
    if (plen < 300) plen = 300;
    net_send_udp_ipv4(0xFFFFFFFFU, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, pkt, plen);
    printf("dhcp: sent DISCOVER xid=0x%08x\n", g_xid);
}

static void dhcp_send_request(void) {
    if (!g_active_netif || g_offer_ip == 0 || g_server_id == 0) return;

    uint8_t pkt[576];
    memset(pkt, 0, sizeof(pkt));
    struct dhcp_packet *d = (struct dhcp_packet*)pkt;

    d->op = DHCP_OP_BOOTREQUEST;
    d->htype = DHCP_HTYPE_ETHERNET;
    d->hlen = DHCP_HLEN_ETHERNET;
    d->xid = htonl(g_xid);
    d->flags = htons(0x8000);
    memcpy(d->chaddr, g_active_netif->mac, 6);
    d->cookie = htonl(DHCP_MAGIC_COOKIE);

    uint8_t *o = d->options;
    *o++ = DHCP_OPTION_MSG_TYPE; *o++ = 1; *o++ = DHCP_MSG_REQUEST;
    *o++ = DHCP_OPTION_REQ_IP; *o++ = 4;
    *((uint32_t*)o) = htonl(g_offer_ip); o += 4;
    *o++ = DHCP_OPTION_SERVER_ID; *o++ = 4;
    *((uint32_t*)o) = htonl(g_server_id); o += 4;
    *o++ = DHCP_OPTION_PARAM_REQ; *o++ = 4;
    *o++ = DHCP_OPTION_SUBNET_MASK;
    *o++ = DHCP_OPTION_ROUTER;
    *o++ = DHCP_OPTION_DNS;
    *o++ = DHCP_OPTION_SERVER_ID;
    *o++ = DHCP_OPTION_END;

    int plen = (int)(o - (uint8_t*)pkt);
    if (plen < 300) plen = 300;
    net_send_udp_ipv4(0xFFFFFFFFU, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, pkt, plen);
    printf("dhcp: sent REQUEST ip=%u.%u.%u.%u\n",
           (g_offer_ip >> 24) & 0xFF, (g_offer_ip >> 16) & 0xFF,
           (g_offer_ip >> 8) & 0xFF, g_offer_ip & 0xFF);
}

void dhcp_start(void) {
    if (!g_active_netif) return;
    if (g_state == DHCP_STATE_BOUND ||
        g_state == DHCP_STATE_SELECTING ||
        g_state == DHCP_STATE_REQUESTING) return;

    dhcp_reset_offer();
    g_state = DHCP_STATE_SELECTING;
    g_poll_ticks = 0;
    g_retries = 0;
    g_xid = ((uint32_t)rand() << 1) ^ 0xA5A50001U;
    if (g_xid == 0) g_xid = 0x10203040U;
    net_set_ipv4(0, 0, 0);
    net_set_dns(0);
    dhcp_send_discover();
}

void dhcp_stop(void) {
    g_state = DHCP_STATE_OFF;
    g_poll_ticks = 0;
    g_retries = 0;
    dhcp_reset_offer();
}

int dhcp_is_bound(void) {
    return g_state == DHCP_STATE_BOUND;
}

void dhcp_poll(void) {
    if (!g_active_netif) return;
    if (g_state == DHCP_STATE_OFF || g_state == DHCP_STATE_BOUND) return;

    g_poll_ticks++;
    if (g_poll_ticks < 5000) return;
    g_poll_ticks = 0;

    if (g_retries >= 8) {
        printf("dhcp: timeout\n");
        if (!rndis_active && !ipheth_active) {
            dhcp_apply_slirp_fallback();
        } else {
            printf("dhcp: giving up on USB DHCP (no fallback for tether mode)\n");
            g_state = DHCP_STATE_OFF;
        }
        return;
    }
    g_retries++;

    if (g_state == DHCP_STATE_SELECTING) {
        dhcp_send_discover();
    } else if (g_state == DHCP_STATE_REQUESTING) {
        dhcp_send_request();
    }
}

void dhcp_on_udp(uint32_t src_ip, const uint8_t *data, int len) {
    (void)src_ip;
    if (!data || len < DHCP_FIXED_LEN) return;
    if (g_state == DHCP_STATE_OFF || g_state == DHCP_STATE_BOUND) return;

    const struct dhcp_packet *d = (const struct dhcp_packet*)data;
    if (d->op != DHCP_OP_BOOTREPLY ||
        d->htype != DHCP_HTYPE_ETHERNET ||
        d->hlen != DHCP_HLEN_ETHERNET) return;
    if (ntohl(d->xid) != g_xid) return;
    if (d->cookie != htonl(DHCP_MAGIC_COOKIE)) return;
    if (!g_active_netif) return;
    for (int i = 0; i < 6; ++i) {
        if (d->chaddr[i] != g_active_netif->mac[i]) return;
    }

    uint8_t msg_type = 0;
    uint32_t subnet_mask = 0;
    uint32_t router = 0;
    uint32_t dns = 0;
    uint32_t server_id = 0;

    const uint8_t *opt = data + DHCP_FIXED_LEN;
    const uint8_t *opt_end = data + len;
    while (opt < opt_end) {
        uint8_t code = *opt++;
        if (code == DHCP_OPTION_END) break;
        if (code == 0) continue;
        if (opt >= opt_end) break;
        uint8_t olen = *opt++;
        if ((opt + olen) > opt_end) break;

        if (code == DHCP_OPTION_MSG_TYPE && olen >= 1) msg_type = opt[0];
        else if (code == DHCP_OPTION_SUBNET_MASK && olen >= 4) subnet_mask = ntohl(*(const uint32_t*)opt);
        else if (code == DHCP_OPTION_ROUTER && olen >= 4) router = ntohl(*(const uint32_t*)opt);
        else if (code == DHCP_OPTION_DNS && olen >= 4) dns = ntohl(*(const uint32_t*)opt);
        else if (code == DHCP_OPTION_SERVER_ID && olen >= 4) server_id = ntohl(*(const uint32_t*)opt);

        opt += olen;
    }

    if (g_state == DHCP_STATE_SELECTING && msg_type == DHCP_MSG_OFFER) {
        g_offer_ip = ntohl(d->yiaddr);
        g_server_id = server_id;
        g_offer_mask = subnet_mask;
        g_offer_router = router;
        g_offer_dns = dns;
        if (g_offer_ip == 0 || g_server_id == 0) return;
        g_state = DHCP_STATE_REQUESTING;
        g_retries = 0;
        g_poll_ticks = 0;
        printf("dhcp: got OFFER ip=%u.%u.%u.%u\n",
               (g_offer_ip >> 24) & 0xFF, (g_offer_ip >> 16) & 0xFF,
               (g_offer_ip >> 8) & 0xFF, g_offer_ip & 0xFF);
        dhcp_send_request();
        return;
    }

    if (g_state == DHCP_STATE_REQUESTING && msg_type == DHCP_MSG_ACK) {
        uint32_t ip = ntohl(d->yiaddr);
        if (ip == 0) ip = g_offer_ip;
        if (subnet_mask == 0) subnet_mask = g_offer_mask ? g_offer_mask : ((uint32_t)255 << 24) | ((uint32_t)255 << 16) | ((uint32_t)255 << 8);
        if (router == 0) router = g_offer_router;
        if (dns == 0) dns = g_offer_dns;
        net_set_ipv4(ip, subnet_mask, router);
        net_set_dns(dns);
        g_state = DHCP_STATE_BOUND;
        printf("dhcp: BOUND ip=%u.%u.%u.%u gw=%u.%u.%u.%u dns=%u.%u.%u.%u\n",
               (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
               (router >> 24) & 0xFF, (router >> 16) & 0xFF, (router >> 8) & 0xFF, router & 0xFF,
               (dns >> 24) & 0xFF, (dns >> 16) & 0xFF, (dns >> 8) & 0xFF, dns & 0xFF);
        return;
    }

    if (g_state == DHCP_STATE_REQUESTING && msg_type == DHCP_MSG_NAK) {
        printf("dhcp: got NAK, restarting\n");
        g_state = DHCP_STATE_SELECTING;
        g_retries = 0;
        g_poll_ticks = 0;
        dhcp_send_discover();
    }
}
