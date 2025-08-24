#!/bin/bash
set -euo pipefail

# Build folder
BUILD=build
mkdir -p "$BUILD"

# Files
BOOTLOADER=bootloader.asm
BOOT_BIN="$BUILD/bootloader.bin"
IMG="$BUILD/os.img"

KDIR=kernel
KERNEL_C="$KDIR/kernel.c"
KERNEL_KBD_C="$KDIR/keyboard.c"
KERNEL_CONS_C="$KDIR/console.c"
KERNEL_MEM_C="$KDIR/memory.c"
KERNEL_VFS_C="$KDIR/vfs.c"
KERNEL_RAMFS_C="$KDIR/ramfs.c"
KERNEL_INITRD_C="$KDIR/initrd.c"
KERNEL_ATA_C="$KDIR/ata.c"
KERNEL_RENDER_C="$KDIR/render.c"
KERNEL_WINDOW_C="$KDIR/window.c"
KERNEL_FB_C="$KDIR/fb.c"
KERNEL_GUI_C="$KDIR/gui.c"
KERNEL_SERIAL_C="$KDIR/serial.c"
KERNEL_ENTRY_ASM="$KDIR/kernel_entry.asm"
LINKER_SCRIPT="$KDIR/kernel.ld"
KOBJ_C="$BUILD/kernel.o"
KOBJ_KBD="$BUILD/keyboard.o"
KOBJ_CONS="$BUILD/console.o"
KOBJ_MEM="$BUILD/memory.o"
KOBJ_VFS="$BUILD/vfs.o"
KOBJ_RAMFS="$BUILD/ramfs.o"
KOBJ_INITRD="$BUILD/initrd.o"
KOBJ_ATA="$BUILD/ata.o"
KOBJ_RENDER="$BUILD/render.o"
KOBJ_WINDOW="$BUILD/window.o"
KOBJ_FB="$BUILD/fb.o"
KOBJ_GUI="$BUILD/gui.o"
KOBJ_SERIAL="$BUILD/serial.o"
KOBJ_ENTRY="$BUILD/kernel_entry.o"
KELF="$BUILD/kernel.elf"
KBIN="$BUILD/kernel.bin"

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

CFLAGS_COMMON="-m32 -ffreestanding -fno-pic -fno-builtin -fno-stack-protector -nostdlib $CDEFS"

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

echo "Compiling ATA driver..."
gcc $CFLAGS_COMMON -c "$KERNEL_ATA_C" -o "$KOBJ_ATA"

echo "Compiling renderer..."
gcc $CFLAGS_COMMON -c "$KERNEL_RENDER_C" -o "$KOBJ_RENDER"

echo "Compiling window..."
gcc $CFLAGS_COMMON -c "$KERNEL_WINDOW_C" -o "$KOBJ_WINDOW"

echo "Compiling framebuffer..."
gcc $CFLAGS_COMMON -c "$KERNEL_FB_C" -o "$KOBJ_FB"

echo "Compiling gui..."
gcc $CFLAGS_COMMON -c "$KERNEL_GUI_C" -o "$KOBJ_GUI"

echo "Compiling serial..."
gcc $CFLAGS_COMMON -c "$KERNEL_SERIAL_C" -o "$KOBJ_SERIAL"

echo "Assembling kernel entry..."
nasm -f elf32 "$KERNEL_ENTRY_ASM" -o "$KOBJ_ENTRY"

echo "Linking kernel (ELF via $LINKER_SCRIPT)..."
ld -m elf_i386 -T "$LINKER_SCRIPT" -nostdlib -o "$KELF" \
  "$KOBJ_ENTRY" "$KOBJ_C" "$KOBJ_KBD" "$KOBJ_CONS" "$KOBJ_MEM" "$KOBJ_VFS" "$KOBJ_RAMFS" "$KOBJ_INITRD" "$KOBJ_ATA" "$KOBJ_RENDER" "$KOBJ_WINDOW" "$KOBJ_FB" "$KOBJ_GUI" "$KOBJ_SERIAL"

echo "Converting kernel to flat binary..."
objcopy -O binary "$KELF" "$KBIN"

