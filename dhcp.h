#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>

void dhcp_start(void);
void dhcp_stop(void);
void dhcp_poll(void);
int dhcp_is_bound(void);
void dhcp_on_udp(uint32_t src_ip, const uint8_t *data, int len);

#endif
