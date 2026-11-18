#!/usr/bin/env bash
set -e

# Include project headers and use compact mbedTLS config
CFLAGS="-I . -I vendor/mbedtls/include -DMBEDTLS_CONFIG_FILE=\"mbedtls/config_mini.h\" -DMBEDTLS_PLATFORM_TIME_TYPE_MACRO=long -DMBEDTLS_PLATFORM_TIME_MACRO=mbedtls_time_stub -DMBEDTLS_PLATFORM_NO_STD_FUNCTIONS -DMBEDTLS_NO_PLATFORM_ENTROPY"

# Force-include small platform header to satisfy INT_MAX, string prototypes etc
CFLAGS+=" -include vendor/mbedtls/include/mbedtls/platform_needed.h -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion"

echo "Cleaning..."
rm -rf iso myos.iso *.o kernel.bin

 # Compile all sources with the cross-compiler
i686-elf-gcc -m32 -c kernel.c        ${CFLAGS} -ffreestanding -o kernel.o
i686-elf-gcc -m32 -c graphics.c      ${CFLAGS} -ffreestanding -o graphics.o
i686-elf-gcc -m32 -c sdl2_port.c     ${CFLAGS} -ffreestanding -o sdl2_port.o
i686-elf-gcc -m32 -c scheduler.c     ${CFLAGS} -ffreestanding -o scheduler.o
i686-elf-gcc -m32 -c wm.c            ${CFLAGS} -ffreestanding -o wm.o
i686-elf-gcc -m32 -c billing.c       ${CFLAGS} -ffreestanding -o billing.o
i686-elf-gcc -m32 -c string.c        ${CFLAGS} -ffreestanding -o string.o
i686-elf-gcc -m32 -c font.c          ${CFLAGS} -ffreestanding -o font.o
i686-elf-gcc -m32 -c font_modern.c   ${CFLAGS} -ffreestanding -o font_modern.o
i686-elf-gcc -m32 -c mouse.c         ${CFLAGS} -ffreestanding -o mouse.o
# USB host and net drivers
i686-elf-gcc -m32 -c drivers/usb/usb_host.c ${CFLAGS} -ffreestanding -o usb_host.o
i686-elf-gcc -m32 -c drivers/usb/xhci.c ${CFLAGS} -ffreestanding -o xhci.o
i686-elf-gcc -m32 -c drivers/usb/usb_rndis.c ${CFLAGS} -ffreestanding -o usb_rndis.o
i686-elf-gcc -m32 -c drivers/usb/usb_ipheth.c ${CFLAGS} -ffreestanding -o usb_ipheth.o
i686-elf-gcc -m32 -c drivers/usb/usb_cdc_ecm.c ${CFLAGS} -ffreestanding -o usb_cdc_ecm.o

# New network-related modules
i686-elf-gcc -m32 -c pci.c           ${CFLAGS} -ffreestanding -o pci.o
# build e1000e driver instead of rtl8139 (driver exposes rtl8139_* API for compatibility)
i686-elf-gcc -m32 -c drivers/e1000e.c ${CFLAGS} -ffreestanding -o e1000e.o
i686-elf-gcc -m32 -c net.c           ${CFLAGS} -ffreestanding -o net.o
i686-elf-gcc -m32 -c dhcp.c          ${CFLAGS} -ffreestanding -o dhcp.o
i686-elf-gcc -m32 -c net_demo.c      ${CFLAGS} -ffreestanding -o net_demo.o
i686-elf-gcc -m32 -c kmalloc.c      ${CFLAGS} -ffreestanding -o kmalloc.o

# compile syscall and ELF loader sources so kernel can call console_puts and elf32_load_and_run
i686-elf-gcc -m32 -c syscalls.c      ${CFLAGS} -ffreestanding -o syscalls.o
i686-elf-gcc -m32 -c exec_elf.c      ${CFLAGS} -ffreestanding -o exec_elf.o

# compile optional generated userprog blob if present
EXTRA_OBJS=""
if [ -f userprog_blob.c ]; then
  i686-elf-gcc -m32 -c userprog_blob.c ${CFLAGS} -ffreestanding -o userprog_blob.o
  EXTRA_OBJS="userprog_blob.o"
fi

i686-elf-gcc -m32 -c tcp.c           ${CFLAGS} -ffreestanding -o tcp.o
i686-elf-gcc -m32 -c http.c          ${CFLAGS} -ffreestanding -o http.o
i686-elf-gcc -m32 -c dns.c           ${CFLAGS} -ffreestanding -o dns.o

# mbedTLS minimal sources to compile
MBED_SOURCES=(
  entropy.c
  ctr_drbg.c
  error.c
  aes.c
  cipher.c
  cipher_wrap.c
  gcm.c
  md.c
  sha1.c
  sha256.c
  sha512.c
  bignum.c
  ecp.c
  ecp_curves.c
  ecdh.c
  rsa.c
  rsa_internal.c
  pk.c
  pk_wrap.c
  pkparse.c
  asn1parse.c
  oid.c
  x509.c
  x509_crt.c
  ssl_tls.c
  ssl_cli.c
  ssl_ciphersuites.c
  ssl_msg.c
  ssl_ticket.c
  constant_time.c
  platform.c
  platform_util.c
  timing.c
  memory_buffer_alloc.c
  version.c
)

