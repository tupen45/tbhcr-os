// usb_host.c
// USB host stack for newos with EHCI and experimental xHCI support

#include "usb_host.h"
#include "drivers/usb/ehci.h"
#include "drivers/usb/usb_cdc_ecm.h"
#include "drivers/usb/usb_ipheth.h"
#include "drivers/usb/usb_rndis.h"
#include "drivers/usb/xhci.h"
#include "pci.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io.h"
#include "kmalloc.h"

enum usb_host_mode {
    USB_HOST_NONE = 0,
    USB_HOST_EHCI,
    USB_HOST_XHCI,
};

struct usb_pci_controller {
    int found;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint32_t bar;
};

/* --- USB Host Controller and Device Table --- */
static struct usb_device_info devices[8];
static int device_count = 0;
static int next_usb_address = 1;
static enum usb_host_mode active_host = USB_HOST_NONE;
static const char *last_usb_error = "none";

/* EHCI controller capability and operational registers */
static volatile uint8_t *ehci_cap_regs = NULL;
static volatile uint32_t *ehci_op_regs = NULL;
static uint32_t ehci_num_ports = 0;
static int ehci_detected = 0;
static int xhci_detected = 0;

/* Periodic frame list (4K aligned) */
static uint32_t *periodic_frame_list = NULL;
/* Asynchronous list head (32-byte aligned) */
static ehci_qh_t *async_list_head = NULL;

static volatile void *map_mmio(uint32_t phys_addr) {
    return (volatile void *)(uintptr_t)phys_addr;
}

static uintptr_t virt_to_phys(void *vptr) {
    return (uintptr_t)vptr;
}

static void usb_pci_enable_controller(const struct usb_pci_controller *ctrl) {
    if (!ctrl || !ctrl->found) return;

    uint32_t cmd = pci_config_read32(ctrl->bus, ctrl->slot, ctrl->func, 0x04);
    cmd |= (1u << 1); /* Memory space enable */
    cmd |= (1u << 2); /* Bus master enable for DMA rings */
    pci_config_write32(ctrl->bus, ctrl->slot, ctrl->func, 0x04, cmd);
}

static uint32_t ehci_qh_ep_char(int dev_addr, int endpoint, uint16_t max_packet_size) {
    const uint32_t eps_high_speed = 2u << 12;
    return ((uint32_t)dev_addr << 0) |
           ((uint32_t)endpoint << 8) |
           eps_high_speed |
           ((uint32_t)max_packet_size << 16) |
           (1u << 15);
}

static void ehci_qtd_set_buffer_pages(ehci_qtd_t *qtd, void *data, int len) {
    if (!qtd) return;

    for (int i = 0; i < 5; ++i) {
        qtd->buffer[i] = 0;
        qtd->extended_buffer[i] = 0;
    }
    if (!data || len <= 0) return;

    uintptr_t phys = virt_to_phys(data);
    uintptr_t page = phys & ~0xFFFu;
    uintptr_t offset = phys & 0xFFFu;

    qtd->buffer[0] = (uint32_t)phys;
    for (int i = 1; i < 5; ++i) {
        if (offset + (uintptr_t)len <= (uintptr_t)i * 4096u) break;
        page += 4096u;
        qtd->buffer[i] = (uint32_t)page;
    }
}

static void pci_scan_usb(struct usb_pci_controller *ehci, struct usb_pci_controller *xhci) {
    printf("usb_host: starting PCI scan for USB controllers\n");

    memset(ehci, 0, sizeof(*ehci));
    memset(xhci, 0, sizeof(*xhci));

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t vendor = pci_config_read16(bus, slot, func, 0x00);
                if (vendor == 0xFFFF) {
                    if (func == 0) break;
                    continue;
                }

                uint32_t devreg0 = pci_config_read32(bus, slot, func, 0x00);
                uint16_t devid = (uint16_t)((devreg0 >> 16) & 0xFFFFu);
                uint32_t class_reg = pci_config_read32(bus, slot, func, 0x08);
                uint8_t baseclass = (class_reg >> 24) & 0xFF;
                uint8_t subclass = (class_reg >> 16) & 0xFF;
                uint8_t prog_if = (class_reg >> 8) & 0xFF;

                printf("usb_host: PCI device found: bus=%u slot=%u func=%u vendor=0x%04x device=0x%04x class=0x%x sub=0x%x prog_if=0x%x\n",
                       (unsigned)bus, (unsigned)slot, (unsigned)func,
                       vendor, devid, baseclass, subclass, prog_if);

                if (baseclass == 0x0C && subclass == 0x03 && prog_if == 0x20 && !ehci->found) {
                    uint32_t bar0 = pci_config_read32(bus, slot, func, 0x10);
                    if (!(bar0 & 0x1)) {
                        ehci->found = 1;
                        ehci->bus = (uint8_t)bus;
                        ehci->slot = slot;
                        ehci->func = func;
                        ehci->bar = bar0 & ~0xFu;
                        ehci_detected = 1;
                        printf("usb_host: candidate EHCI BAR0 raw=0x%08x\n", bar0);
                    } else {
                        printf("usb_host: EHCI BAR is I/O (BAR=0x%08x), not supported\n", bar0);
                    }
                }

                if (baseclass == 0x0C && subclass == 0x03 && prog_if == 0x30 && !xhci->found) {
                    uint32_t bar0 = pci_config_read32(bus, slot, func, 0x10);
                    if (!(bar0 & 0x1)) {
                        uint32_t bar_type = (bar0 >> 1) & 0x3u;
                        uint32_t bar_hi = (bar_type == 0x2u) ? pci_config_read32(bus, slot, func, 0x14) : 0;
                        if (bar_hi != 0) {
                            printf("usb_host: xHCI 64-bit BAR above 4G unsupported (BAR0=0x%08x BAR1=0x%08x)\n",
                                   bar0, bar_hi);
                            continue;
                        }
                        xhci->found = 1;
                        xhci->bus = (uint8_t)bus;
                        xhci->slot = slot;
                        xhci->func = func;
                        xhci->bar = bar0 & ~0xFu;
                        xhci_detected = 1;
                        printf("usb_host: candidate xHCI BAR0 raw=0x%08x\n", bar0);
                    }
                }

                if (func == 0 && !((pci_config_read32(bus, slot, func, 0x0C) >> 16) & 0x80)) {
                    break;
                }
            }
        }
    }
}

