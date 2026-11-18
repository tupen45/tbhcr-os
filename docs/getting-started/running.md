# Running in QEMU

Once `myos.iso` exists (see [Build](build.md)), you can run MyOS under
QEMU. This page lists the recommended invocations and explains the important
options.

## Basic run (NIC networking)

```bash
qemu-system-i386 -m 256 \
  -netdev user,id=n1,hostfwd=udp::6000-:6000,hostfwd=udp::6001-:6001 \
  -device e1000,netdev=n1 \
  -cdrom myos.iso -usbdevice mouse
```

| Option | Meaning |
|--------|---------|
| `-m 256` | 256 MiB RAM (plenty; kernel heap is 1 MiB at 16 MiB) |
| `-netdev user,id=n1,...` | QEMU "slirp" user-mode networking; `10.0.2.0/24` guest subnet |
| `-device e1000,netdev=n1` | Intel e1000 NIC — MyOS's preferred driver |
| `-cdrom myos.iso` | Boot the GRUB ISO |
| `-usbdevice mouse` | Attach a USB mouse (PS/2 emulation still works) |

The two `hostfwd=udp` rules forward UDP ports 6000/6001 from the host to the
guest — useful if you run a host-side UDP→HTTP helper (see `udp_proxy_*`
state in `kernel.c`).

After boot you will see the **welcome screen** showing live USB/network
status. Click **Start** to reach the desktop.

## Recommended run (Wi-Fi sharing, no phone)

```bash
qemu-system-i386 -m 512 \
  -cdrom myos.iso \
  -serial mon:stdio \
  -netdev user,id=n1,hostfwd=udp::6000-:6000,hostfwd=udp::6001-:6001 \
  -device e1000,netdev=n1 \
  -device qemu-xhci,id=xhci
```

`-serial mon:stdio` lets you watch `printf` debugging (DHCP/DNS/TCP logs)
on the host terminal while the graphical UI runs in the QEMU window.

## USB tethering (phone as NIC)

MyOS can use a phone's USB tether as its network interface via RNDIS
(Android) or the Apple iPhone Ethernet driver (iPheth). Because the kernel
prefers USB over PCI, pass the phone through:

**EHCI (recommended — the kernel's primary USB path):**

```bash
# Find the phone's bus/device: lsusb -t
sudo qemu-system-i386 -m 512 \
  -cdrom myos.iso \
  -serial mon:stdio \
  -device usb-ehci,id=ehci \
  -device usb-host,bus=ehci.0,hostbus=<BUS>,hostaddr=<DEV>,guest-reset=false,guest-resets-all=false,loglevel=4
```

**xHCI** (supported but the welcome screen warns if the only controller is
xHCI, since the kernel's most-tested path is EHCI):

```bash
sudo qemu-system-i386 -m 512 \
  -cdrom myos.iso \
  -serial mon:stdio \
  -netdev user,id=n1,hostfwd=udp::6000-:6000,hostfwd=udp::6001-:6001 \
  -device e1000,netdev=n1 \
  -device qemu-xhci,id=xhci \
  -device usb-host,bus=xhci.0,hostbus=1,hostaddr=6,guest-reset=false,guest-resets-all=false,loglevel=4
```

> Example: `Bus 01 Dev 06` becomes `hostbus=1,hostaddr=6`.

## What to expect on first boot

1. Welcome screen enumerates USB devices and shows a status code (see
   `welcome_status_code()` in `kernel.c`):
   - `0` USB not connected
   - `1` tether device found, bind failed
   - `2` attached, DHCP pending
   - `3` RNDIS/iPheth + DHCP ready
   - `4` xHCI only (unsupported tether)
   - `5` no host controller
   - `6` only USB hubs visible
2. Click **Start** → desktop with taskbar.
3. Open **Browser (Posts)** or **JSON Viewer (Users)** to see live HTTP
   fetches from `jsonplaceholder.typicode.com`.

## Debugging tips

- Keep `-serial mon:stdio` open; the network stack is verbose.
- If DHCP never binds, the client applies the **slirp fallback**
  (`10.0.2.15/24`, gw `10.0.2.2`, DNS `10.0.2.3`) after 8 retries
  when no USB tether is active.
- To capture the kernel's own serial log, the project ships `myos_serial.log`
  and several `serial_*.log` files in the repo root.

Next: [Desktop & Shell](apps/desktop.md).
