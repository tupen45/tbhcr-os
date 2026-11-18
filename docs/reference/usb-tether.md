# USB Tethering Reference

MyOS can use a phone as its network interface. This page summarizes the
three tether classes and their readiness flags.

## Active-interface flags

Set by the respective USB module when it successfully binds a network
interface and calls `netif_register()`:

| Flag | Set by | Phone type |
|------|---------|-------------|
| `rndis_active` | `usb_rndis.c` | Android USB tether (RNDIS) |
| `ipheth_active` | `usb_ipheth.c` | Apple iPhone (iPheth) |
| `cdc_ecm_active` | `usb_cdc_ecm.c` | CDC-ECM USB Ethernet |

`kmain` checks these before falling back to a PCI NIC:

```c
if (rndis_active || ipheth_active || cdc_ecm_active)
    usb_net_found = 1;
```

## Candidate detection (`kernel.c`)

The welcome screen decides *why* a device is not usable via:

| Function | Detects |
|----------|-----------|
| `usb_device_is_hub(dev)` | Device or interface class `0x09`. |
| `usb_only_hub_devices_connected()` | Every enumerated device is a hub. |
| `usb_has_rndis_candidate_device()` | Interface `WIRELESS(0xE0)/RNDIS(0x01/0x03)` or `CDC_DATA(0x0A)`. |
| `usb_has_ipheth_candidate_device()` | Vendor == `USB_VENDOR_APPLE` (and iPheth interface). |

## Class constants (`drivers/usb/usb_host.h`)

```c
#define USB_CLASS_HUB        0x09
#define USB_CLASS_WIRELESS   0xE0
#define USB_SUBCLASS_RNDIS  0x01
#define USB_PROTOCOL_RNDIS   0x03
#define USB_CLASS_CDC_DATA   0x0A
#define USB_VENDOR_APPLE     /* Apple Inc. */
```

## Welcome status code mapping

See [Desktop & Shell](../apps/desktop.md) and
[USB Host](../drivers/usb-host.md) for the full 0–6 code table. In
short: codes 1–3 indicate a tether is present (bind/DHCP progress), while
4 (xHCI only) and 6 (hubs only) are "won't work yet" conditions.

## Recommended invocation

EHCI passthrough is the most reliable path — see the
[Running](../getting-started/running.md) page for the exact QEMU
templates (`usb-ehci` / `qemu-xhci` + `usb-host`).
