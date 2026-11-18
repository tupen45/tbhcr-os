# Input & Driver Registration Reference

Reference for the input devices and how NIC/USB drivers plug into the core.

## Mouse (`mouse.c` / `mouse.h`)

| Symbol | Meaning |
|--------|----------|
| `int mouse_x, mouse_y` | Last cursor position (init to center). |
| `init_mouse()` | Enable aux device + IRQ12, defaults, enable reporting. |
| `mouse_wait_read/write()` | Spin on controller status (`0x64`). |
| `mouse_write(val)` | `0xD4` to cmd port, then `val` to data port. |
| `mouse_read()` | Read one byte from data port (`0x60`). |

Packet decode lives in `kernel.c:mouse_read_packet()` (3-byte PS/2
packet: `dx=pkt[1]`, `dy=-pkt[2]`, `buttons=pkt[0]`).

## Keyboard (in `kernel.c`)

| Symbol | Meaning |
|--------|----------|
| `keyboard_read_char(&c)` | Returns 1 + fills `c` (ASCII) on a key make; tracks shift; maps scancodes. |
| `keyboard_scancode_to_char(sc, shifted)` | Hard-coded scancode→char table. |
| Special | `0x0E`→BS, `0x1C`→LF, `0x0F`→TAB, `&0x80`→key-up (ignored). |

## NIC driver contract

Every NIC driver must expose:

```c
int  e1000e_init(void);        // returns 0 on success
int  rtl8139_init(void);       // stub in e1000e.c returns -1
void rtl8139_poll(void);       // drain RX -> net_rx()
void nic_tx(const void *data, int len);  // TX entry used by net.c
int  rtl8139_is_ready(void);
```

On success the driver calls
`netif_register(mac, nic_tx, rtl8139_poll)` so `net.c` can send and
poll. See [NIC Drivers](../drivers/nic-drivers.md).

## USB module contract

Each tether module (`usb_rndis`, `usb_ipheth`, `usb_cdc_ecm`)
registers a `net_if` on bind and sets its `*_active` flag. The core USB
API (init/poll/enumeration/status) is documented in
[USB Host](../drivers/usb-host.md).

## PCI helper

`pci_find_device(vendor, device, &bus, &slot, &func)` returns 1 and
fills the location. NIC drivers use it to locate the device, then read
`0x04` (command: set IO + bus-master) and `0x10` (BAR0) to map
registers. See [PCI](../drivers/pci.md).

See also: [Input](../kernel/input.md).
