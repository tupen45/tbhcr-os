#pragma once
// USB host stack interface

#include <stdint.h>
#include "drivers/usb/ehci.h" // For usb_setup_packet_t

// USB Descriptor Structures
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
} __attribute__((packed)) usb_descriptor_header_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_configuration_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

// Expanded device info
#define MAX_ENDPOINTS 4
#define MAX_USB_INTERFACES 16

struct usb_interface_info {
    uint8_t intf_number;
    uint8_t alt_setting;
    uint8_t intf_class;
    uint8_t intf_subclass;
    uint8_t intf_protocol;
    uint8_t mac_string_index; /* CDC-ECM iMAC string index when present */
    struct {
        uint8_t address;
        uint8_t type;
        uint16_t max_packet_size;
    } endpoints[MAX_ENDPOINTS];
    int num_endpoints;
};

struct usb_device_info {
    int active;
    int addr;
    int slot_id;
    int port;
    int vid;
    int pid;
    uint8_t dev_class;
    uint8_t dev_subclass;
    uint8_t dev_protocol;
    uint8_t max_packet_size0;
    uint8_t config_value;

    // Information from the selected configuration
    struct usb_interface_info interfaces[MAX_USB_INTERFACES];
    int num_interfaces;
};

void usb_host_init();
void usb_poll();
int usb_get_device_count();
const struct usb_device_info *usb_get_device(int idx);
int usb_control_transfer(int dev_addr, int endpoint, uint8_t max_packet_size, usb_setup_packet_t* setup, void* data, int len);
int usb_bulk_transfer(int dev_addr, int endpoint, uint16_t max_packet_size, int direction, void* data, int len);
int usb_bulk_transfer_actual(int dev_addr, int endpoint, uint16_t max_packet_size, int direction, void* data, int len, int *actual_len);
int ehci_control_transfer(int dev_addr, int endpoint, uint8_t max_packet_size, usb_setup_packet_t* setup, void* data, int len);
int ehci_bulk_transfer(int dev_addr, int endpoint, uint16_t max_packet_size, int direction, void* data, int len);
int usb_set_interface(int dev_addr, uint8_t intf_number, uint8_t alt_setting, uint8_t max_packet_size);
int usb_configure_interface(int dev_addr, const struct usb_interface_info *intf);
int usb_host_has_ehci(void);
int usb_host_has_xhci(void);
int usb_host_using_ehci(void);
int usb_host_using_xhci(void);
int usb_host_has_controller(void);
int usb_host_has_unsupported_xhci(void);
int usb_host_connected_port_count(void);
uint32_t usb_host_first_connected_portsc(void);
const char *usb_host_last_error(void);
