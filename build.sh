#!/bin/bash
set -euo pipefail

# Build folder
BUILD=build
mkdir -p "$BUILD"

# Files
BOOT_DIR=boot
DRIVERS_DIR=drivers
FS_DIR=fs
KDIR=kernel
COMMON_DIR=common

UEFI_DIR=boot
UEFI_BIN="$BUILD/BOOTX64.EFI"
UEFI_OBJ="$BUILD/uefi_main.obj"
ESP_IMG="$BUILD/esp.img"
ISO_DIR="$BUILD/iso"
ISO_PATH="$BUILD/uefi.iso"

KERNEL_C="$KDIR/kernel.c"
KERNEL_KBD_C="$DRIVERS_DIR/keyboard.c"
KERNEL_CONS_C="$DRIVERS_DIR/console.c"
KERNEL_MEM_C="$KDIR/memory.c"
KERNEL_TESTS_C="$KDIR/tests.c"
KERNEL_RELOC_C="$KDIR/relocation.c"
KERNEL_VFS_C="$FS_DIR/vfs.c"
KERNEL_RAMFS_C="$FS_DIR/ramfs.c"
KERNEL_INITRD_C="$FS_DIR/initrd.c"
KERNEL_ATA_C="$DRIVERS_DIR/ata.c"
KERNEL_SERIAL_C="$DRIVERS_DIR/serial.c"
KERNEL_ENTRY_C="$BOOT_DIR/uefi_main.c"
LINKER_SCRIPT="$KDIR/kernel.ld"

KOBJ_TESTS="$BUILD/tests.o"
KOBJ_RELOC="$BUILD/relocation.o"

KOBJ_C="$BUILD/kernel.o"
KOBJ_KBD="$BUILD/keyboard.o"
KOBJ_CONS="$BUILD/console.o"
KOBJ_MEM="$BUILD/memory.o"
KOBJ_TESTS="$BUILD/tests.o"
KOBJ_VFS="$BUILD/vfs.o"
KOBJ_RAMFS="$BUILD/ramfs.o"
KOBJ_INITRD="$BUILD/initrd.o"
KOBJ_ATA="$BUILD/ata.o"
KOBJ_SERIAL="$BUILD/serial.o"
KOBJ_IDT="$BUILD/idt.o"
KOBJ_IDT_STUBS="$BUILD/idt_stubs.o"
KOBJ_TIMER="$BUILD/timer.o"
KOBJ_ENTRY="$UEFI_OBJ"
KOBJ_KERNEL_ENTRY="$BUILD/kernel_entry.o"

KELF="$BUILD/kernel.elf"

# Target: superfloppy (default) or hdd
TARGET=${BUILD_TARGET:-hdd}

# Define disk macros for kernel based on target
if [[ "$TARGET" == "floppy" ]]; then
  DISK_SECTORS=2880
  DISK_SECTOR_SIZE=512
  CDEFS="-DDISK_MODE_FLOPPY=1 -DDISK_SECTORS=$DISK_SECTORS -DDISK_SECTOR_SIZE=$DISK_SECTOR_SIZE"
  MAKE_HDD_IMAGE=0
else
  DISK_SIZE_MB=${DISK_SIZE_MB:-32}
  SECTORS_TOTAL=$(( DISK_SIZE_MB * 1024 * 1024 / 512 ))
  DISK_SECTORS=$SECTORS_TOTAL
  DISK_SECTOR_SIZE=512
  PART_START=${PT_LBA_START:-2048}
  PART_COUNT=$(( SECTORS_TOTAL - PART_START ))
  CDEFS="-DDISK_MODE_HDD=1 -DDISK_SECTORS=$DISK_SECTORS -DDISK_SECTOR_SIZE=$DISK_SECTOR_SIZE -DPT_LBA_START=$PART_START -DPT_LBA_COUNT=$PART_COUNT -DDISK_SIZE_MB=$DISK_SIZE_MB"
  MAKE_HDD_IMAGE=1
fi

