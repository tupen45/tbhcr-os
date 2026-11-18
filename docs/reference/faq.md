# Frequently Asked Questions

**Q: Is MyOS 64-bit?**
No. It is a 32-bit (`i686`) protected-mode kernel. There is no long
mode / paging. See [Boot Flow](../architecture/boot-flow.md).

**Q: Does it have processes or threads?**
No scheduler. The kernel runs everything in one ring-0 address space.
"Applications" are functions run cooperatively by the desktop event loop.
See [Control Flow](../architecture/control-flow.md).

**Q: Why is everything polling instead of interrupts?**
Simplicity. NIC/USB receive is driven by `net_poll()`, called in every
UI loop. The `int 0x80` IDT entry exists only for the user-program
syscall gate. See [Control Flow](../architecture/control-flow.md).

**Q: Does the browser really hit the internet?**
Yes. It resolves `jsonplaceholder.typicode.com` via DNS and performs a live
HTTP/HTTPS GET. Under QEMU `user` networking with an `e1000` NIC (or a
USB tether), packets leave the VM through your host. See
[HTTP](../networking/http.md) / [TLS](../networking/tls.md).

**Q: Is the HTTPS implementation secure?**
No. Certificate verification is disabled (`VERIFY_NONE`) and entropy is a toy
LFSR. It is a functional demo only — do not send real secrets. See
[TLS](../networking/tls.md).

**Q: Why does my laptop show "USB TETHERING BLOCKED"?**
Your machine exposes only an xHCI controller, while the kernel's best-tested
USB path is EHCI. Use the EHCI passthrough template. See
[Troubleshooting](../reference/troubleshooting.md).

**Q: Can I add my own app / syscall / user program?**
Yes — see the [Developer Guides](../reference/dev-add-app.md),
[dev-add-syscall](../reference/dev-add-syscall.md), and
[User Programs](../userland/user-programs.md).

**Q: Why are large mallocs never freed?**
`kfree` only handles fixed-pool blocks. The upper-heap bump region (large
allocations) is intentionally non-freeable. This is fine for a short-lived
demo. See [Memory](../kernel/memory.md).

**Q: What is the `nbu os.zip` file in the repo root?**
A project archive (snapshot/backup). It is not part of the build.

**Q: Where do I see debug logs?**
Append `-serial mon:stdio` to QEMU. The network stack and mbedTLS
print to that serial console. See
[Running](../getting-started/running.md).

**Q: Which NIC does MyOS prefer?**
USB tether (if present) > Intel e1000e > Realtek RTL8139. See
[NIC Drivers](../drivers/nic-drivers.md).
