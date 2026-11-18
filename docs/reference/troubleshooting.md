# Troubleshooting

A practical checklist for the most common problems when building or running
MyOS.

## Build failures

### `i686-elf-gcc: command not found`
The cross toolchain is not on `PATH`. Install/build it, or activate your
environment before running `build.sh`.

### `cannot find -lgcc` / libgcc path error
`build.sh:107` hard-codes
`/home/tupenshil/opt/cross/lib/gcc/i686-elf/13.1.0/libgcc.a`.
Update that path to wherever your toolchain's `libgcc.a` lives.

### `xorriso: command not found` (grub-mkrescue fails)
Install `xorriso` (the backend `grub-mkrescue` shells out to).

### mbedTLS does not compile
`vendor/mbedtls` + `config_mini.h` + `platform_shim.c` are tightly
coupled. Do not swap in a different mbedTLS version without updating the
shim and config.

## Boot / runtime

### Black screen after GRUB
- Confirm the ISO was built from a successful `build.sh` (check `kernel.bin`
  size, ~250 KiB).
- Run with `-serial mon:stdio` to see `printf` output; a panic/abort
  prints via the serial console.

### Welcome screen shows "USB: Not connected" (status 0)
Expected if no USB device is passed through. Networking still works via the
PCI NIC (`e1000`). Pass a phone tether for USB networking.

### "USB TETHERING BLOCKED" (status 4)
Your machine exposes only an **xHCI** controller. The kernel's most-tested
USB path is **EHCI**. Pass `-device usb-ehci,id=ehci` and use the EHCI
passthrough template in [Running](../getting-started/running.md).

### "USB HUB DETECTED" (status 6)
A hub is between the kernel and the phone. Plug the phone into a direct
root port or a simpler adapter.

### DHCP never binds; browser shows "Fetch failed"
- Without a USB tether, `dhcp.c` applies the **slirp fallback**
  (`10.0.2.15/24`, gw `10.0.2.2`, DNS `10.0.2.3`) after 8
  retries — this should make the browser work under QEMU user networking.
- With a phone tether there is **no fallback**; the tether's DHCP must
  succeed. Watch `-serial mon:stdio` for `dhcp:` logs.
- Verify the QEMU `-netdev user` / `-device e1000` options are present.

### Browser/JSON viewer blank or truncated
Responses are clipped to the window (~90 lines / 8 KiB body buffer). This
is by design for the demo UI, not a fetch failure.

### TLS sites fail
- Certificate verification is **disabled** and entropy is a **toy LFSR** —
  HTTPS works functionally but is *not secure* (see [TLS](../networking/tls.md)).
- Only plain HTTPS GET by resolved IP is supported; redirects to a
  different hostname (with no DNS in the TLS path) abort.

### Mouse cursor "smears"
The software cursor uses save/restore; a stray unmatched draw can leave
artifacts. Clicking around a window normally repaints cleanly.

## Getting more logs

Append `-serial mon:stdio` to any QEMU command. The network stack is
verbose: `dhcp:`, `dns:`, `tcp:`, and mbedTLS debug strings all print
there. The repo also keeps `myos_serial.log` and `serial_*.log` samples.
