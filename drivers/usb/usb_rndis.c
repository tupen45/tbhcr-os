// RNDIS (USB tethering) driver for newos

#include "drivers/usb/usb_rndis.h"
#include "drivers/usb/ehci.h"
#include "usb_host.h"
#include "net.h"
#include "kmalloc.h"
#include "dhcp.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// --- RNDIS Driver State ---
static const struct usb_device_info* rndis_dev = NULL;
static const struct usb_interface_info* rndis_ctrl_intf = NULL;
static const struct usb_interface_info* rndis_data_intf = NULL;
static uint8_t rndis_mac[6];
int rndis_active = 0;
static uint32_t rndis_request_id = 1;
static int rndis_bound = 0;

static uint8_t bulk_in_ep = 0;
static uint8_t bulk_out_ep = 0;
static uint16_t bulk_in_max_packet = 0;
static uint16_t bulk_out_max_packet = 0;

static int is_rndis_ctrl_interface(const struct usb_interface_info *intf) {
    return intf &&
           intf->intf_class == USB_CLASS_WIRELESS &&
           intf->intf_subclass == USB_SUBCLASS_RNDIS &&
           intf->intf_protocol == USB_PROTOCOL_RNDIS;
}

static int interface_find_bulk_eps(const struct usb_interface_info *intf,
                                   uint8_t *in_ep, uint8_t *out_ep,
                                   uint16_t *in_mps, uint16_t *out_mps) {
    if (!intf) return 0;

    int have_in = 0;
    int have_out = 0;
    for (int e = 0; e < intf->num_endpoints; ++e) {
        uint8_t addr = intf->endpoints[e].address;
        uint8_t attrs = intf->endpoints[e].type;
        if ((attrs & 0x03) != 2) continue;
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

static void delay_small(void) {
    for (volatile int i = 0; i < 40000; ++i) {
        __asm__ volatile("nop");
    }
}

static uint8_t *rndis_info_ptr(void *msg_start, uint32_t info_offset) {
    // RNDIS offsets are relative to RequestID field (message start + 8).
    return ((uint8_t*)msg_start + 8u + info_offset);
}

static int rndis_control_msg(void* msg, int msg_len, void* response_buf, int response_len) {
    if (!rndis_dev || !msg || !response_buf || msg_len <= 0 || response_len <= 0) return 0;

    usb_setup_packet_t setup;
    memset(&setup, 0, sizeof(setup));

    setup.bmRequestType = 0x21; // Host->Device, Class, Interface
    setup.bRequest = REQ_SEND_ENCAPSULATED_COMMAND;
    setup.wValue = 0;
    setup.wIndex = rndis_ctrl_intf ? rndis_ctrl_intf->intf_number : 0;
    setup.wLength = (uint16_t)msg_len;
    if (!usb_control_transfer(rndis_dev->addr, 0, rndis_dev->max_packet_size0, &setup, msg, msg_len)) {
        printf("rndis: SEND_ENCAPSULATED_COMMAND failed\n");
        return 0;
    }

    setup.bmRequestType = 0xA1; // Device->Host, Class, Interface
    setup.bRequest = REQ_GET_ENCAPSULATED_RESPONSE;
    setup.wValue = 0;
    setup.wIndex = rndis_ctrl_intf ? rndis_ctrl_intf->intf_number : 0;
    setup.wLength = (uint16_t)response_len;

    for (int tries = 0; tries < 32; ++tries) {
        memset(response_buf, 0, (size_t)response_len);
        if (usb_control_transfer(rndis_dev->addr, 0, rndis_dev->max_packet_size0, &setup, response_buf, response_len)) {
            rndis_header_t *hdr = (rndis_header_t*)response_buf;
            if (hdr->MessageLength >= sizeof(rndis_header_t) && hdr->MessageLength <= (uint32_t)response_len) {
                return 1;
            }
        }
        delay_small();
    }

    printf("rndis: GET_ENCAPSULATED_RESPONSE timed out\n");
    return 0;
}

// --- RNDIS Driver Initialization ---
void usb_rndis_init(void) {
    if (rndis_active || rndis_bound) return;

    int n = usb_get_device_count();
    if (n == 0) return;

    const struct usb_device_info *candidate = NULL;
    const struct usb_interface_info *ctrl_intf = NULL;
    const struct usb_interface_info *data_intf = NULL;
    uint8_t in_ep = 0, out_ep = 0;
    uint16_t in_mps = 0, out_mps = 0;

    for (int i = 0; i < n; ++i) {
        const struct usb_device_info *dev = usb_get_device(i);
        if (!dev || !dev->active) continue;

        const struct usb_interface_info *dev_ctrl = NULL;
        const struct usb_interface_info *dev_data = NULL;
        uint8_t dev_in_ep = 0, dev_out_ep = 0;
        uint16_t dev_in_mps = 0, dev_out_mps = 0;

        for (int j = 0; j < dev->num_interfaces; ++j) {
            const struct usb_interface_info *intf = &dev->interfaces[j];
            if (is_rndis_ctrl_interface(intf)) {
                dev_ctrl = intf;
                if (!dev_data &&
                    interface_find_bulk_eps(intf, &dev_in_ep, &dev_out_ep, &dev_in_mps, &dev_out_mps)) {
                    dev_data = intf;
                }
            }
            if (intf->intf_class == USB_CLASS_CDC_DATA &&
                interface_find_bulk_eps(intf, &dev_in_ep, &dev_out_ep, &dev_in_mps, &dev_out_mps)) {
                dev_data = intf;
            }
        }

        if (!dev_ctrl) dev_ctrl = dev_data;
        if (!dev_ctrl || !dev_data) continue;

        candidate = dev;
        ctrl_intf = dev_ctrl;
        data_intf = dev_data;
        in_ep = dev_in_ep;
        out_ep = dev_out_ep;
        in_mps = dev_in_mps;
        out_mps = dev_out_mps;
        break;
    }

    if (!candidate) {
        return;
    }

    rndis_dev = candidate;
    rndis_ctrl_intf = ctrl_intf;
    rndis_data_intf = data_intf;
    bulk_in_ep = in_ep;
    bulk_out_ep = out_ep;
    bulk_in_max_packet = in_mps;
    bulk_out_max_packet = out_mps;
    rndis_bound = 1;

    printf("rndis: candidate addr=%d ctrl=%u data=%u alt=%u IN=0x%02x OUT=0x%02x\n",
           rndis_dev->addr,
           rndis_ctrl_intf ? rndis_ctrl_intf->intf_number : 0,
           rndis_data_intf ? rndis_data_intf->intf_number : 0,
           rndis_data_intf ? rndis_data_intf->alt_setting : 0,
           bulk_in_ep, bulk_out_ep);

    if (rndis_data_intf && rndis_data_intf->alt_setting != 0 &&
        !usb_set_interface(rndis_dev->addr, rndis_data_intf->intf_number,
                           rndis_data_intf->alt_setting, rndis_dev->max_packet_size0)) {
        printf("rndis: failed to select data alt setting %u on interface %u\n",
               rndis_data_intf->alt_setting, rndis_data_intf->intf_number);
        rndis_bound = 0;
        rndis_ctrl_intf = NULL;
        rndis_data_intf = NULL;
        rndis_dev = NULL;
        return;
    }

    if (!usb_configure_interface(rndis_dev->addr, rndis_data_intf)) {
        printf("rndis: failed to configure data interface endpoints\n");
        rndis_bound = 0;
        rndis_ctrl_intf = NULL;
        rndis_data_intf = NULL;
        rndis_dev = NULL;
        return;
    }

    uint8_t buf[1024];
    memset(buf, 0, sizeof(buf));

    rndis_init_t* init_msg = (rndis_init_t*)buf;
    init_msg->MessageType = RNDIS_MSG_INIT;
    init_msg->MessageLength = sizeof(rndis_init_t);
    init_msg->RequestID = rndis_request_id++;
    init_msg->MajorVersion = 1;
    init_msg->MinorVersion = 0;
    init_msg->MaxTransferSize = sizeof(buf);

    if (!rndis_control_msg(init_msg, sizeof(rndis_init_t), buf, sizeof(buf))) {
        rndis_bound = 0;
        return;
    }
    rndis_init_cmplt_t* init_cmplt = (rndis_init_cmplt_t*)buf;
    if (init_cmplt->MessageType != RNDIS_MSG_INIT_C || init_cmplt->Status != 0) {
        printf("rndis: INIT failed (type=0x%08x status=0x%08x)\n",
               init_cmplt->MessageType, init_cmplt->Status);
        rndis_bound = 0;
        return;
    }

    memset(buf, 0, sizeof(buf));
    rndis_query_t* query_msg = (rndis_query_t*)buf;
    query_msg->MessageType = RNDIS_MSG_QUERY;
    query_msg->MessageLength = sizeof(rndis_query_t);
    query_msg->RequestID = rndis_request_id++;
    query_msg->Oid = OID_GEN_PERMANENT_MAC_ADDRESS;
    query_msg->InformationBufferLength = 0;
    query_msg->InformationBufferOffset = 0;
    query_msg->DeviceVcHandle = 0;

    if (!rndis_control_msg(query_msg, sizeof(rndis_query_t), buf, sizeof(buf))) {
        rndis_bound = 0;
        return;
    }

    rndis_query_cmplt_t* query_cmplt = (rndis_query_cmplt_t*)buf;
    if (query_cmplt->MessageType != RNDIS_MSG_QUERY_C || query_cmplt->Status != 0 ||
        query_cmplt->InformationBufferLength < 6) {
        printf("rndis: MAC query failed (type=0x%08x status=0x%08x len=%u)\n",
               query_cmplt->MessageType, query_cmplt->Status, query_cmplt->InformationBufferLength);
        rndis_bound = 0;
        return;
    }
    uint8_t *mac_ptr = rndis_info_ptr(query_cmplt, query_cmplt->InformationBufferOffset);
    memcpy(rndis_mac, mac_ptr, 6);

    memset(buf, 0, sizeof(buf));
    rndis_set_t* set_msg = (rndis_set_t*)buf;
    set_msg->MessageType = RNDIS_MSG_SET;
    set_msg->MessageLength = sizeof(rndis_set_t) + 4;
    set_msg->RequestID = rndis_request_id++;
    set_msg->Oid = OID_GEN_CURRENT_PACKET_FILTER;
    set_msg->InformationBufferLength = 4;
    set_msg->InformationBufferOffset = sizeof(rndis_set_t) - sizeof(rndis_header_t);
    set_msg->DeviceVcHandle = 0;
    uint32_t* filter = (uint32_t*)(buf + sizeof(rndis_set_t));
    *filter = RNDIS_PACKET_TYPE_DIRECTED | RNDIS_PACKET_TYPE_BROADCAST | RNDIS_PACKET_TYPE_ALL_MULTICAST;

    if (!rndis_control_msg(set_msg, sizeof(rndis_set_t) + 4, buf, sizeof(buf))) {
        rndis_bound = 0;
        return;
    }
    rndis_set_cmplt_t* set_cmplt = (rndis_set_cmplt_t*)buf;
    if (set_cmplt->MessageType != RNDIS_MSG_SET_C || set_cmplt->Status != 0) {
        printf("rndis: SET packet filter failed (type=0x%08x status=0x%08x)\n",
               set_cmplt->MessageType, set_cmplt->Status);
        rndis_bound = 0;
        return;
    }

    int netif = netif_register(rndis_mac, usb_rndis_send, usb_rndis_poll);
    if (netif < 0) {
        printf("rndis: netif registration failed\n");
        rndis_bound = 0;
        return;
    }
    netif_activate(netif);

    rndis_active = 1;
    printf("rndis: attached MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
           rndis_mac[0], rndis_mac[1], rndis_mac[2],
           rndis_mac[3], rndis_mac[4], rndis_mac[5]);
    printf("rndis: starting DHCP\n");
    dhcp_start();
}

int usb_rndis_send(const void *data, int len) {
    if (!rndis_active || !rndis_dev || !data || len <= 0) return -1;

    int packet_len = (int)sizeof(rndis_packet_t) + len;
    rndis_packet_t* pkt = (rndis_packet_t*)kmalloc((size_t)packet_len);
    if (!pkt) return -1;
    memset(pkt, 0, (size_t)packet_len);

    pkt->MessageType = RNDIS_MSG_PACKET;
    pkt->MessageLength = (uint32_t)packet_len;
    /* RNDIS data offsets are relative to byte 8 of the message. */
    pkt->DataOffset = (uint32_t)sizeof(rndis_packet_t) - 8u;
    pkt->DataLength = (uint32_t)len;
    memcpy((uint8_t*)pkt + sizeof(rndis_packet_t), data, (size_t)len);

    int success = usb_bulk_transfer(rndis_dev->addr, bulk_out_ep, bulk_out_max_packet, 0, pkt, packet_len);
    kfree(pkt);
    return success ? len : -1;
}

void usb_rndis_poll(void) {
    if (!rndis_active || !rndis_dev) return;

    uint8_t buf[4096];
    if (!usb_bulk_transfer(rndis_dev->addr, bulk_in_ep, bulk_in_max_packet, 1, buf, sizeof(buf))) {
        return;
    }

    rndis_packet_t* pkt = (rndis_packet_t*)buf;
    if (pkt->MessageType != RNDIS_MSG_PACKET) return;
    if (pkt->DataLength == 0) return;
    if (pkt->MessageLength < sizeof(rndis_packet_t) || pkt->MessageLength > sizeof(buf)) return;

    uint8_t *payload = (uint8_t*)pkt + 8u + pkt->DataOffset;
    uint8_t *end = buf + pkt->MessageLength;
    if (payload >= end) return;
    if (payload + pkt->DataLength > end) return;
    net_rx(payload, (int)pkt->DataLength);
}

void usb_rndis_handle_disconnect(int dev_addr) {
    if (!rndis_dev) return;
    if (rndis_dev->addr != dev_addr) return;

    printf("rndis: device %d disconnected, dropping link\n", dev_addr);
    rndis_active = 0;
    rndis_bound = 0;
    rndis_dev = NULL;
    rndis_ctrl_intf = NULL;
    rndis_data_intf = NULL;
    bulk_in_ep = 0;
    bulk_out_ep = 0;
    bulk_in_max_packet = 0;
    bulk_out_max_packet = 0;
    net_set_ipv4(0, 0, 0);
    net_set_dns(0);
    dhcp_stop();
    if (g_active_netif && g_active_netif->send == usb_rndis_send) {
        if (netif_activate_other(usb_rndis_send) != 0) {
            g_active_netif = NULL;
        } else if (g_active_netif && g_active_netif->ip == 0) {
            dhcp_start();
        }
    }
}