static void delay_ms(int ms) {
    for (int i = 0; i < ms * 100000; ++i) {
        __asm__ volatile("nop");
    }
}

static int wait_until_clear(volatile uint32_t *reg, uint32_t mask, int max_ms) {
    for (int i = 0; i < max_ms; ++i) {
        if (((*reg) & mask) == 0) return 1;
        delay_ms(1);
    }
    return 0;
}

static int usb_find_active_device_on_port(int port) {
    for (int i = 0; i < device_count; ++i) {
        if (devices[i].active && devices[i].port == port) return i;
    }
    return -1;
}

static int usb_active_device_count(void) {
    int count = 0;
    for (int i = 0; i < device_count; ++i) {
        if (devices[i].active) {
            count++;
        }
    }
    return count;
}

static struct usb_device_info *usb_alloc_device_slot(void) {
    for (int i = 0; i < device_count; ++i) {
        if (!devices[i].active) {
            memset(&devices[i], 0, sizeof(devices[i]));
            return &devices[i];
        }
    }
    if (device_count >= (int)(sizeof(devices) / sizeof(devices[0]))) {
        return NULL;
    }
    memset(&devices[device_count], 0, sizeof(devices[device_count]));
    return &devices[device_count++];
}

static struct usb_device_info *usb_find_device_by_addr_mut(int addr) {
    for (int i = 0; i < device_count; ++i) {
        if (devices[i].active && devices[i].addr == addr) return &devices[i];
    }
    return NULL;
}

static void usb_disconnect_port(int port) {
    printf("usb_host: port %d device disconnected.\n", port);
    for (int i = 0; i < device_count; ++i) {
        if (!devices[i].active || devices[i].port != port) continue;
        usb_rndis_handle_disconnect(devices[i].addr);
        usb_ipheth_handle_disconnect(devices[i].addr);
        usb_cdc_ecm_handle_disconnect(devices[i].addr);
        if (active_host == USB_HOST_XHCI && devices[i].slot_id > 0) {
            xhci_handle_disconnect(devices[i].slot_id);
        }
        devices[i].active = 0;
    }
}

/* --- EHCI Controller Initialization --- */
static void ehci_reset_and_init(void) {
    if (!ehci_cap_regs) return;

    uint8_t caplength = ehci_cap_regs[EHCI_CAP_CAPLENGTH];
    ehci_op_regs = (volatile uint32_t *)(ehci_cap_regs + caplength);

    uint32_t hcsparams = *(volatile uint32_t *)(ehci_cap_regs + EHCI_CAP_HCSPARAMS);
    ehci_num_ports = hcsparams & 0xF;

    ehci_op_regs[EHCI_OP_USBCMD / 4] &= ~CMD_RUN_STOP;
    int wait = 0;
    while (!(ehci_op_regs[EHCI_OP_USBSTS / 4] & STS_HCHALTED) && wait++ < 1000) {
        delay_ms(1);
    }

    ehci_op_regs[EHCI_OP_USBCMD / 4] |= CMD_HCRESET;
    wait = 0;
    while ((ehci_op_regs[EHCI_OP_USBCMD / 4] & CMD_HCRESET) && wait++ < 1000) {
        delay_ms(1);
    }

    periodic_frame_list = kmalloc_aligned(4096, 4096);
    if (!periodic_frame_list) {
        printf("usb_host: periodic frame list allocation failed\n");
        return;
    }
    for (int i = 0; i < 1024; ++i) periodic_frame_list[i] = 1;
    ehci_op_regs[EHCI_OP_PERIODICLISTBASE / 4] = (uint32_t)(uintptr_t)periodic_frame_list;

    async_list_head = kmalloc_aligned(sizeof(ehci_qh_t), 32);
    if (!async_list_head) {
        printf("usb_host: async qh allocation failed\n");
        return;
    }
    memset((void *)async_list_head, 0, sizeof(ehci_qh_t));
    async_list_head->qh_link = ((uint32_t)(uintptr_t)async_list_head) | (1 << 1);
    async_list_head->ep_char = (1 << 15);
    ehci_op_regs[EHCI_OP_ASYNCLISTADDR / 4] = (uint32_t)(uintptr_t)async_list_head;

    uint32_t cmd = ehci_op_regs[EHCI_OP_USBCMD / 4];
    cmd |= CMD_ASYNC_ENABLE | CMD_PERIODIC_ENABLE;
    ehci_op_regs[EHCI_OP_USBCMD / 4] = cmd;

    ehci_op_regs[EHCI_OP_CONFIGFLAG / 4] = 1;
    delay_ms(20);
    ehci_op_regs[EHCI_OP_USBCMD / 4] |= CMD_RUN_STOP;

    for (unsigned int port = 0; port < ehci_num_ports; ++port) {
        volatile uint32_t *portsc = &ehci_op_regs[(EHCI_OP_PORTSC_BASE / 4) + port];
        *portsc |= PORTSC_PORT_POWER;
    }
    delay_ms(20);
}