for src in "${MBED_SOURCES[@]}"; do
  obj=$(basename "${src}" .c).o
  echo "Compiling mbedTLS: ${src} -> ${obj}"
  i686-elf-gcc -m32 -c vendor/mbedtls/library/${src} ${CFLAGS} -ffreestanding -o ${obj}
done

# Compile platform shim (provides minimal platform/ciphersuite/printf stubs)
i686-elf-gcc -m32 -c vendor/mbedtls/platform_shim.c ${CFLAGS} -ffreestanding -o platform_shim.o

# Compile TLS glue (uses vendored headers)
i686-elf-gcc -m32 -c tls_mbedtls.c   ${CFLAGS} -ffreestanding -o tls_mbedtls.o || true

# Assemble boot code
nasm -f elf32 boot.asm -o boot.o

# Assemble IRQ stubs (use GAS via the compiler because file uses AT&T/GAS syntax)
i686-elf-gcc -m32 -c irqstubs.S -o irqstubs.o
i686-elf-gcc -m32 -c sched_switch.S -o sched_switch.o

LIBGCC=$(i686-elf-gcc -m32 -print-libgcc-file-name)
GRUB_MKRESCUE=$(command -v grub-mkrescue || command -v i686-elf-grub-mkrescue)

# Link everything into kernel.bin using compiler driver (pull in libgcc builtins)
i686-elf-gcc -m32 -nostdlib -Wl,-melf_i386 -Wl,-T,linker.ld -Wl,-z,max-page-size=0x1000 \
  boot.o kernel.o graphics.o sdl2_port.o scheduler.o sched_switch.o wm.o billing.o string.o font.o font_modern.o mouse.o \
  usb_host.o xhci.o usb_rndis.o usb_ipheth.o usb_cdc_ecm.o \
   pci.o e1000e.o net.o dhcp.o net_demo.o kmalloc.o \
   syscalls.o exec_elf.o ${EXTRA_OBJS} \
   tcp.o http.o dns.o tls_mbedtls.o platform_shim.o irqstubs.o \
   aes.o cipher.o cipher_wrap.o gcm.o entropy.o ctr_drbg.o error.o md.o sha1.o sha256.o sha512.o \
   bignum.o ecp.o ecp_curves.o ecdh.o rsa.o rsa_internal.o pk.o pk_wrap.o pkparse.o asn1parse.o oid.o x509.o x509_crt.o \
   ssl_tls.o ssl_cli.o ssl_ciphersuites.o ssl_msg.o ssl_ticket.o constant_time.o platform.o platform_util.o timing.o version.o memory_buffer_alloc.o \
   "${LIBGCC}" \
   -o kernel.bin

# Build ISO
mkdir -p iso/boot/grub
cp kernel.bin iso/boot/

cat > iso/boot/grub/grub.cfg <<EOF
set timeout=0
menuentry "MyOS" {
  multiboot2 /boot/kernel.bin
  boot
}
EOF

"${GRUB_MKRESCUE}" -o myos.iso iso
echo -e "\nBuilt myos.iso"

echo "Run with →"
echo "qemu-system-i386 -m 256 \\
  -netdev user,id=n1,hostfwd=udp::6000-:6000,hostfwd=udp::6001-:6001 \\
  -device e1000,netdev=n1 \\
  -cdrom myos.iso -usbdevice mouse"
echo
echo "USB tether passthrough template →"
echo "Find your current phone bus/device with: lsusb -t"
echo "sudo qemu-system-i386 -m 512 \\
  -cdrom myos.iso \\
  -serial mon:stdio \\
  -device usb-ehci,id=ehci \\
  -device usb-host,bus=ehci.0,hostbus=<BUS>,hostaddr=<DEV>,guest-reset=false,guest-resets-all=false,loglevel=4"
echo
echo "USB tether passthrough template (xHCI) →"
echo "Example: Bus 01 Dev 06 becomes hostbus=1,hostaddr=6"
echo "sudo qemu-system-i386 -m 512 \\
  -cdrom myos.iso \\
  -serial mon:stdio \\
  -netdev user,id=n1,hostfwd=udp::6000-:6000,hostfwd=udp::6001-:6001 \\
  -device e1000,netdev=n1 \\
  -device qemu-xhci,id=xhci \\
  -device usb-host,bus=xhci.0,hostbus=<BUS>,hostaddr=<DEV>,guest-reset=false,guest-resets-all=false,loglevel=4"

echo
echo "Recommended Wi-Fi sharing run command (no phone passthrough) →"
echo "qemu-system-i386 -m 512 \\
  -cdrom myos.iso \\
  -serial mon:stdio \\
  -netdev user,id=n1,hostfwd=udp::6000-:6000,hostfwd=udp::6001-:6001 \\
  -device e1000,netdev=n1 \\
  -device qemu-xhci,id=xhci"
