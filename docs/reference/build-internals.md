# Build Internals

What `build.sh` actually does, step by step, so you can modify the
pipeline or debug a failed build.

## Toolchain flags

Every compile uses:

```
-m32 -ffreestanding
-I . -I vendor/mbedtls/include
-DMBEDTLS_CONFIG_FILE=\"mbedtls/config_mini.h\"
-DMBEDTLS_PLATFORM_TIME_TYPE_MACRO=long
-DMBEDTLS_PLATFORM_TIME_MACRO=mbedtls_time_stub
-DMBEDTLS_PLATFORM_NO_STD_FUNCTIONS
-DMBEDTLS_NO_PLATFORM_ENTROPY
-include vendor/mbedtls/include/mbedtls/platform_needed.h
```

- `-m32` → 32-bit code. `-ffreestanding` → no hosted libc.
- The mbedTLS macros configure the vendored TLS lib for freestanding use.
- `platform_needed.h` force-includes `INT_MAX`, string prototypes, etc.

## Compile order

1. Kernel C: `kernel`, `graphics`, `string`, `font`, `mouse`.
2. USB host + tether: `usb_host`, `xhci`, `usb_rndis`, `usb_ipheth`,
   `usb_cdc_ecm`.
3. NIC + PCI: `pci`, `drivers/e1000e.c` (built as `e1000e.o`),
   `net`, `dhcp`, `net_demo`, `kmalloc`.
4. Userland glue: `syscalls`, `exec_elf`, optional `userprog_blob`.
5. Network L4: `tcp`, `http`, `dns`.
6. mbedTLS: ~24 `vendor/mbedtls/library/*.c` → objects, plus
   `platform_shim.c` and `tls_mbedtls.c`.
7. Assembly: `boot.asm` via NASM; `irqstubs.S` via GCC.

## Link step

```bash
i686-elf-gcc -m32 -nostdlib \
  -Wl,-melf_i386 -Wl,-T,linker.ld -Wl,-z,max-page-size=0x1000 \
  boot.o kernel.o graphics.o ... tls_mbedtls.o platform_shim.o irqstubs.o \
  aes.o cipher.o ... version.o memory_buffer_alloc.o \
  /home/tupenshil/opt/cross/lib/gcc/i686-elf/13.1.0/libgcc.a \
  -o kernel.bin
```

- `-nostdlib` → no libc/crt; `_start` is the entry (`linker.ld`).
- `libgcc.a` supplies builtins (`__divdi3`, etc.).
- The `*.o` mbedTLS objects are pre-built/checked-in; if you change
  mbedTLS, rebuild them or delete and let the script recompile.

## ISO assembly

```bash
mkdir -p iso/boot/grub
cp kernel.bin iso/boot/
cat > iso/boot/grub/grub.cfg <<EOF
set timeout=0
menuentry "MyOS" { multiboot2 /boot/kernel.bin; boot }
EOF
grub-mkrescue -o myos.iso iso
```

The script ends by printing recommended QEMU invocations
(see [Running](../getting-started/running.md)).

## `Makefile.inc` (opt-in)

`make -f Makefile.inc userprog` / `embed-userprog` build and embed the
sample user program. `build.sh` independently picks up `userprog_blob.c`
if present (via its own wildcard check), so you can ship a kernel with the
"Run embedded hello" feature enabled.

## Modifying the build

- **Add a source:** append a `i686-elf-gcc -m32 -c new.c ...` line
  and add `new.o` to the link command.
- **Change mbedTLS config:** edit `vendor/mbedtls/include/mbedtls/
  config_mini.h` and the shim; keep them consistent.
- **Change load address:** edit `linker.ld` (`. = 1M;`) — must stay above
  the BIOS/GRUB region and below your heap (`0x01000000`).
