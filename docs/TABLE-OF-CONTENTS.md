# Table of Contents

A full map of the MyOS documentation set (53 pages). Use it to jump
anywhere. Start at [README](../README.md) or
[System Overview](../architecture/overview.md).

## Getting started
- [Installation & Build](../getting-started/build.md)
- [Running in QEMU](../getting-started/running.md)

## Architecture
- [System Overview](../architecture/overview.md)
- [Boot Flow](../architecture/boot-flow.md)
- [Control Flow](../architecture/control-flow.md)
- [Memory Model & Address Space](../architecture/memory-model.md)

## Kernel subsystems
- [Memory (kmalloc)](../kernel/memory.md)
- [Graphics](../kernel/graphics.md)
- [Input (mouse/keyboard)](../kernel/input.md)

## Networking
- [Network Core](../networking/network-core.md)
- [DHCP](../networking/dhcp.md)
- [DNS](../networking/dns.md)
- [TCP](../networking/tcp.md)
- [HTTP](../networking/http.md)
- [TLS / HTTPS](../networking/tls.md)

## Drivers & hardware
- [USB Host Stack](../drivers/usb-host.md)
- [USB Controllers Deep-Dive](../drivers/usb-deep-dive.md)
- [NIC Drivers](../drivers/nic-drivers.md)
- [NIC Drivers Deep-Dive](../drivers/nic-deep-dive.md)
- [PCI Bus](../drivers/pci.md)

## Userland
- [Syscall Gate](../userland/syscalls.md)
- [ELF Loader](../userland/elf-loader.md)
- [User Programs](../userland/user-programs.md)

## Applications
- [Desktop & Shell](../apps/desktop.md)
- [App Catalog](../apps/app-catalog.md)

## Reference
- [API: Networking (net.h)](../reference/api-net.md)
- [API: Graphics](../reference/api-graphics.md)
- [API: JSON (JSMN)](../reference/api-json.md)
- [API: Stdlib/Stubs](../reference/api-stdlib.md)
- [Boot & Linker](../reference/boot-linker.md)
- [Input & Driver Registration](../reference/input-drivers.md)
- [USB Tethering](../reference/usb-tether.md)
- [DHCP Options & Wire](../reference/dhcp-wire.md)
- [TCP State Machine](../reference/tcp-states.md)
- [DNS Wire](../reference/dns-wire.md)
- [HTTP/TLS Wire](../reference/http-tls-wire.md)
- [mbedTLS Integration](../reference/mbedtls-integration.md)
- [User Program Build](../reference/userprog-build.md)
- [Graphics: Rendering](../reference/graphics-rendering.md)
- [Welcome Screen Status Codes](../reference/welcome-status.md)
- [Project File Index](../reference/file-index.md)
- [Developer: Add an App](../reference/dev-add-app.md)
- [Developer: Add a Syscall](../reference/dev-add-syscall.md)
- [Build Internals](../reference/build-internals.md)
- [Coding Style & Conventions](../reference/coding-style.md)
- [Glossary](../reference/glossary.md)
- [Troubleshooting](../reference/troubleshooting.md)
- [FAQ](../reference/faq.md)
- [Roadmap & Limitations](../reference/roadmap.md)

## How to navigate

- **New here?** README → System Overview → Build → Running.
- **Wiring a NIC/USB device?** Drivers section + the two Deep-Dive pages.
- **Debugging networking?** Networking section + the Wire reference pages.
- **Extending the OS?** Developer guides in Reference.
- **Stuck?** Troubleshooting + FAQ + Glossary.