/* --- EHCI Transfer Primitives --- */
int ehci_control_transfer(int dev_addr, int endpoint, uint8_t max_packet_size,
                          usb_setup_packet_t *setup, void *data, int len) {
    if (!async_list_head || !setup) return 0;

    ehci_qh_t *qh = kmalloc_aligned(sizeof(ehci_qh_t), 32);
    ehci_qtd_t *qtd_setup = kmalloc_aligned(sizeof(ehci_qtd_t), 32);
    ehci_qtd_t *qtd_data = NULL;
    ehci_qtd_t *qtd_status = kmalloc_aligned(sizeof(ehci_qtd_t), 32);
    if (!qh || !qtd_setup || !qtd_status || (len > 0 && !data)) {
        if (qh) kfree_aligned(qh);
        if (qtd_setup) kfree_aligned(qtd_setup);
        if (qtd_status) kfree_aligned(qtd_status);
        return 0;
    }

    memset(qh, 0, sizeof(*qh));
    memset(qtd_setup, 0, sizeof(*qtd_setup));
    memset(qtd_status, 0, sizeof(*qtd_status));

    qh->ep_char = ehci_qh_ep_char(dev_addr, endpoint, max_packet_size);
    qh->ep_caps = (1 << 30);
    qh->next_qtd_overlay = (uint32_t)(uintptr_t)qtd_setup;

    qtd_setup->next_qtd = (len > 0) ? 0 : (uint32_t)(uintptr_t)qtd_status;
    qtd_setup->alt_next_qtd = 1;
    qtd_setup->token = (sizeof(usb_setup_packet_t) << QTD_TOTAL_BYTES_SHIFT) |
                       QTD_PID_SETUP | QTD_STATUS_ACTIVE;
    ehci_qtd_set_buffer_pages(qtd_setup, setup, sizeof(usb_setup_packet_t));

    ehci_qtd_t *last_qtd = qtd_setup;
    if (len > 0) {
        qtd_data = kmalloc_aligned(sizeof(ehci_qtd_t), 32);
        if (!qtd_data) {
            kfree_aligned(qh);
            kfree_aligned(qtd_setup);
            kfree_aligned(qtd_status);
            return 0;
        }
        memset(qtd_data, 0, sizeof(*qtd_data));
        last_qtd->next_qtd = (uint32_t)(uintptr_t)qtd_data;
        last_qtd = qtd_data;

        uint32_t pid = (setup->bmRequestType & 0x80) ? QTD_PID_IN : QTD_PID_OUT;
        qtd_data->token = ((uint32_t)len << QTD_TOTAL_BYTES_SHIFT) | pid | QTD_STATUS_ACTIVE;
        qtd_data->next_qtd = (uint32_t)(uintptr_t)qtd_status;
        qtd_data->alt_next_qtd = 1;
        ehci_qtd_set_buffer_pages(qtd_data, data, len);
    }

    uint32_t status_pid = (setup->bmRequestType & 0x80) ? QTD_PID_OUT : QTD_PID_IN;
    qtd_status->token = status_pid | QTD_STATUS_ACTIVE | (1 << 15);
    qtd_status->next_qtd = 1;
    qtd_status->alt_next_qtd = 1;

    qh->qh_link = async_list_head->qh_link;
    async_list_head->qh_link = ((uint32_t)(uintptr_t)qh) | (1 << 1);

    int done = 0;
    for (int i = 0; i < 2000000; ++i) {
        if ((qtd_status->token & QTD_STATUS_ACTIVE) == 0) {
            done = 1;
            break;
        }
    }
    if (!done) {
        printf("usb_host: control transfer timeout\n");
        async_list_head->qh_link = qh->qh_link;
        kfree_aligned(qh);
        kfree_aligned(qtd_setup);
        if (qtd_data) kfree_aligned(qtd_data);
        kfree_aligned(qtd_status);
        return 0;
    }

    int success = !(qtd_status->token & QTD_STATUS_HALTED);
    if (!success) {
        printf("usb_host: control transfer failed. Status: 0x%x\n", qtd_status->token);
    }

    async_list_head->qh_link = qh->qh_link;
    kfree_aligned(qh);
    kfree_aligned(qtd_setup);
    if (qtd_data) kfree_aligned(qtd_data);
    kfree_aligned(qtd_status);
    return success;
}

