#pragma once

#include <stdint.h>

#define USB_VENDOR_APPLE          0x05AC
#define USB_CLASS_VENDOR_SPEC     0xFF
#define IPHETH_USBINTF_SUBCLASS   0xFD
#define IPHETH_USBINTF_PROTOCOL   0x01

void usb_ipheth_init(void);
int usb_ipheth_send(const void *data, int len);
void usb_ipheth_poll(void);
void usb_ipheth_handle_disconnect(int dev_addr);
int usb_ipheth_is_candidate_class(uint8_t intf_class, uint8_t intf_subclass, uint8_t intf_protocol);
int usb_ipheth_is_apple_candidate_interface(uint8_t intf_number,
                                            uint8_t intf_class,
                                            uint8_t intf_subclass,
                                            uint8_t intf_protocol);

extern int ipheth_active;
