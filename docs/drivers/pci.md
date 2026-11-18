# PCI Bus Access (`pci.c`)

MyOS interacts with PCI devices (NICs, USB controllers) through **PCI
configuration space** using the legacy `0xCF8`/`0xCFC` IO ports.

## Configuration address register

```c
static inline uint32_t pci_config_addr(uint8_t bus, uint8_t slot,
                                       uint8_t func, uint8_t offset) {
    return 0x80000000u
        | ((uint32_t)bus  << 16)
        | ((uint32_t)slot << 11)
        | ((uint32_t)func << 8)
        | (offset & 0xFC);
}
```

Bit 31 enables the transaction; the remaining fields encode bus/slot/
function/offset. Reads/writes go through `PCI_CONFIG_ADDRESS` (`0xCF8`)
and `PCI_CONFIG_DATA` (`0xCFC`).

## Accessors

| Function | Width |
|----------|-------|
| `pci_config_read32(bus,slot,func,off)` | 32-bit |
| `pci_config_read16(...)` | 16-bit (shifts within the 32-bit read) |
| `pci_config_read8(...)` | 8-bit |
| `pci_config_write32(bus,slot,func,off,val)` | 32-bit write |

The 16/8-bit helpers read the enclosing 32-bit word and shift by
`(offset & 2)*8` / `(offset & 3)*8`.

## Device discovery — `pci_config_read32`

```c
int pci_find_device(uint16_t vendor, uint16_t device,
                    uint8_t *out_bus, uint8_t *out_slot,
                    uint8_t *out_func);
```

Walks all 256 buses × 32 slots × 8 functions. For each function it reads
the vendor ID at offset `0x00`; a value of `0xFFFF` means "no device".
When both `vendor` and `device` match, it fills the out pointers and
returns `1`. It honors the **multifunction** header bit (`0x0E` bit 7) to
skip unused functions on single-function devices.

## How NIC drivers use it

`rtl8139_init()` and `e1000e_init()` call `pci_find_device()` with the
relevant vendor/device IDs, then read the **command register** at `0x04`
to set IO-space (`0x1`) and bus-master (`0x4`) bits, and read the **BAR0**
at `0x10` to locate the IO base (RTL8139) or MMIO base (e1000e).

## Limitations

- Legacy PCI only (no PCIe ECAM / MMCFG).
- Single-domain (bus 0–255, the conventional flat space).
- No bridge traversal beyond the root — fine for QEMU's flat PCI layout.

Next: [Syscalls](userland/syscalls.md).
