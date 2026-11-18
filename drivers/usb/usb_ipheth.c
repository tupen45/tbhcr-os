// Apple iPhone USB Ethernet (ipheth) driver for newos.

#include "drivers/usb/usb_ipheth.h"
#include "drivers/usb/ehci.h"
#include "drivers/usb/usb_host.h"
#include "net.h"
#include "dhcp.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IPHETH_INTFNUM             2
#define IPHETH_ALT_INTFNUM         1
#define IPHETH_CTRL_ENDP           0
#define IPHETH_CTRL_BUF_SIZE       64
#define IPHETH_CMD_GET_MACADDR     0x00
#define IPHETH_CMD_CARRIER_CHECK   0x45
#define IPHETH_CARRIER_ON          0x04
#define IPHETH_IP_ALIGN            2
#define IPHETH_RX_BUF_SIZE         1536

static const struct usb_device_info *ipheth_dev = NULL;
static const struct usb_interface_info *ipheth_intf = NULL;
static uint8_t ipheth_mac[6];
static uint8_t ipheth_bulk_in = 0;
static uint8_t ipheth_bulk_out = 0;
static uint16_t ipheth_bulk_in_mps = 0;
static uint16_t ipheth_bulk_out_mps = 0;
static int ipheth_bound = 0;
int ipheth_active = 0;

int usb_ipheth_is_candidate_class(uint8_t intf_class, uint8_t intf_subclass, uint8_t intf_protocol) {
    return intf_class == USB_CLASS_VENDOR_SPEC &&
           intf_subclass == IPHETH_USBINTF_SUBCLASS &&
           intf_protocol == IPHETH_USBINTF_PROTOCOL;
}

int usb_ipheth_is_apple_candidate_interface(uint8_t intf_number,
                                            uint8_t intf_class,
                                            uint8_t intf_subclass,
                                            uint8_t intf_protocol) {
    if (usb_ipheth_is_candidate_class(intf_class, intf_subclass, intf_protocol)) {
        return 1;
    }
    return intf_number == IPHETH_INTFNUM && intf_class == USB_CLASS_VENDOR_SPEC;
}

static int ipheth_is_apple_tether_interface(const struct usb_interface_info *intf) {
    if (!intf) return 0;
    return usb_ipheth_is_apple_candidate_interface(intf->intf_number,
                                                   intf->intf_class,
                                                   intf->intf_subclass,
                                                   intf->intf_protocol);
}

static int ipheth_find_bulk_eps(const struct usb_interface_info *intf,
                                uint8_t *in_ep, uint8_t *out_ep,
                                uint16_t *in_mps, uint16_t *out_mps) {
    if (!intf) return 0;

    int have_in = 0;
    int have_out = 0;
    for (int e = 0; e < intf->num_endpoints; ++e) {
        uint8_t addr = intf->endpoints[e].address;
        uint8_t type = intf->endpoints[e].type;
        if ((type & 0x03) != 2) continue;
        if (addr & 0x80) {
            have_in = 1;
            if (in_ep) *in_ep = addr;
            if (in_mps) *in_mps = intf->endpoints[e].max_packet_size;
        } else {
            have_out = 1;
            if (out_ep) *out_ep = addr;
            if (out_mps) *out_mps = intf->endpoints[e].max_packet_size;
        }
    }
    return have_in && have_out;
}

static int ipheth_control_in(uint8_t request, void *buf, int len) {
    usb_setup_packet_t setup;
    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = 0xC0;
    setup.bRequest = request;
    setup.wValue = 0;
    setup.wIndex = ipheth_intf ? ipheth_intf->intf_number : IPHETH_INTFNUM;
    setup.wLength = (uint16_t)len;
    return usb_control_transfer(ipheth_dev->addr, IPHETH_CTRL_ENDP,
                                ipheth_dev->max_packet_size0, &setup, buf, len);
}

static int ipheth_carrier_on(void) {
    uint8_t ctrl[IPHETH_CTRL_BUF_SIZE];
    memset(ctrl, 0, sizeof(ctrl));
    if (!ipheth_control_in(IPHETH_CMD_CARRIER_CHECK, ctrl, sizeof(ctrl))) {
        return 1;
    }
    return ctrl[0] == IPHETH_CARRIER_ON || ctrl[1] == IPHETH_CARRIER_ON;
}

