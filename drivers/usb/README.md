# USB Host and RNDIS Driver Skeleton for newos

## Roadmap
1. Implement USB host controller (OHCI/EHCI/XHCI) init and enumeration in `usb_host.c`.
2. Implement RNDIS protocol in `usb_rndis.c` to support USB-tethered phones/dongles.
3. Integrate with `net.c` so USB netif works like existing NIC.

## Resources
- [Linux USB subsystem docs](https://www.kernel.org/doc/html/latest/usb/)
- [TinyUSB (MIT, C, portable)](https://github.com/hathach/tinyusb)
- [RNDIS protocol spec (MSDN)](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/remote-ndis--rndis--)
- [Linux drivers: drivers/net/usb/rndis_host.c, cdc_ether.c]

## Testing
- Use Android/iPhone USB tethering or a USB 4G dongle.
- Boot kernel, check for enumeration, then try HTTP/JSON fetch.