int ehci_bulk_transfer(int dev_addr, int endpoint, uint16_t max_packet_size,
                       int direction, void *data, int len) {
    if (!async_list_head || !data || len <= 0) return 0;

    ehci_qh_t *qh = kmalloc_aligned(sizeof(ehci_qh_t), 32);
    ehci_qtd_t *qtd = kmalloc_aligned(sizeof(ehci_qtd_t), 32);
    if (!qh || !qtd) {
        if (qh) kfree_aligned(qh);
        if (qtd) kfree_aligned(qtd);
        return 0;
    }

    memset(qh, 0, sizeof(*qh));
    memset(qtd, 0, sizeof(*qtd));

    qh->ep_char = ehci_qh_ep_char(dev_addr, endpoint & 0x7F, max_packet_size);
    qh->ep_caps = (1 << 30);
    qh->next_qtd_overlay = (uint32_t)(uintptr_t)qtd;

    uint32_t pid = direction ? QTD_PID_IN : QTD_PID_OUT;
    qtd->token = ((uint32_t)len << QTD_TOTAL_BYTES_SHIFT) | pid | QTD_STATUS_ACTIVE | (1 << 15);
    qtd->next_qtd = 1;
    qtd->alt_next_qtd = 1;
    ehci_qtd_set_buffer_pages(qtd, data, len);

    qh->qh_link = async_list_head->qh_link;
    async_list_head->qh_link = ((uint32_t)(uintptr_t)qh) | (1 << 1);

    int done = 0;
    for (int i = 0; i < 2000000; ++i) {
        if ((qtd->token & QTD_STATUS_ACTIVE) == 0) {
            done = 1;
            break;
        }
    }
    if (!done) {
        async_list_head->qh_link = qh->qh_link;
        kfree_aligned(qh);
        kfree_aligned(qtd);
        return 0;
    }

    int success = !(qtd->token & QTD_STATUS_HALTED);
    if (!success) {
        printf("usb_host: bulk transfer failed. Status: 0x%x\n", qtd->token);
    }

    async_list_head->qh_link = qh->qh_link;
    kfree_aligned(qh);
    kfree_aligned(qtd);
    return success;
}

/* --- Generic Transfer Wrappers --- */
int usb_control_transfer(int dev_addr, int endpoint, uint8_t max_packet_size,
                         usb_setup_packet_t *setup, void *data, int len) {
    if (active_host == USB_HOST_XHCI) {
        struct usb_device_info *dev = usb_find_device_by_addr_mut(dev_addr);
        if (!dev) return 0;
        return xhci_control_transfer(dev->slot_id, setup, data, len);
    }
    return ehci_control_transfer(dev_addr, endpoint, max_packet_size, setup, data, len);
}

int usb_bulk_transfer(int dev_addr, int endpoint, uint16_t max_packet_size,
                      int direction, void *data, int len) {
    return usb_bulk_transfer_actual(dev_addr, endpoint, max_packet_size, direction, data, len, NULL);
}

int usb_bulk_transfer_actual(int dev_addr, int endpoint, uint16_t max_packet_size,
                             int direction, void *data, int len, int *actual_len) {
    if (actual_len) *actual_len = 0;
    if (active_host == USB_HOST_XHCI) {
        struct usb_device_info *dev = usb_find_device_by_addr_mut(dev_addr);
        if (!dev) return 0;
        (void)max_packet_size;
        return xhci_bulk_transfer_actual(dev->slot_id, (uint8_t)endpoint, direction, data, len, actual_len);
    }
    int ok = ehci_bulk_transfer(dev_addr, endpoint, max_packet_size, direction, data, len);
    if (ok && actual_len) *actual_len = len;
    return ok;
}

int usb_set_interface(int dev_addr, uint8_t intf_number, uint8_t alt_setting, uint8_t max_packet_size) {
    usb_setup_packet_t setup;
    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = 0x01;
    setup.bRequest = REQ_SET_INTERFACE;
    setup.wValue = alt_setting;
    setup.wIndex = intf_number;
    setup.wLength = 0;
    return usb_control_transfer(dev_addr, 0, max_packet_size, &setup, NULL, 0);
}

int usb_configure_interface(int dev_addr, const struct usb_interface_info *intf) {
    if (active_host != USB_HOST_XHCI) return 1;
    struct usb_device_info *dev = usb_find_device_by_addr_mut(dev_addr);
    if (!dev) return 0;
    return xhci_configure_interface(dev->slot_id, intf);
}

