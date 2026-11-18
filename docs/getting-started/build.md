# Installation & Build

This page explains how to compile MyOS from source into a bootable ISO.

## Prerequisites

| Tool | Purpose | Check |
|------|---------|-------|
| `i686-elf-gcc` | Cross-compiler for the freestanding kernel | `i686-elf-gcc --version` |
| `i686-elf-ld` | Linker (invoked via the compiler driver) | `i686-elf-ld --version` |
| `nasm` | Assembles `boot.asm` and `irqstubs.S` | `nasm -v` |
| `grub-mkrescue` | Builds the bootable ISO | `grub-mkrescue --version` |
| `xorriso` | Backend for grub-mkrescue | `xorriso --version` |
| `xxd` | Generates the embedded user-program blob | `xxd -v` |

The cross toolchain is expected at `/home/tupenshil/opt/cross/` (libgcc path is
hard-coded in `build.sh:107`). Adjust that path to match your install.

## The build script

`build.sh` performs the full pipeline:

1. **Clean** previous artifacts (`rm -rf iso myos.iso *.o kernel.bin`).
2. **Compile** every kernel source with:
   ```
   i686-elf-gcc -m32 -c <src> -I . -I vendor/mbedtls/include \
     -DMBEDTLS_CONFIG_FILE=\"mbedtls/config_mini.h\" -ffreestanding -o <obj>
   ```
3. **Compile the vendored mbedTLS** minimal sources (entropy, ciphers,
   hashes, ECP, ECDH, SSL) plus a `platform_shim.c` that satisfies the
   freestanding platform hooks.
4. **Assemble** `boot.asm` (NASM, `elf32`) and `irqstubs.S` (GCC/AT&T).
5. **Link** everything with `linker.ld` (load base 1 MiB) and pull in
   `libgcc.a` for builtin helpers.
6. **Assemble the ISO**: copy `kernel.bin` to `iso/boot/`, write a
   `grub.cfg` that multiboots it, then `grub-mkrescue -o myos.iso iso`.

## Running the build

```bash
cd /home/tupenshil/Desktop/os/newos
chmod +x build.sh
./build.sh
```

On success you will see:

```
Built myos.iso
Run with →
qemu-system-i386 -m 256 \
  -netdev user,id=n1,hostfwd=udp::6000-:6000,hostfwd=udp::6001-:6001 \
  -device e1000,netdev=n1 \
  -cdrom myos.iso -usbdevice mouse
```

## Building the optional user program

The in-kernel ELF loader can run an embedded "hello" program. To regenerate
it:

```bash
make -f Makefile.inc userprog        # produces out/hello.so (32-bit PIE/ET_DYN)
make -f Makefile.inc embed-userprog   # xxd's it into userprog_blob.c + userprog_hello.h
```

Then rebuild. The **Run embedded hello** menu entry will load and run it. If
`userprog_blob.c` is absent, the menu entry prints a hint instead.

## Common build issues

- **`i686-elf-gcc: command not found`** — install/activate the cross
  toolchain and ensure it is on `PATH`.
- **`cannot find -lgcc` / libgcc path** — update the `libgcc.a` path at
  `build.sh:107` to your toolchain's `lib/gcc/i686-elf/<ver>/` directory.
- **`xorriso` missing** — `grub-mkrescue` needs it; install it
  (e.g. `apt install xorriso`).
- **mbedTLS compile errors** — the minimal config `mbedtls/config_mini.h`
  and `platform_shim.c` are tightly coupled; do not swap in a different
  mbedTLS version without updating the shim.

Next: [Running in QEMU](running.md).
