# Network Interface Card Drivers

MyOS ships two interchangeable NIC drivers that expose an **identical API**
so `kmain` can prefer one and fall back to the other:

```c
int  e1000e_init(void);     // Intel e1000(e) — preferred
int  rtl8139_init(void);     // Realtek RTL8139 — fallback
void rtl8139_poll(void);    // RX drain (called by net_poll)
void nic_tx(const void *data, int len);   // TX entry used by net.c
int  rtl8139_is_ready(void);
```

> **Naming quirk:** `drivers/e1000e.c` includes `rtl8139.h` and
> *defines* `rtl8139_init()` as a stub that always returns `-1` (forcing
> the e1000e path to win). The real RTL8139 logic lives in top-level
> `rtl8139.c` and is selected only when e1000e enumeration fails.

## Intel e1000(e) — `drivers/e1000e.c`

A **memory-mapped I/O (MMIO)** polling driver for the common QEMU `e1000`
device (vendor `0x8086`, class `0x02`). It uses descriptor rings:

| Register | Purpose |
|---------|----------|
| `E1000_CTRL` / `E1000_STATUS` | Device control / status |
| `E1000_RCTL` | Receive control |
| `E1000_RDBAL/H`, `RDLEN`, `RDH`, `RDT` | RX descriptor ring base/len/head/tail |
| `E1000_TCTL` | Transmit control |
| `E1000_TDBAL/H`, `TDLEN`, `TDH`, `TDT` | TX descriptor ring |

After PCI enable + BAR mapping, it allocates/fills descriptor rings, enables
RX/TX, reads the MAC, and registers with `netif_register(mac, nic_tx,
rtl8139_poll)`. TX copies the frame into a descriptor buffer and advances
the tail pointer; RX is drained in the poll routine and forwarded to
`net_rx()`.

## Realtek RTL8139 — `rtl8139.c`

An **I/O-port** driver for the classic QEMU `rtl8139` NIC
(vendor `0x10EC`, device `0x8139`). It favors simplicity and DMA-safe
buffers:

- 4 preallocated TX DMA buffers (`TX_BUF_SIZE 1792`) rotated by `tx_idx`.
- One 8 KiB RX ring (`RL_RXBUF`) with the datasheet's CAPR wrap handling.
- `nic_tx()` enforces 60–1792 byte frame bounds, copies into a DMA
  buffer, and kicks TX via TSAD/TSD registers.
- `rtl8139_poll()` reads `ISR`, acknowledges it, and for each RX frame
  (`[status][len][data]`) strips the CRC and calls `net_rx()`.

`VIRT_TO_PHYS` is identity-mapped for low memory; if you add an IOMMU or
real MMU, this macro is the single place to fix.

## How `kmain` selects a NIC

```c
if (rndis_active || ipheth_active || cdc_ecm_active) {
    usb_net_found = 1;                 // USB tether wins
}
if (!usb_net_found) {
    if (e1000e_init() != 0) rtl8139_init();   // PCI fallback
    fallback_nic_started = 1;
}
if (g_active_netif) dhcp_start();
```

If networking never comes up (e.g. no device), the desktop loop keeps
retrying `e1000e_init()`/`rtl8139_init()` + `dhcp_start()` on each
iteration until an interface appears.

## Adding a new NIC driver

1. Implement the same four-function contract (init, poll, `nic_tx`, ready).
2. Read your PCI vendor/device via `pci_find_device()` (`pci.c`).
3. Enable bus-mastering and map BARs.
4. Call `netif_register(mac, your_tx, your_poll)` so `net.c` can use it.

Next: [PCI](pci.md).
