// drivers/usb/ehci.h

#pragma once
#include <stdint.h>

// EHCI Capability Registers (offsets from MMIO base)
#define EHCI_CAP_CAPLENGTH    0x00 // Capability Register Length (8-bit)
#define EHCI_CAP_HCIVERSION   0x02 // Host Controller Interface Version (16-bit)
#define EHCI_CAP_HCSPARAMS    0x04 // Host Controller Structural Parameters
#define EHCI_CAP_HCCPARAMS    0x08 // Host Controller Capability Parameters
#define EHCI_CAP_HCSP_PORTROUTE 0x0C // Companion Port Route Description

// EHCI Operational Registers (offsets from MMIO base + CAPLENGTH)
#define EHCI_OP_USBCMD        0x00 // USB Command
#define EHCI_OP_USBSTS        0x04 // USB Status
#define EHCI_OP_USBINTR       0x08 // USB Interrupt Enable
#define EHCI_OP_FRINDEX       0x0C // Frame Index
#define EHCI_OP_CTRLDSSEGMENT 0x10 // Control Data Structure Segment
#define EHCI_OP_PERIODICLISTBASE 0x14 // Periodic Frame List Base Address
#define EHCI_OP_ASYNCLISTADDR 0x18 // Asynchronous List Base Address
#define EHCI_OP_CONFIGFLAG    0x40 // Configure Flag
#define EHCI_OP_PORTSC_BASE   0x44 // Port Status/Control (first port)

// USBCMD bits
#define CMD_RUN_STOP        (1 << 0)
#define CMD_HCRESET         (1 << 1)
#define CMD_PERIODIC_ENABLE (1 << 4)
#define CMD_ASYNC_ENABLE    (1 << 5)
#define CMD_INT_DOORBELL    (1 << 6)

// USBSTS bits
#define STS_HCHALTED        (1 << 12)

// PORTSC bits
#define PORTSC_CONNECT_STATUS   (1 << 0)
#define PORTSC_CONNECT_CHANGE   (1 << 1)
#define PORTSC_PORT_ENABLED     (1 << 2)
#define PORTSC_ENABLE_CHANGE    (1 << 3)
#define PORTSC_PORT_RESET       (1 << 8)
#define PORTSC_LINE_STATUS_K    (1 << 10)
#define PORTSC_LINE_STATUS_J    (1 << 11)
#define PORTSC_PORT_POWER       (1 << 12)
#define PORTSC_OWNER            (1 << 13)

// HCCPARAMS bits
#define HCC_64BIT_CAP       (1 << 0) // 64-bit addressing capability
#define HCC_EECP_MASK       (0xFF << 8) // EHCI Extended Capabilities Pointer

// --- EHCI Data Structures for Transfers ---
// Must be 32-byte aligned

// Queue Element Transfer Descriptor (qTD)
typedef struct {
    volatile uint32_t next_qtd;         // Next qTD pointer
    volatile uint32_t alt_next_qtd;     // Alternate next qTD pointer
    volatile uint32_t token;            // Status and control flags
    volatile uint32_t buffer[5];        // Buffer pointers
    volatile uint32_t extended_buffer[5]; // For 64-bit
} __attribute__((packed)) ehci_qtd_t;

// Queue Head (qH)
typedef struct {
    volatile uint32_t qh_link;          // Horizontal link pointer
    volatile uint32_t ep_char;          // Endpoint characteristics
    volatile uint32_t ep_caps;          // Endpoint capabilities
    volatile uint32_t current_qtd;      // Current qTD pointer
    // Overlay area (corresponds to qTD fields)
    volatile uint32_t next_qtd_overlay;
    volatile uint32_t alt_next_qtd_overlay;
    volatile uint32_t token_overlay;
    volatile uint32_t buffer_overlay[5];
    volatile uint32_t extended_buffer_overlay[5];
} __attribute__((packed)) ehci_qh_t;

// qTD Token bits
#define QTD_STATUS_ACTIVE   (1 << 7)
#define QTD_STATUS_HALTED   (1 << 6)
#define QTD_PID_OUT         (0 << 8)
#define QTD_PID_IN          (1 << 8)
#define QTD_PID_SETUP       (2 << 8)
#define QTD_CERR_MASK       (3 << 10)
#define QTD_TOTAL_BYTES_SHIFT 16

// USB Standard Request Codes
#define REQ_GET_STATUS        0x00
#define REQ_CLEAR_FEATURE     0x01
#define REQ_SET_FEATURE       0x03
#define REQ_SET_ADDRESS       0x05
#define REQ_GET_DESCRIPTOR    0x06
#define REQ_SET_DESCRIPTOR    0x07
#define REQ_GET_CONFIGURATION 0x08
#define REQ_SET_CONFIGURATION 0x09
#define REQ_SET_INTERFACE     0x0B

// USB Descriptor Types
#define DESC_DEVICE           1
#define DESC_CONFIGURATION    2
#define DESC_STRING           3
#define DESC_INTERFACE        4
#define DESC_ENDPOINT         5

// Standard Device Request (Setup Packet)
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;
