#pragma once
#include <stdint.h>

#define ETH_ADDR_LEN 6

// Function pointer types for a network driver
typedef int (*net_send_func_t)(const void *data, int len);
typedef void (*net_poll_func_t)(void);

struct net_if {
    uint8_t mac[ETH_ADDR_LEN];
    uint32_t ip; // host order
    uint32_t gw_ip; // host order
    uint32_t netmask; // host order
    net_send_func_t send;
    net_poll_func_t poll;
};

extern struct net_if *g_active_netif;

// Register a network interface driver
int netif_register(uint8_t mac[6], net_send_func_t send, net_poll_func_t poll);
void net_register_if(uint8_t mac[6], net_send_func_t send, net_poll_func_t poll);
int netif_activate(int idx);
int netif_activate_other(net_send_func_t avoid_send);

void net_set_ipv4(uint32_t ip, uint32_t netmask, uint32_t gw);
uint32_t net_get_ip(void);
int net_has_ipv4(void);
uint32_t net_get_gateway(void);
void net_set_dns(uint32_t dns_ip);
uint32_t net_get_dns(void);

// low level - will now use the function pointer in g_netif
void nic_tx(const void *data, int len);

// high-level helpers
void net_send_udp_ipv4(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
const void *payload, int len);

void net_send_ip(uint32_t dst_ip, uint8_t proto, const uint8_t *payload, int payload_len);

// RX entry point from NIC driver
void net_rx(const uint8_t *frame, int len);

// Main network poll function
void net_poll(void);