void usb_ipheth_init(void) {
    if (ipheth_active || ipheth_bound) return;

    const struct usb_device_info *candidate = NULL;
    const struct usb_interface_info *data_intf = NULL;
    uint8_t in_ep = 0, out_ep = 0;
    uint16_t in_mps = 0, out_mps = 0;

    int n = usb_get_device_count();
    for (int i = 0; i < n; ++i) {
        const struct usb_device_info *dev = usb_get_device(i);
        if (!dev || !dev->active || dev->vid != USB_VENDOR_APPLE) continue;

        for (int j = 0; j < dev->num_interfaces; ++j) {
            const struct usb_interface_info *intf = &dev->interfaces[j];
            if (intf->alt_setting != IPHETH_ALT_INTFNUM) {
                continue;
            }
            if (!ipheth_find_bulk_eps(intf, &in_ep, &out_ep, &in_mps, &out_mps)) {
                continue;
            }
            if (!ipheth_is_apple_tether_interface(intf)) {
                printf("ipheth: Apple fallback interface %u alt %u class=0x%x sub=0x%x proto=0x%x\n",
                       intf->intf_number, intf->alt_setting,
                       intf->intf_class, intf->intf_subclass, intf->intf_protocol);
            }
            candidate = dev;
            data_intf = intf;
            break;
        }
        if (candidate) break;
    }

    if (!candidate || !data_intf) return;

    ipheth_dev = candidate;
    ipheth_intf = data_intf;
    ipheth_bulk_in = in_ep;
    ipheth_bulk_out = out_ep;
    ipheth_bulk_in_mps = in_mps;
    ipheth_bulk_out_mps = out_mps;
    ipheth_bound = 1;

    printf("ipheth: candidate addr=%d interface=%u alt=%u IN=0x%02x OUT=0x%02x\n",
           ipheth_dev->addr, ipheth_intf->intf_number, ipheth_intf->alt_setting,
           ipheth_bulk_in, ipheth_bulk_out);

    if (!usb_set_interface(ipheth_dev->addr, ipheth_intf->intf_number, ipheth_intf->alt_setting,
                           ipheth_dev->max_packet_size0)) {
        printf("ipheth: failed to select interface %u alt %u\n",
               ipheth_intf->intf_number, ipheth_intf->alt_setting);
        ipheth_bound = 0;
        ipheth_dev = NULL;
        ipheth_intf = NULL;
        return;
    }

    if (!usb_configure_interface(ipheth_dev->addr, ipheth_intf)) {
        printf("ipheth: failed to configure xHCI bulk endpoints\n");
        ipheth_bound = 0;
        ipheth_dev = NULL;
        ipheth_intf = NULL;
        return;
    }

    uint8_t ctrl[IPHETH_CTRL_BUF_SIZE];
    memset(ctrl, 0, sizeof(ctrl));
    if (!ipheth_control_in(IPHETH_CMD_GET_MACADDR, ctrl, sizeof(ctrl))) {
        printf("ipheth: MAC request failed; unlock/trust iPhone and enable hotspot\n");
        ipheth_bound = 0;
        ipheth_dev = NULL;
        ipheth_intf = NULL;
        return;
    }
    memcpy(ipheth_mac, ctrl, 6);

    int netif = netif_register(ipheth_mac, usb_ipheth_send, usb_ipheth_poll);
    if (netif < 0) {
        printf("ipheth: netif registration failed\n");
        ipheth_bound = 0;
        return;
    }
    netif_activate(netif);
    ipheth_active = 1;

    printf("ipheth: attached MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
           ipheth_mac[0], ipheth_mac[1], ipheth_mac[2],
           ipheth_mac[3], ipheth_mac[4], ipheth_mac[5]);
    dhcp_start();
}

int usb_ipheth_send(const void *data, int len) {
    if (!ipheth_active || !ipheth_dev || !data || len <= 0) return -1;
    if (!ipheth_carrier_on()) return -1;
    return usb_bulk_transfer(ipheth_dev->addr, ipheth_bulk_out, ipheth_bulk_out_mps,
                             0, (void *)data, len) ? len : -1;
}

void usb_ipheth_poll(void) {
    if (!ipheth_active || !ipheth_dev) return;
    if (!ipheth_carrier_on()) return;

    uint8_t buf[IPHETH_RX_BUF_SIZE];
    int actual = 0;
    if (!usb_bulk_transfer_actual(ipheth_dev->addr, ipheth_bulk_in, ipheth_bulk_in_mps,
                                  1, buf, sizeof(buf), &actual)) {
        return;
    }
    if (actual == 4 && buf[0] == 0x00 && buf[1] == 0x01) {
        return;
    }
    if (actual > IPHETH_IP_ALIGN + 14) {
        net_rx(buf + IPHETH_IP_ALIGN, actual - IPHETH_IP_ALIGN);
    }
}

void usb_ipheth_handle_disconnect(int dev_addr) {
    if (!ipheth_dev || ipheth_dev->addr != dev_addr) return;

    printf("ipheth: device %d disconnected, dropping link\n", dev_addr);
    ipheth_active = 0;
    ipheth_bound = 0;
    ipheth_dev = NULL;
    ipheth_intf = NULL;
    ipheth_bulk_in = 0;
    ipheth_bulk_out = 0;
    ipheth_bulk_in_mps = 0;
    ipheth_bulk_out_mps = 0;
    net_set_ipv4(0, 0, 0);
    net_set_dns(0);
    dhcp_stop();
    if (g_active_netif && g_active_netif->send == usb_ipheth_send) {
        if (netif_activate_other(usb_ipheth_send) != 0) {
            g_active_netif = NULL;
        } else if (g_active_netif && g_active_netif->ip == 0) {
            dhcp_start();
        }
    }
}