# Calculate sectors for loader (ceil(size/512))
KBIN_SIZE=$(stat -c%s "$KBIN")
SECTORS=$(( (KBIN_SIZE + 511) / 512 ))
echo "Kernel size: $KBIN_SIZE bytes -> $SECTORS sectors"

# Pad kernel to full sectors so image data matches sectors read
PAD=$(( SECTORS * 512 - KBIN_SIZE ))
if (( PAD > 0 )); then
  echo "Padding kernel by $PAD bytes to align to $SECTORS*512"
  dd if=/dev/zero bs=1 count=$PAD status=none >> "$KBIN"
fi

# Recompute size sanity
KBIN_SIZE2=$(stat -c%s "$KBIN")
if (( KBIN_SIZE2 != SECTORS * 512 )); then
  echo "Error: padded kernel size ($KBIN_SIZE2) != SECTORS*512 ($((SECTORS*512)))" >&2
  exit 1
fi

# Assemble bootloader with KERNEL_SECTORS macro
echo "Assembling bootloader with KERNEL_SECTORS=$SECTORS..."
nasm -f bin -DKERNEL_SECTORS=$SECTORS "$BOOTLOADER" -o "$BOOT_BIN"

# Verify bootloader size is exactly 512 and signature 0x55AA
BOOT_SIZE=$(stat -c%s "$BOOT_BIN")
if (( BOOT_SIZE != 512 )); then
  echo "Error: bootloader size is $BOOT_SIZE, expected 512" >&2
  exit 1
fi
SIG=$(hexdump -v -e '1/1 "%02x"' -s 510 -n 2 "$BOOT_BIN")
if [[ "$SIG" != "55aa" && "$SIG" != "55AA" ]]; then
  echo "Error: bootloader missing 0x55AA signature (got $SIG)" >&2
  exit 1
fi

# Always create a floppy boot image for reliable boot
echo "Creating floppy image..."
IMG="$BUILD/os.img"
dd if=/dev/zero of="$IMG" bs=512 count=2880 status=none
echo "Writing boot sector..."
dd if="$BOOT_BIN" of="$IMG" conv=notrunc status=none
echo "Writing kernel at LBA 1..$SECTORS..."
dd if="$KBIN" of="$IMG" bs=512 seek=1 conv=notrunc status=none

echo "Done. Floppy Image: $IMG"

# Optionally also create an HDD image for disk management tests
if (( MAKE_HDD_IMAGE )); then
  DISK_SIZE_MB=${DISK_SIZE_MB:-32}
  SECTORS_TOTAL=$(( DISK_SIZE_MB * 1024 * 1024 / 512 ))
  HDD_IMG="$BUILD/disk.img"
  echo "Creating $DISK_SIZE_MB MiB HDD image ($SECTORS_TOTAL sectors) at $HDD_IMG..."
  dd if=/dev/zero of="$HDD_IMG" bs=512 count=$SECTORS_TOTAL status=none
  echo "Writing MBR with one partition..."
  nasm -f bin -DPT_LBA_START=${PART_START:-2048} -DPT_LBA_COUNT=$(( SECTORS_TOTAL - ${PART_START:-2048} )) mbr_pt.asm -o "$BUILD/mbr.bin"
  dd if="$BUILD/mbr.bin" of="$HDD_IMG" conv=notrunc status=none
  echo "Writing VBR (bootloader) at LBA ${PART_START:-2048}..."
  dd if="$BOOT_BIN" of="$HDD_IMG" bs=512 seek=${PART_START:-2048} conv=notrunc status=none
  echo "Writing kernel right after VBR..."
  dd if="$KBIN" of="$HDD_IMG" bs=512 seek=$(( ${PART_START:-2048} + 1 )) conv=notrunc status=none
  echo "Done. HDD Image: $HDD_IMG"
  echo "Run: qemu-system-i386 -m 64 -serial stdio -boot a -drive file=$IMG,if=floppy,format=raw -drive id=hdd,file=$HDD_IMG,if=none,format=raw -device ide-hd,drive=hdd,bus=ide.0"
else
  echo "Run: qemu-system-i386 -m 64 -serial stdio -boot a -drive file=$IMG,if=floppy,format=raw"
fi
