#pragma once

#include <stdint.h>

#define USB_CLASS_COMM          0x02
#define USB_SUBCLASS_ECM        0x06
#define USB_ECM_SET_PACKET_FILTER 0x43

void usb_cdc_ecm_init(void);
int  usb_cdc_ecm_send(const void *data, int len);
void usb_cdc_ecm_poll(void);
void usb_cdc_ecm_handle_disconnect(int dev_addr);

extern int cdc_ecm_active;
