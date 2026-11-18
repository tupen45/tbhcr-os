# Welcome Screen Status Codes

The first screen the user sees (`show_welcome()` in `kernel.c`) computes a
single integer via `welcome_status_code()` and renders a color-coded status
plus USB diagnostics. This page is the field guide for interpreting it.

## Code table

| Code | Condition (`welcome_status_code`) | Rendered message | Color |
|------|----------------------------------------|----------------------|--------|
| 4 | `usb_host_has_unsupported_xhci()` | "USB: xHCI detected (unsupported)" + red banner "USB TETHERING BLOCKED" | red |
| 0 | `usb_get_device_count() <= 0` | "USB: Not connected" | red |
| 5 | `!usb_host_has_controller()` | "USB: Host controller not detected" | red |
| 1 | device found but `!rndis_active && !ipheth_active` | One of: "iPhone tether found (bind failed)" / "Tethering device found (bind failed)" / "Device found, but not a net adapter" | amber |
| 2 | attached but `!dhcp_is_bound()` | "iPhone attached (DHCP pending)" / "RNDIS attached (DHCP pending)" | amber |
| 6 | only hub-class devices connected | "USB: Only hub-class devices found" + amber banner "USB HUB DETECTED" | amber |
| 3 | otherwise (RNDIS/iPheth + DHCP bound) | "USB: iPhone DHCP ready" / "USB: RNDIS + DHCP ready" | green |

Codes 1/2 also branch on `usb_has_ipheth_candidate_device()` and
`usb_has_rndis_candidate_device()` to report *which* kind of device was
seen even when binding failed.

## Diagnostic block

Below the status line, the welcome screen always prints:

```
USB devices: <count>
USB host: <EHCI active | xHCI active | xHCI init failed | EHCI init failed | none>
USB ports: <connected port count>
PORTSC: <08x>                 (only if a port is connected)
USB err: <last error>         (if 0 devices but ports>0)
VID:PID <04x>:<04x> ifs:<n>  (first device)
IF0 n<d> a<a> c<02x> s<02x> p<02x> ep<d>  (first interface)
```

## How to read it

- **Green (3):** everything is working — click **Start**.
- **Amber (1/2/6):** a device is present but not yet usable; wait for
  DHCP, or check the tether binding. A hub (6) means the phone is behind
  a hub the kernel can't traverse.
- **Red (0/4/5):** no usable networking via USB. The OS will still try
  a PCI NIC; if there's no NIC either, the browser won't fetch.
- **Red (4) banner:** your machine is xHCI-only; use the EHCI
  passthrough template from [Running](../getting-started/running.md).

See also: [Desktop & Shell](../apps/desktop.md),
[USB Host](../drivers/usb-host.md), [USB Tether](../reference/usb-tether.md).
