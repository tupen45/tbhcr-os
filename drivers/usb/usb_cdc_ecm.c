#include <stdio.h>
#include <string.h>
#include "drivers/usb/usb_host.h"
#include "drivers/usb/usb_cdc_ecm.h"
#include "net.h"
#include "dhcp.h"

#define ECM_RX_BUF_SIZE 2048

int cdc_ecm_active = 0;

static const struct usb_device_info *ecm_dev = NULL;
static const struct usb_interface_info *ecm_data_intf = NULL;
static uint8_t ecm_ctrl_intf_num = 0;
static uint8_t ecm_bulk_in = 0;
static uint8_t ecm_bulk_out = 0;
static uint16_t ecm_bulk_in_mps = 0;
static uint16_t ecm_bulk_out_mps = 0;
static int ecm_bound = 0;
static uint8_t ecm_mac[6] = { 0x02, 0x55, 0x53, 0x42, 0xEC, 0x01 };

static int ecm_find_bulk_eps(const struct usb_interface_info *intf,
                             uint8_t *in_ep, uint8_t *out_ep,
                             uint16_t *in_mps, uint16_t *out_mps) {
    uint8_t found_in = 0, found_out = 0;
    uint16_t mps_in = 0, mps_out = 0;

    if (!intf) return 0;
    for (int e = 0; e < intf->num_endpoints; ++e) {
        if ((intf->endpoints[e].type & 0x03) != 2) continue;
        if (intf->endpoints[e].address & 0x80) {
            if (!found_in) {
                found_in = intf->endpoints[e].address;
                mps_in = intf->endpoints[e].max_packet_size;
            }
        } else {
            if (!found_out) {
                found_out = intf->endpoints[e].address;
                mps_out = intf->endpoints[e].max_packet_size;
            }
        }
    }
    if (!found_in || !found_out) return 0;
    *in_ep = found_in;
    *out_ep = found_out;
    *in_mps = mps_in ? mps_in : 512;
    *out_mps = mps_out ? mps_out : 512;
    return 1;
}

static void ecm_set_packet_filter(uint8_t dev_addr, uint8_t max_ep0, uint8_t ctrl_intf) {
    usb_setup_packet_t setup;
    setup.bmRequestType = 0x21; /* Host-to-device, Class, Interface */
    setup.bRequest = USB_ECM_SET_PACKET_FILTER;
    setup.wValue = 0x000F; /* Directed | All Multicast | Broadcast | Promiscuous */
    setup.wIndex = ctrl_intf;
    setup.wLength = 0;
    (void)usb_control_transfer(dev_addr, 0, max_ep0, &setup, NULL, 0);
}