INCLUDES="-I$KDIR -I$DRIVERS_DIR -I$FS_DIR -I$COMMON_DIR"
CFLAGS_COMMON="-m64 -ffreestanding -fno-pic -fno-pie -fno-builtin -fno-stack-protector -mno-red-zone -nostdlib $CDEFS $INCLUDES"
UEFI_CFLAGS="-m64 -ffreestanding -fno-pic -fno-pie -fno-builtin -fno-stack-protector -mno-red-zone -nostdlib $INCLUDES"

# Assemble bootloader later, after we know kernel sectors

echo "Compiling kernel C..."
gcc $CFLAGS_COMMON -c "$KERNEL_C" -o "$KOBJ_C"

echo "Compiling keyboard driver..."
gcc $CFLAGS_COMMON -c "$KERNEL_KBD_C" -o "$KOBJ_KBD"

echo "Compiling console..."
gcc $CFLAGS_COMMON -c "$KERNEL_CONS_C" -o "$KOBJ_CONS"

echo "Compiling memory manager..."
gcc $CFLAGS_COMMON -c "$KERNEL_MEM_C" -o "$KOBJ_MEM"

echo "Compiling VFS/RAMFS/initrd..."
gcc $CFLAGS_COMMON -c "$KERNEL_VFS_C" -o "$KOBJ_VFS"
gcc $CFLAGS_COMMON -c "$KERNEL_RAMFS_C" -o "$KOBJ_RAMFS"
gcc $CFLAGS_COMMON -c "$KERNEL_INITRD_C" -o "$KOBJ_INITRD"

echo "Compiling tests..."
gcc $CFLAGS_COMMON -c "$KERNEL_TESTS_C" -o "$KOBJ_TESTS"

echo "Compiling relocation helper..."
gcc $CFLAGS_COMMON -c "$KERNEL_RELOC_C" -o "$KOBJ_RELOC"

echo "Compiling ATA driver..."
gcc $CFLAGS_COMMON -c "$KERNEL_ATA_C" -o "$KOBJ_ATA"

echo "Compiling serial..."
gcc $CFLAGS_COMMON -c "$KERNEL_SERIAL_C" -o "$KOBJ_SERIAL"

echo "Compiling IDT..."
gcc $CFLAGS_COMMON -c "$KDIR/idt.c" -o "$KOBJ_IDT"

echo "Assembling IDT stubs..."
nasm -f elf64 "$KDIR/idt_stubs.asm" -o "$KOBJ_IDT_STUBS"

echo "Assembling kernel entry..."
nasm -f elf64 "$KDIR/kernel_entry.asm" -o "$KOBJ_KERNEL_ENTRY"

echo "Compiling timer..."
gcc $CFLAGS_COMMON -c "$KDIR/timer.c" -o "$KOBJ_TIMER"

echo "Compiling UEFI entry..."
clang --target=x86_64-pc-windows-gnu $UEFI_CFLAGS -c "$KERNEL_ENTRY_C" -o "$KOBJ_ENTRY"

echo "Linking kernel ELF ($LINKER_SCRIPT)..."
ld -m elf_x86_64 -T "$LINKER_SCRIPT" -nostdlib -o "$KELF" \
  "$KOBJ_KERNEL_ENTRY" "$KOBJ_C" "$KOBJ_KBD" "$KOBJ_CONS" "$KOBJ_MEM" "$KOBJ_RELOC" "$KOBJ_TESTS" "$KOBJ_VFS" "$KOBJ_RAMFS" "$KOBJ_INITRD" "$KOBJ_ATA" "$KOBJ_SERIAL" "$KOBJ_IDT" "$KOBJ_IDT_STUBS" "$KOBJ_TIMER"

echo "Linking UEFI loader EFI application..."
lld-link /nologo /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:x64 /base:0x400000 /fixed /out:"$UEFI_BIN" "$KOBJ_ENTRY"

# Create simple EFI ISO with BOOTX64.EFI
if command -v genisoimage >/dev/null 2>&1; then
  echo "Packing EFI ISO..."
  rm -rf "$ISO_DIR"
  mkdir -p "$ISO_DIR/EFI/BOOT"
  cp -f "$UEFI_BIN" "$ISO_DIR/EFI/BOOT/BOOTX64.EFI" || true
  cp -f "$KELF" "$ISO_DIR/EFI/BOOT/KERNEL.ELF"
  genisoimage -o "$ISO_PATH" -V FOXOS -J -r -eltorito-alt-boot -e EFI/BOOT/BOOTX64.EFI -no-emul-boot "$ISO_DIR" >/dev/null 2>&1 || true
  echo "Created EFI ISO: $ISO_PATH"
