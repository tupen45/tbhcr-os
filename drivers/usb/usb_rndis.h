#pragma once
#include <stdint.h>

// RNDIS is a Class 2 (CDC) device, but with vendor-specific subclass/protocol
#define USB_CLASS_CDC_DATA      0x0A
#define USB_CLASS_WIRELESS      0xE0
#define USB_SUBCLASS_RNDIS      0x01
#define USB_PROTOCOL_RNDIS      0x03

// RNDIS Control Message Types
#define RNDIS_MSG_INIT          0x00000002
#define RNDIS_MSG_INIT_C        0x80000002
#define RNDIS_MSG_HALT          0x00000003
#define RNDIS_MSG_QUERY         0x00000004
#define RNDIS_MSG_QUERY_C       0x80000004
#define RNDIS_MSG_SET           0x00000005
#define RNDIS_MSG_SET_C         0x80000005
#define RNDIS_MSG_RESET         0x00000006
#define RNDIS_MSG_RESET_C       0x80000006
#define RNDIS_MSG_KEEPALIVE     0x00000008
#define RNDIS_MSG_KEEPALIVE_C   0x80000008

// RNDIS Data Message Type
#define RNDIS_MSG_PACKET        0x00000001

// RNDIS OIDs (Object IDs)
#define OID_GEN_SUPPORTED_LIST            0x00010101
#define OID_GEN_MAC_OPTIONS               0x0001010D
#define OID_GEN_PERMANENT_MAC_ADDRESS     0x00010102
#define OID_GEN_MAXIMUM_FRAME_SIZE        0x00010106
#define OID_GEN_MAXIMUM_TOTAL_SIZE        0x00010111
#define OID_GEN_CURRENT_PACKET_FILTER     0x0001010E

// RNDIS Packet Filter Bits
#define RNDIS_PACKET_TYPE_DIRECTED        0x0001
#define RNDIS_PACKET_TYPE_MULTICAST       0x0002
#define RNDIS_PACKET_TYPE_ALL_MULTICAST   0x0004
#define RNDIS_PACKET_TYPE_BROADCAST       0x0008
#define RNDIS_PACKET_TYPE_PROMISCUOUS     0x0020

// RNDIS Message Structures
typedef struct {
    uint32_t MessageType;
    uint32_t MessageLength;
    uint32_t RequestID;
} __attribute__((packed)) rndis_header_t;

typedef struct {
    uint32_t MessageType;
    uint32_t MessageLength;
    uint32_t RequestID;
    uint32_t MajorVersion;
    uint32_t MinorVersion;
    uint32_t MaxTransferSize;
} __attribute__((packed)) rndis_init_t;

typedef struct {
    uint32_t MessageType;
    uint32_t MessageLength;
    uint32_t RequestID;
    uint32_t Status;
    uint32_t MajorVersion;
    uint32_t MinorVersion;
    uint32_t DeviceFlags;
    uint32_t Medium;
    uint32_t MaxPacketsPerTransfer;
    uint32_t MaxTransferSize;
    uint32_t PacketAlignmentFactor;
    uint32_t AFListOffset;
    uint32_t AFListSize;
} __attribute__((packed)) rndis_init_cmplt_t;

typedef struct {
    uint32_t MessageType;
    uint32_t MessageLength;
    uint32_t RequestID;
    uint32_t Oid;
    uint32_t InformationBufferLength;
    uint32_t InformationBufferOffset;
    uint32_t DeviceVcHandle;
} __attribute__((packed)) rndis_query_t;

typedef struct {
    uint32_t MessageType;
    uint32_t MessageLength;
    uint32_t RequestID;
    uint32_t Status;
    uint32_t InformationBufferLength;
    uint32_t InformationBufferOffset;
} __attribute__((packed)) rndis_query_cmplt_t;

typedef struct {
    uint32_t MessageType;
    uint32_t MessageLength;
    uint32_t RequestID;
    uint32_t Oid;
    uint32_t InformationBufferLength;
    uint32_t InformationBufferOffset;
    uint32_t DeviceVcHandle;
} __attribute__((packed)) rndis_set_t;

typedef struct {
    uint32_t MessageType;
    uint32_t MessageLength;
    uint32_t RequestID;
    uint32_t Status;
} __attribute__((packed)) rndis_set_cmplt_t;

typedef struct {
    uint32_t MessageType;
    uint32_t MessageLength;
    uint32_t DataOffset;
    uint32_t DataLength;
    uint32_t OOBDataOffset;
    uint32_t OOBDataLength;
    uint32_t NumOOBDataElements;
    uint32_t PerPacketInfoOffset;
    uint32_t PerPacketInfoLength;
    uint32_t VcHandle;
    uint32_t Reserved;
} __attribute__((packed)) rndis_packet_t;

// USB Class-Specific Requests for RNDIS
#define REQ_SEND_ENCAPSULATED_COMMAND 0x00
#define REQ_GET_ENCAPSULATED_RESPONSE 0x01

void usb_rndis_init(void);
int usb_rndis_send(const void *data, int len);
void usb_rndis_poll(void);
void usb_rndis_handle_disconnect(int dev_addr);

extern int rndis_active;