void usb_cdc_ecm_init(void) {
    if (cdc_ecm_active || ecm_bound) return;

    const struct usb_device_info *candidate = NULL;
    const struct usb_interface_info *data_intf = NULL;
    uint8_t ctrl_num = 0;
    uint8_t in_ep = 0, out_ep = 0;
    uint16_t in_mps = 0, out_mps = 0;

    int n = usb_get_device_count();
    for (int i = 0; i < n; ++i) {
        const struct usb_device_info *dev = usb_get_device(i);
        if (!dev || !dev->active) continue;

        int has_ecm_ctrl = 0;
        for (int j = 0; j < dev->num_interfaces; ++j) {
            const struct usb_interface_info *intf = &dev->interfaces[j];
            if (intf->intf_class == USB_CLASS_COMM && intf->intf_subclass == USB_SUBCLASS_ECM) {
                has_ecm_ctrl = 1;
                ctrl_num = intf->intf_number;
                break;
            }
        }
        if (!has_ecm_ctrl) continue;

        /* Prefer alt_setting == 1 for CDC-ECM data interface, then fallback to alt_setting == 0 */
        for (int pass = 0; pass < 2 && !data_intf; ++pass) {
            for (int j = 0; j < dev->num_interfaces; ++j) {
                const struct usb_interface_info *intf = &dev->interfaces[j];
                if (intf->intf_class != 0x0A) continue;
                if (pass == 0 && intf->alt_setting == 0) continue;
                if (ecm_find_bulk_eps(intf, &in_ep, &out_ep, &in_mps, &out_mps)) {
                    candidate = dev;
                    data_intf = intf;
                    break;
                }
            }
        }
        if (candidate && data_intf) break;
    }

    if (!candidate || !data_intf) return;

    ecm_dev = candidate;
    ecm_data_intf = data_intf;
    ecm_ctrl_intf_num = ctrl_num;
    ecm_bulk_in = in_ep;
    ecm_bulk_out = out_ep;
    ecm_bulk_in_mps = in_mps;
    ecm_bulk_out_mps = out_mps;
    ecm_bound = 1;

    printf("cdc_ecm: candidate addr=%d ctrl=%u data=%u alt=%u IN=0x%02x OUT=0x%02x\n",
           ecm_dev->addr, ecm_ctrl_intf_num, ecm_data_intf->intf_number,
           ecm_data_intf->alt_setting, ecm_bulk_in, ecm_bulk_out);

    if (ecm_data_intf->alt_setting != 0) {
        if (!usb_set_interface(ecm_dev->addr, ecm_data_intf->intf_number,
                               ecm_data_intf->alt_setting, ecm_dev->max_packet_size0)) {
            printf("cdc_ecm: failed to select data interface %u alt %u\n",
                   ecm_data_intf->intf_number, ecm_data_intf->alt_setting);
            ecm_bound = 0;
            ecm_dev = NULL;
            ecm_data_intf = NULL;
            return;
        }
    }

    if (!usb_configure_interface(ecm_dev->addr, ecm_data_intf)) {
        printf("cdc_ecm: failed to configure bulk endpoints\n");
        ecm_bound = 0;
        ecm_dev = NULL;
        ecm_data_intf = NULL;
        return;
    }

    ecm_set_packet_filter(ecm_dev->addr, ecm_dev->max_packet_size0, ecm_ctrl_intf_num);

    ecm_mac[0] = 0x02;
    ecm_mac[1] = 0x55;
    ecm_mac[2] = 0x53;
    ecm_mac[3] = (uint8_t)(ecm_dev->vid & 0xFF);
    ecm_mac[4] = (uint8_t)(ecm_dev->pid & 0xFF);
    ecm_mac[5] = (uint8_t)(ecm_dev->addr & 0xFF);

    int netif = netif_register(ecm_mac, usb_cdc_ecm_send, usb_cdc_ecm_poll);
    if (netif < 0) {
        printf("cdc_ecm: netif registration failed\n");
        ecm_bound = 0;
        return;
    }
    netif_activate(netif);
    cdc_ecm_active = 1;

    printf("cdc_ecm: attached MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
           ecm_mac[0], ecm_mac[1], ecm_mac[2],
           ecm_mac[3], ecm_mac[4], ecm_mac[5]);
    dhcp_start();
}

int usb_cdc_ecm_send(const void *data, int len) {
    if (!cdc_ecm_active || !ecm_dev || !data || len <= 0) return -1;
    return usb_bulk_transfer(ecm_dev->addr, ecm_bulk_out, ecm_bulk_out_mps,
                             0, (void *)data, len) ? len : -1;
}

void usb_cdc_ecm_poll(void) {
    if (!cdc_ecm_active || !ecm_dev) return;

    uint8_t buf[ECM_RX_BUF_SIZE];
    int actual = 0;
    if (!usb_bulk_transfer_actual(ecm_dev->addr, ecm_bulk_in, ecm_bulk_in_mps,
                                  1, buf, sizeof(buf), &actual)) {
        return;
    }
    if (actual >= 14) {
        net_rx(buf, actual);
    }
}

void usb_cdc_ecm_handle_disconnect(int dev_addr) {
    if (!ecm_dev || ecm_dev->addr != dev_addr) return;

    printf("cdc_ecm: device %d disconnected, dropping link\n", dev_addr);
    cdc_ecm_active = 0;
    ecm_bound = 0;
    ecm_dev = NULL;
    ecm_data_intf = NULL;
    ecm_bulk_in = 0;
    ecm_bulk_out = 0;
    ecm_bulk_in_mps = 0;
    ecm_bulk_out_mps = 0;
    netif_activate_other(usb_cdc_ecm_send);
}