static void usb_bind_network_device(struct usb_device_info *dev) {
    int have_cdc_ecm = 0;
    int have_rndis = 0;
    int have_ipheth = (dev->vid == USB_VENDOR_APPLE);

    for (int i = 0; i < dev->num_interfaces; ++i) {
        const struct usb_interface_info *intf = &dev->interfaces[i];
        if (intf->intf_class == 0x02 && intf->intf_subclass == 0x06) {
            have_cdc_ecm = 1;
        }
        if (intf->intf_class == USB_CLASS_CDC_DATA ||
            (intf->intf_class == USB_CLASS_WIRELESS &&
             intf->intf_subclass == USB_SUBCLASS_RNDIS &&
             intf->intf_protocol == USB_PROTOCOL_RNDIS)) {
            have_rndis = 1;
        }
        if (dev->vid == USB_VENDOR_APPLE) have_ipheth = 1;
    }

    if (have_cdc_ecm) {
        extern void usb_cdc_ecm_init(void);
        usb_cdc_ecm_init();
    }
    if (have_rndis) {
        extern void usb_rndis_init(void);
        usb_rndis_init();
    }
    if (have_ipheth) {
        usb_ipheth_init();
    }
}

static int usb_interface_has_bulk_pair(const struct usb_interface_info *intf) {
    int have_in = 0;
    int have_out = 0;
    if (!intf) return 0;
    for (int e = 0; e < intf->num_endpoints; ++e) {
        if ((intf->endpoints[e].type & 0x03) != 2) continue;
        if (intf->endpoints[e].address & 0x80) {
            have_in = 1;
        } else {
            have_out = 1;
        }
    }
    return have_in && have_out;
}

static int usb_config_score(const struct usb_device_info *dev) {
    int score = 0;
    if (!dev) return 0;

    if (dev->vid == USB_VENDOR_APPLE) {
        score += 1;
        if (dev->num_interfaces > 1) score += 10;
    }

    for (int i = 0; i < dev->num_interfaces; ++i) {
        const struct usb_interface_info *intf = &dev->interfaces[i];
        if (intf->intf_class == 0x02 && intf->intf_subclass == 0x06) score += 80;
        if (intf->intf_class == USB_CLASS_CDC_DATA) score += 60;
        if (intf->intf_class == USB_CLASS_WIRELESS &&
            intf->intf_subclass == USB_SUBCLASS_RNDIS &&
            intf->intf_protocol == USB_PROTOCOL_RNDIS) score += 80;
        if (dev->vid == USB_VENDOR_APPLE &&
            usb_ipheth_is_apple_candidate_interface(intf->intf_number,
                                                    intf->intf_class,
                                                    intf->intf_subclass,
                                                    intf->intf_protocol)) {
            score += 100;
            if (intf->alt_setting == 1) score += 20;
            if (usb_interface_has_bulk_pair(intf)) score += 40;
        }
    }

    return score;
}

static int usb_parse_configuration(struct usb_device_info *dev, void *full_cfg_buf,
                                   uint16_t total_len, int verbose) {
    if (!dev || !full_cfg_buf || total_len < sizeof(usb_configuration_descriptor_t)) return 0;

    usb_configuration_descriptor_t *cfg_desc = (usb_configuration_descriptor_t *)full_cfg_buf;
    dev->config_value = cfg_desc->bConfigurationValue;
    dev->num_interfaces = 0;

    uint8_t *p = (uint8_t *)full_cfg_buf;
    uint8_t *end = p + total_len;
    struct usb_interface_info *cur_intf = NULL;
    while (p < end) {
        usb_descriptor_header_t *hdr = (usb_descriptor_header_t *)p;
        if (hdr->bLength == 0 || p + hdr->bLength > end) break;

        if (hdr->bDescriptorType == DESC_INTERFACE) {
            usb_interface_descriptor_t *intf = (usb_interface_descriptor_t *)p;
            if (dev->num_interfaces >= MAX_USB_INTERFACES) {
                cur_intf = NULL;
                if (verbose) printf("  Interface descriptor dropped (max interfaces reached)\n");
            } else {
                cur_intf = &dev->interfaces[dev->num_interfaces++];
                memset(cur_intf, 0, sizeof(*cur_intf));
                cur_intf->intf_number = intf->bInterfaceNumber;
                cur_intf->alt_setting = intf->bAlternateSetting;
                cur_intf->intf_class = intf->bInterfaceClass;
                cur_intf->intf_subclass = intf->bInterfaceSubClass;
                cur_intf->intf_protocol = intf->bInterfaceProtocol;
                if (verbose) {
                    printf("  Interface %u alt %u: Class=0x%x, SubClass=0x%x, Proto=0x%x\n",
                           intf->bInterfaceNumber, intf->bAlternateSetting,
                           intf->bInterfaceClass, intf->bInterfaceSubClass, intf->bInterfaceProtocol);
                }
            }
        } else if (hdr->bDescriptorType == DESC_ENDPOINT) {
            usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t *)p;
            if (cur_intf && cur_intf->num_endpoints < MAX_ENDPOINTS) {
                int idx = cur_intf->num_endpoints++;
                cur_intf->endpoints[idx].address = ep->bEndpointAddress;
                cur_intf->endpoints[idx].type = ep->bmAttributes & 0x03;
                cur_intf->endpoints[idx].max_packet_size = ep->wMaxPacketSize;
                if (verbose) {
                    printf("    Endpoint: Addr=0x%02x, Attrs=0x%x, MaxPacket=%d\n",
                           ep->bEndpointAddress, ep->bmAttributes, ep->wMaxPacketSize);
                }
            }
        } else if (hdr->bDescriptorType == 0x24) {
            uint8_t *cs = p;
            if (cur_intf && hdr->bLength >= 5 && cs[2] == 0x0F) {
                cur_intf->mac_string_index = cs[4];
                if (verbose) {
                    printf("    CDC-ECM Functional Descriptor found, iMAC string index=%u\n",
                           cur_intf->mac_string_index);
                }
            }
        }
        p += hdr->bLength;
    }

    return 1;
}

