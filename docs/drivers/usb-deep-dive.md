# USB Controllers Deep-Dive

MyOS's USB stack (`drivers/usb/`) supports two host controllers and three
tether classes. This page goes a level deeper than
[USB Host](../drivers/usb-host.md).

## Controller preference

`kmain` / `usb_host_init()` probe for a controller in this order of
preference:

1. **EHCI** (USB 2.0) — the kernel's most-tested and best-supported
   path. Prefered for Android RNDIS and iPhone tethering.
2. **xHCI** (USB 3.0) — supported, but the welcome screen warns
   ("USB TETHERING BLOCKED") if it is the *only* controller, because the
   bulk of tether testing was done on EHCI.

The flags `usb_host_using_ehci()` / `usb_host_using_xhci()` report which
is active; `usb_host_has_unsupported_xhci()` is true when xHCI is the
only option.

## EHCI (`ehci.h`)

Defines the standard EHCI register block and descriptor layouts, including
`usb_setup_packet_t` (the 8-byte control SETUP packet used for all
descriptor reads). EHCI uses periodic/asynchronous schedules and QH/TD
structures; the project's `usb_host.c` drives a minimal subset needed to
enumerate a few devices and run a tether bulk pipe.

## xHCI (`xhci.c` / `xhci.h`)

The xHCI driver manages the newer register model (capability/operational/
runtime/doorbell regions), command/event rings, and transfer request
blocks (TRBs). It is more complex than EHCI; hence the cautious welcome
screen messaging.

## Tether classes

| Class | File | Device | Bind flag |
|-------|------|--------|-----------|
| RNDIS | `usb_rndis.c` | Android USB tether (WIRELESS/0xE0, subclass 0x01, proto 0x03) | `rndis_active` |
| iPheth | `usb_ipheth.c` | Apple iPhone (vendor `USB_VENDOR_APPLE`) | `ipheth_active` |
| CDC-ECM | `usb_cdc_ecm.c` | USB CDC Ethernet Control Model | `cdc_ecm_active` |

Each module, on a successful bind, registers a `net_if` via
`netif_register()` so `net.c` sends/receives through the phone.

## Enumeration flow (polling)

```
usb_host_init()
  └─ probe EHCI then xHCI
kmain: for i in 0..7: usb_poll()    // catch already-attached
loop: net_poll() -> usb_poll()
  └─ read port status (PORTSC)
  └─ on connect: GET_DESCRIPTOR(device)
  └─ GET_DESCRIPTOR(config) -> interfaces/endpoints
  └─ match class -> tether module -> netif_register()
```

Because enumeration is poll-driven, `net_poll()` is called *frequently* —
every UI iteration and 8× at startup — so tethered phones finish
attaching even on slower real hardware.

## Limitations

- **Hub traversal is limited.** If the phone is behind a hub, the welcome
  screen shows code 6 ("Only hub-class devices found").
- No isochronous / interrupt-heavy device support beyond what the tether
  pipes need.
- xHCI is functional but secondary; prefer EHCI passthrough under QEMU.

See also: [USB Tether](../reference/usb-tether.md),
[Running](../getting-started/running.md).
