#pragma once

#include <stdint.h>
#include "drivers/usb/ehci.h"

struct usb_interface_info;

#define XHCI_PORT_CONNECT    (1u << 0)
#define XHCI_PORT_ENABLED    (1u << 1)
#define XHCI_PORT_RESET      (1u << 4)
#define XHCI_PORT_POWER      (1u << 9)
#define XHCI_PORT_CSC        (1u << 17)
#define XHCI_PORT_PEC        (1u << 18)
#define XHCI_PORT_WRC        (1u << 19)
#define XHCI_PORT_OCC        (1u << 20)
#define XHCI_PORT_RC         (1u << 21)
#define XHCI_PORT_PLC        (1u << 22)
#define XHCI_PORT_CEC        (1u << 23)
#define XHCI_PORT_CHANGE_MASK \
    (XHCI_PORT_CSC | XHCI_PORT_PEC | XHCI_PORT_WRC | XHCI_PORT_OCC | \
     XHCI_PORT_RC | XHCI_PORT_PLC | XHCI_PORT_CEC)
#define XHCI_PORT_SPEED_MASK (0xFu << 10)

int xhci_init(uint8_t bus, uint8_t slot, uint8_t func, uint32_t bar);
void xhci_poll(void);
int xhci_is_active(void);
int xhci_get_port_count(void);
uint32_t xhci_get_portsc(int port);
int xhci_get_connected_port_count(void);
uint32_t xhci_get_first_connected_portsc(void);
void xhci_ack_port_changes(int port);
int xhci_port_speed(int port);
int xhci_reset_port(int port);
int xhci_address_device(int port, uint8_t max_packet_size0, int *slot_id_out);
int xhci_finish_address_device(int slot_id, uint16_t max_packet_size0);
int xhci_update_ep0(int slot_id, uint16_t max_packet_size);
int xhci_control_transfer(int slot_id, usb_setup_packet_t *setup, void *data, int len);
int xhci_bulk_transfer(int slot_id, uint8_t endpoint, int direction, void *data, int len);
int xhci_bulk_transfer_actual(int slot_id, uint8_t endpoint, int direction, void *data, int len, int *actual_len);
int xhci_configure_interface(int slot_id, const struct usb_interface_info *intf);
void xhci_handle_disconnect(int slot_id);
