# USB Host Stack (`drivers/usb/`)

MyOS includes a compact USB host stack used primarily to turn a phone's USB
tether into a network interface. It supports **EHCI** (the kernel's primary,
best-tested path) and **xHCI**, plus three tether classes: RNDIS
(Android), iPheth (iPhone), and CDC-ECM.

## Source map

| File | Responsibility |
|------|----------------|
| `usb_host.h` / `usb_host.c` | Core host controller abstraction, device/descriptor structs, enumeration, status API |
| `xhci.h` / `xhci.c` | xHCI (USB 3.0) controller driver |
| `ehci.h` | EHCI register/descriptor definitions and `usb_setup_packet_t` |
| `usb_rndis.c` / `usb_rndis.h` | RNDIS tether (Android USB tether) |
| `usb_ipheth.c` / `usb_ipheth.h` | Apple iPhone Ethernet (iPheth) tether |
| `usb_cdc_ecm.c` / `usb_cdc_ecm.h` | CDC-ECM class driver |

## Key data types (`usb_host.h`)

- `usb_device_descriptor_t`, `usb_configuration_descriptor_t`,
  `usb_interface_descriptor_t`, `usb_endpoint_descriptor_t` — standard
  USB descriptors (`__attribute__((packed))`).
- `struct usb_device_info` — a discovered device: `vid`, `pid`,
  `dev_class`, an array of `usb_interface_info` interfaces, etc.
- `struct usb_interface_info` — `intf_number`, `intf_class`,
  `intf_subclass`, `intf_protocol`, `num_endpoints`.

## Public API (used by `kernel.c`)

| Function | Returns / Does |
|----------|------------------|
| `usb_host_init()` | Initialize the first available controller (EHCI preferred) |
| `usb_poll()` | Advance enumeration / transfer state (called from `net_poll`) |
| `usb_get_device_count()` | Number of enumerated devices |
| `usb_get_device(i)` | Pointer to device `i` (or NULL) |
| `usb_host_has_controller()` | Any controller present? |
| `usb_host_using_ehci()` / `usb_host_using_xhci()` | Which controller is active |
| `usb_host_has_ehci()` / `usb_host_has_xhci()` | Controller type present |
| `usb_host_has_unsupported_xhci()` | xHCI-only machine → tether unsupported |
| `usb_host_connected_port_count()` | Ports with a device |
| `usb_host_first_connected_portsc()` | PORTSC register of first connected port |
| `usb_host_last_error()` | Human-readable last error string |

## Device classes of interest

```c
#define USB_CLASS_HUB        0x09
#define USB_CLASS_WIRELESS   0xE0
#define USB_SUBCLASS_RNDIS  0x01
#define USB_PROTOCOL_RNDIS   0x03
#define USB_CLASS_CDC_DATA   0x0A
#define USB_VENDOR_APPLE      /* Apple Inc. */
```

`kernel.c` uses these to decide whether a connected device is a usable
tether (`usb_has_rndis_candidate_device`, `usb_has_ipheth_candidate_device`)
or just a hub (`usb_only_hub_devices_connected`).

## Integration with networking

When a tether binds, the corresponding module registers a `net_if` via
`netif_register()` (setting `rndis_active` / `ipheth_active` /
`cdc_ecm_active`). `kmain` prefers this interface over a PCI NIC. The
welcome screen's status codes (0–6) reflect the USB+DHCP situation.

## Behavior notes

- Enumeration is **polling-driven** — `usb_poll()` must be called
  frequently. `net_poll()` calls `usb_poll()` on every UI iteration, and
  `kmain` calls it 8× at startup to catch already-attached devices.
- Real hardware is slower than QEMU; the project deliberately polls often
  to let tethered phones finish attaching.
- If only **xHCI** is available (many modern laptops), the welcome screen
  shows "USB TETHERING BLOCKED" — the kernel's most reliable path is EHCI.
- Hub traversal is limited; the welcome screen explicitly warns when only
  hub-class devices are visible ("Try a direct USB port").

Next: [NIC Drivers](nic-drivers.md).
