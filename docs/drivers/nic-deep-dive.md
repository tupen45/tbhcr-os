# NIC Drivers Deep-Dive

Complements [NIC Drivers](../drivers/nic-drivers.md) with register-level
detail for the two shipped drivers.

## Common contract

```
int  e1000e_init(void);     // returns 0 on success
int  rtl8139_init(void);     // returns 0 on success
void rtl8139_poll(void);     // drain RX -> net_rx()
void nic_tx(const void *data, int len);
int  rtl8139_is_ready(void);
```

Both call `netif_register(mac, nic_tx, rtl8139_poll)`. `kmain` prefers
the result of `e1000e_init()` and only calls `rtl8139_init()` if that
returns non-zero.

> **Quirk:** `drivers/e1000e.c` includes `rtl8139.h` and defines
> `rtl8139_init()` as a stub returning `-1`, forcing the e1000e path. The
> real RTL8139 logic is in top-level `rtl8139.c`.

## Intel e1000(e) — MMIO (`drivers/e1000e.c`)

Memory-mapped registers (offsets into the BAR0 MMIO region):

| Register | Offset | Purpose |
|----------|--------|----------|
| `E1000_CTRL` | `0x00000` | Device control |
| `E1000_STATUS` | `0x00008` | Device status |
| `E1000_RCTL` | `0x00100` | Receive control |
| `E1000_RDBAL/H` | `0x02800/04` | RX desc ring base (low/high) |
| `E1000_RDLEN` | `0x02808` | RX ring length |
| `E1000_RDH/T` | `0x02810/18` | RX head/tail |
| `E1000_TCTL` | `0x00400` | Transmit control |
| `E1000_TDBAL/H` | `0x03800/04` | TX desc ring base |
| `E1000_TDLEN` | `0x03808` | TX ring length |
| `E1000_TDH/T` | `0x03810/18` | TX head/tail |

Descriptor rings are pre-filled with `kmalloc`'d buffers. TX copies the
frame into a descriptor and advances the tail; RX is drained in
`rtl8139_poll()` (shared poll name) and forwarded to `net_rx()`.

## Realtek RTL8139 — IO ports (`rtl8139.c`)

Port offsets are relative to `io_base` (BAR0 & ~3):

| Register | Offset | Purpose |
|----------|--------|----------|
| `RL_IDR0..5` | `0x00..05` | MAC bytes |
| `RL_RBSTART` | `0x30` | RX buffer physical address |
| `RL_CMD` | `0x37` | RX/TX enable, reset (`0x10`) |
| `RL_CAPR` | `0x38` | RX read pointer (consumer) |
| `RL_IMR` | `0x3C` | Interrupt mask |
| `RL_ISR` | `0x3E` | Interrupt status |
| `RL_RCR` | `0x44` | Receive config |
| `RL_TCR` | `0x40` | Transmit config |
| `RL_CONFIG1` | `0x52` | Power config |

`nic_tx()` enforces 60–1792 byte frames, copies into one of 4 rotating
DMA buffers, and kicks TX via `TSADn`/`TSDn`. `rtl8139_poll()` reads
`ISR`, and for each `RX OK` frame (`[status][len][data]`, CRC stripped)
calls `net_rx()`.

## DMA & physical addresses

Both drivers treat `kmalloc`'d buffers as physically contiguous via the
identity `VIRT_TO_PHYS` macro. On real hardware with an IOMMU this would
need a real virt→phys translation.

## Adding a NIC

Implement the four-function contract, `pci_find_device()` for your
vendor/device, enable bus-master + IO/MMIO, map BARs, then
`netif_register()`. See [PCI](../drivers/pci.md).