static int usb_fetch_descriptors_and_configure(struct usb_device_info *dev) {
    usb_setup_packet_t setup;
    uint8_t buffer[256];

    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = 0x80;
    setup.bRequest = REQ_GET_DESCRIPTOR;
    setup.wValue = (DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = sizeof(usb_device_descriptor_t);
    if (!usb_control_transfer(dev->addr, 0, dev->max_packet_size0, &setup,
                              buffer, sizeof(usb_device_descriptor_t))) {
        return 0;
    }

    usb_device_descriptor_t *dev_desc = (usb_device_descriptor_t *)buffer;
    dev->vid = dev_desc->idVendor;
    dev->pid = dev_desc->idProduct;
    dev->dev_class = dev_desc->bDeviceClass;
    dev->dev_subclass = dev_desc->bDeviceSubClass;
    dev->dev_protocol = dev_desc->bDeviceProtocol;
    printf("usb_host: device enumerated: addr=%d, VID=0x%04x, PID=0x%04x\n",
           dev->addr, dev->vid, dev->pid);

    int best_cfg_index = 0;
    int best_score = -1;
    int cfg_count = dev_desc->bNumConfigurations;
    if (cfg_count <= 0) cfg_count = 1;

    for (int cfg_index = 0; cfg_index < cfg_count; ++cfg_index) {
        memset(&setup, 0, sizeof(setup));
        setup.bmRequestType = 0x80;
        setup.bRequest = REQ_GET_DESCRIPTOR;
        setup.wValue = (DESC_CONFIGURATION << 8) | (uint16_t)cfg_index;
        setup.wLength = sizeof(usb_configuration_descriptor_t);
        if (!usb_control_transfer(dev->addr, 0, dev->max_packet_size0, &setup,
                                  buffer, sizeof(usb_configuration_descriptor_t))) {
            continue;
        }

        usb_configuration_descriptor_t *cfg_desc = (usb_configuration_descriptor_t *)buffer;
        uint16_t total_len = cfg_desc->wTotalLength;
        if (total_len < sizeof(usb_configuration_descriptor_t) || total_len > 4096) {
            continue;
        }

        void *full_cfg_buf = kmalloc(total_len);
        if (!full_cfg_buf) return 0;
        setup.wLength = total_len;
        if (!usb_control_transfer(dev->addr, 0, dev->max_packet_size0, &setup,
                                  full_cfg_buf, total_len)) {
            kfree(full_cfg_buf);
            continue;
        }

        if (usb_parse_configuration(dev, full_cfg_buf, total_len, 0)) {
            int score = usb_config_score(dev);
            printf("usb_host: config index=%d value=%u interfaces=%d score=%d\n",
                   cfg_index, dev->config_value, dev->num_interfaces, score);
            if (score > best_score) {
                best_score = score;
                best_cfg_index = cfg_index;
            }
        }
        kfree(full_cfg_buf);
    }

    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = 0x80;
    setup.bRequest = REQ_GET_DESCRIPTOR;
    setup.wValue = (DESC_CONFIGURATION << 8) | (uint16_t)best_cfg_index;
    setup.wLength = sizeof(usb_configuration_descriptor_t);
    if (!usb_control_transfer(dev->addr, 0, dev->max_packet_size0, &setup,
                              buffer, sizeof(usb_configuration_descriptor_t))) {
        return 0;
    }

    usb_configuration_descriptor_t *cfg_desc = (usb_configuration_descriptor_t *)buffer;
    uint16_t total_len = cfg_desc->wTotalLength;
    void *full_cfg_buf = kmalloc(total_len);
    if (!full_cfg_buf) return 0;

    setup.wLength = total_len;
    if (!usb_control_transfer(dev->addr, 0, dev->max_packet_size0, &setup,
                              full_cfg_buf, total_len)) {
        kfree(full_cfg_buf);
        return 0;
    }

    if (!usb_parse_configuration(dev, full_cfg_buf, total_len, 1)) {
        kfree(full_cfg_buf);
        return 0;
    }
    printf("usb_host: selected config index=%d value=%u score=%d\n",
           best_cfg_index, dev->config_value, best_score);
    kfree(full_cfg_buf);

    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = 0x00;
    setup.bRequest = REQ_SET_CONFIGURATION;
    setup.wValue = dev->config_value;
    setup.wIndex = 0;
    setup.wLength = 0;
    if (!usb_control_transfer(dev->addr, 0, dev->max_packet_size0, &setup, NULL, 0)) {
        return 0;
    }
    printf("usb_host: device configured.\n");

    usb_bind_network_device(dev);
    return 1;
}

static int usb_enumerate_ehci_device(int port) {
    struct usb_device_info *dev = usb_alloc_device_slot();
    if (!dev) return 0;

    usb_setup_packet_t setup;
    uint8_t buffer[256];
    memset(&setup, 0, sizeof(setup));

    setup.bmRequestType = 0x80;
    setup.bRequest = REQ_GET_DESCRIPTOR;
    setup.wValue = (DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 8;
    if (!ehci_control_transfer(0, 0, 8, &setup, buffer, 8)) return 0;

    uint8_t max_packet_size = buffer[7];
    int new_address = next_usb_address++;

    setup.bmRequestType = 0x00;
    setup.bRequest = REQ_SET_ADDRESS;
    setup.wValue = (uint16_t)new_address;
    setup.wIndex = 0;
    setup.wLength = 0;
    if (!ehci_control_transfer(0, 0, max_packet_size, &setup, NULL, 0)) return 0;
    delay_ms(10);

    dev->active = 1;
    dev->addr = new_address;
    dev->slot_id = 0;
    dev->port = port;
    dev->max_packet_size0 = max_packet_size;
    if (!usb_fetch_descriptors_and_configure(dev)) {
        dev->active = 0;
        return 0;
    }
    return 1;
}

static int usb_enumerate_xhci_device(int port) {
    struct usb_device_info *dev = usb_alloc_device_slot();
    if (!dev) {
        last_usb_error = "no device slots";
        return 0;
    }

    int slot_id = 0;
    last_usb_error = "xHCI address";
    if (!xhci_address_device(port, 64, &slot_id)) return 0;

    dev->active = 1;
    dev->addr = slot_id;
    dev->slot_id = slot_id;
    dev->port = port;
    dev->max_packet_size0 = 64;

    usb_setup_packet_t setup;
    uint8_t buffer[8];
    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = 0x80;
    setup.bRequest = REQ_GET_DESCRIPTOR;
    setup.wValue = (DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 8;
    last_usb_error = "xHCI get desc8";
    if (!usb_control_transfer(dev->addr, 0, dev->max_packet_size0, &setup, buffer, 8)) {
        xhci_handle_disconnect(slot_id);
        dev->active = 0;
        return 0;
    }

    if (buffer[7] != 0 && buffer[7] != dev->max_packet_size0) {
        dev->max_packet_size0 = buffer[7];
        xhci_update_ep0(slot_id, buffer[7]);
    }

    last_usb_error = "xHCI finish address";
    if (!xhci_finish_address_device(slot_id, dev->max_packet_size0)) {
        xhci_handle_disconnect(slot_id);
        dev->active = 0;
        return 0;
    }

    last_usb_error = "xHCI fetch cfg";
    if (!usb_fetch_descriptors_and_configure(dev)) {
        xhci_handle_disconnect(slot_id);
        dev->active = 0;
        return 0;
    }
    last_usb_error = "none";
    return 1;
}

static void ehci_poll_ports(void) {
    if (!ehci_op_regs) return;

    for (unsigned int port = 0; port < ehci_num_ports; ++port) {
        volatile uint32_t *portsc_ptr = &ehci_op_regs[(EHCI_OP_PORTSC_BASE / 4) + port];
        uint32_t portsc = *portsc_ptr;
        int found = usb_find_active_device_on_port((int)port) >= 0;

        if (portsc & PORTSC_CONNECT_CHANGE) {
            *portsc_ptr |= PORTSC_CONNECT_CHANGE;
            if (portsc & PORTSC_CONNECT_STATUS) {
                if (found) continue;
                printf("usb_host: port %u device connected.\n", port);
                delay_ms(100);
                *portsc_ptr |= PORTSC_PORT_RESET;
                if (!wait_until_clear(portsc_ptr, PORTSC_PORT_RESET, 250)) {
                    printf("usb_host: port %u reset timeout\n", port);
                    continue;
                }
                delay_ms(10);
                usb_enumerate_ehci_device((int)port);
            } else {
                usb_disconnect_port((int)port);
            }
        } else if ((portsc & PORTSC_CONNECT_STATUS) && !found) {
            printf("usb_host: port %u device present at boot - enumerating.\n", port);
            delay_ms(100);
            *portsc_ptr |= PORTSC_PORT_RESET;
            if (!wait_until_clear(portsc_ptr, PORTSC_PORT_RESET, 250)) {
                printf("usb_host: port %u boot reset timeout\n", port);
                continue;
            }
            delay_ms(10);
            usb_enumerate_ehci_device((int)port);
        }
    }
}

static void xhci_poll_ports(void) {
    xhci_poll();
    for (int port = 0; port < xhci_get_port_count(); ++port) {
        uint32_t portsc = xhci_get_portsc(port);
        int connected = (portsc & XHCI_PORT_CONNECT) != 0;
        int found = usb_find_active_device_on_port(port) >= 0;

        if (portsc & XHCI_PORT_CHANGE_MASK) {
            xhci_ack_port_changes(port);
        }

        if (!connected && found) {
            usb_disconnect_port(port);
            continue;
        }
        if (!connected || found) continue;

        printf("usb_host: xHCI port %d device present - enumerating.\n", port);
        if (!xhci_reset_port(port)) {
            continue;
        }
        usb_enumerate_xhci_device(port);
    }
}

/* --- Public API --- */
void usb_host_init(void) {
    kmalloc_init();

    struct usb_pci_controller ehci;
    struct usb_pci_controller xhci;
    memset(devices, 0, sizeof(devices));
    device_count = 0;
    next_usb_address = 1;
    active_host = USB_HOST_NONE;
    ehci_detected = 0;
    xhci_detected = 0;

    pci_scan_usb(&ehci, &xhci);

    if (xhci.found) {
        usb_pci_enable_controller(&xhci);
        printf("usb_host: xHCI controller BAR=0x%08x at bus=%d slot=%d func=%d\n",
               xhci.bar, xhci.bus, xhci.slot, xhci.func);
        if (xhci_init(xhci.bus, xhci.slot, xhci.func, xhci.bar)) {
            active_host = USB_HOST_XHCI;
            return;
        }
        printf("usb_host: xHCI init failed, falling back if EHCI exists\n");
    }

    if (ehci.found) {
        usb_pci_enable_controller(&ehci);
        printf("usb_host: EHCI controller BAR=0x%08x at bus=%d slot=%d func=%d\n",
               ehci.bar, ehci.bus, ehci.slot, ehci.func);
        ehci_cap_regs = map_mmio(ehci.bar);
        if (!ehci_cap_regs) {
            printf("usb_host: map_mmio failed for BAR=0x%08x\n", ehci.bar);
            return;
        }

        uint8_t caplength = ehci_cap_regs[EHCI_CAP_CAPLENGTH];
        uint32_t hcs = *(volatile uint32_t *)(ehci_cap_regs + EHCI_CAP_HCSPARAMS);
        uint32_t hcc = *(volatile uint32_t *)(ehci_cap_regs + EHCI_CAP_HCCPARAMS);
        printf("usb_host: mapped EHCI MMIO at %p, caplength=%u, HCSP=0x%08x, HCCP=0x%08x\n",
               (void *)ehci_cap_regs, caplength, hcs, hcc);
        ehci_reset_and_init();
        active_host = USB_HOST_EHCI;
        return;
    }

    if (xhci_detected) {
        printf("usb_host: xHCI detected but init failed\n");
    } else {
        printf("usb_host: no supported USB controller found\n");
    }
}

void usb_poll(void) {
    if (active_host == USB_HOST_XHCI) {
        xhci_poll_ports();
    } else if (active_host == USB_HOST_EHCI) {
        ehci_poll_ports();
    }
    usb_cdc_ecm_init();
    usb_rndis_init();
    usb_ipheth_init();
}

int usb_get_device_count(void) {
    return usb_active_device_count();
}

const struct usb_device_info *usb_get_device(int idx) {
    if (idx < 0) return NULL;
    int active_idx = 0;
    for (int i = 0; i < device_count; ++i) {
        if (!devices[i].active) continue;
        if (active_idx == idx) {
            return &devices[i];
        }
        active_idx++;
    }
    return NULL;
}

int usb_host_has_ehci(void) {
    return ehci_detected;
}

int usb_host_has_xhci(void) {
    return xhci_detected;
}

int usb_host_using_ehci(void) {
    return active_host == USB_HOST_EHCI;
}

int usb_host_using_xhci(void) {
    return active_host == USB_HOST_XHCI;
}

int usb_host_has_controller(void) {
    return ehci_detected || xhci_detected;
}

int usb_host_has_unsupported_xhci(void) {
    return xhci_detected && active_host != USB_HOST_XHCI && !ehci_detected;
}

int usb_host_connected_port_count(void) {
    if (active_host == USB_HOST_XHCI) return xhci_get_connected_port_count();
    if (active_host == USB_HOST_EHCI) {
        int count = 0;
        for (unsigned int port = 0; port < ehci_num_ports; ++port) {
            volatile uint32_t *portsc = &ehci_op_regs[(EHCI_OP_PORTSC_BASE / 4) + port];
            if (*portsc & PORTSC_CONNECT_STATUS) count++;
        }
        return count;
    }
    return 0;
}

uint32_t usb_host_first_connected_portsc(void) {
    if (active_host == USB_HOST_XHCI) return xhci_get_first_connected_portsc();
    if (active_host == USB_HOST_EHCI) {
        for (unsigned int port = 0; port < ehci_num_ports; ++port) {
            volatile uint32_t *portsc = &ehci_op_regs[(EHCI_OP_PORTSC_BASE / 4) + port];
            if (*portsc & PORTSC_CONNECT_STATUS) return *portsc;
        }
    }
    return 0;
}

const char *usb_host_last_error(void) {
    return last_usb_error;
}