elif command -v xorriso >/dev/null 2>&1 && command -v parted >/dev/null 2>&1 && command -v mkfs.vfat >/dev/null 2>&1 && command -v mcopy >/dev/null 2>&1 && command -v mmd >/dev/null 2>&1; then
  echo "Packing EFI disk image with GPT ESP..."
  rm -rf "$ISO_DIR"
  mkdir -p "$ISO_DIR"
  rm -f "$ESP_IMG"
  dd if=/dev/zero of="$ESP_IMG" bs=1M count=64 >/dev/null 2>&1
  parted -s "$ESP_IMG" mklabel gpt
  parted -s "$ESP_IMG" mkpart ESP fat32 1MiB 100%
  parted -s "$ESP_IMG" set 1 esp on
  mkfs.vfat -F 32 -n FOXOS --offset=2048 "$ESP_IMG"
  mmd -i "$ESP_IMG@@1048576" ::/EFI
  mmd -i "$ESP_IMG@@1048576" ::/EFI/BOOT
  mcopy -i "$ESP_IMG@@1048576" "$UEFI_BIN" ::/EFI/BOOT/BOOTX64.EFI
  mcopy -i "$ESP_IMG@@1048576" "$KELF" ::/EFI/BOOT/KERNEL.ELF
  cp -f "$ESP_IMG" "$ISO_DIR/esp.img"
  xorriso -as mkisofs -o "$ISO_PATH" -V FOXOS -J -R -eltorito-alt-boot -e esp.img -no-emul-boot "$ISO_DIR" >/dev/null 2>&1
  echo "Created EFI ISO: $ISO_PATH"
else
  echo "Warning: no complete ISO toolchain found; EFI ISO not created. BOOTX64.EFI available at $UEFI_BIN"
fi

QEMU_CODE=""
for candidate in /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/edk2/x64/OVMF_CODE.secboot.4m.fd; do
  if [[ -f "$candidate" ]]; then
    QEMU_CODE="$candidate"
    break
  fi
done

QEMU_VARS=""
for candidate in /usr/share/OVMF/OVMF_VARS.fd /usr/share/edk2/x64/OVMF_VARS.4m.fd; do
  if [[ -f "$candidate" ]]; then
    QEMU_VARS="$candidate"
    break
  fi
done

if [[ -n "$QEMU_CODE" ]]; then
  if [[ -n "$QEMU_VARS" ]]; then
    cp -f "$QEMU_VARS" "$BUILD/OVMF_VARS.fd"
  fi
  HUMAN_QEMU_CMD="qemu-system-x86_64 -m 512 -serial stdio -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE"
  if [[ -n "$QEMU_VARS" ]]; then
    echo "Run in QEMU:"
    HUMAN_QEMU_CMD="$HUMAN_QEMU_CMD -drive if=pflash,format=raw,file=$BUILD/OVMF_VARS.fd"
  else
    echo "Run in QEMU:"
  fi
  HUMAN_QEMU_CMD="$HUMAN_QEMU_CMD -drive if=ide,format=raw,file=$ESP_IMG -no-reboot"
  echo "$HUMAN_QEMU_CMD"
  echo "Headless verifier:"
  if [[ -n "$QEMU_VARS" ]]; then
    echo "qemu-system-x86_64 -m 512 -serial stdio -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE -drive if=pflash,format=raw,file=build/OVMF_VARS.fd -drive if=ide,format=raw,file=$ESP_IMG -display none -monitor unix:build/qemu-monitor.sock,server,nowait -no-reboot"
  else
    echo "qemu-system-x86_64 -m 512 -serial stdio -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE -drive if=ide,format=raw,file=$ESP_IMG -display none -monitor unix:build/qemu-monitor.sock,server,nowait -no-reboot"
  fi
else
  echo "Run in QEMU:"
  echo "qemu-system-x86_64 -m 512 -serial stdio -drive if=ide,format=raw,file=$ESP_IMG -no-reboot"
fi

echo "Done. UEFI binary: $UEFI_BIN"
