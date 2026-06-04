#!/bin/bash
set -euo pipefail
export PATH="$HOME/.cargo/bin:$PATH"

# Build folder
BUILD=build
mkdir -p "$BUILD"

# Files
BOOT_DIR=boot
DRIVERS_DIR=drivers
FS_DIR=fs
KDIR=kernel
COMMON_DIR=common
RUST_DIR=kernel_rs
RUST_TARGET_BARE=x86_64-unknown-none
RUST_TARGET_FALLBACK=x86_64-unknown-linux-gnu
RUST_CARGO="$HOME/.cargo/bin/cargo"
if [[ ! -x "$RUST_CARGO" ]]; then
  RUST_CARGO="cargo"
fi

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
KERNEL_RELOCATOR_C="$KDIR/relocator.c"
KERNEL_QUIESCE_C="$KDIR/quiesce.c"
KERNEL_VFS_C="$FS_DIR/vfs.c"
KERNEL_RAMFS_C="$FS_DIR/ramfs.c"
KERNEL_INITRD_C="$FS_DIR/initrd.c"
KERNEL_FAT32_C="$FS_DIR/fat32.c"
KERNEL_BIO_C="$FS_DIR/bio.c"
KERNEL_FOXFS_C="$FS_DIR/foxfs.c"
KERNEL_ATA_C="$DRIVERS_DIR/ata.c"
KERNEL_SERIAL_C="$DRIVERS_DIR/serial.c"
KERNEL_SCHED_C="$KDIR/sched.c"
KERNEL_SMP_C="$KDIR/smp.c"
KERNEL_PERCPU_C="$KDIR/percpu.c"
KERNEL_APIC_C="$KDIR/apic.c"
KERNEL_IO_WAIT_C="$KDIR/io_wait.c"
KERNEL_GDT_C="$KDIR/gdt.c"
KERNEL_SYSCALL_C="$KDIR/syscall.c"
KERNEL_PROCESS_C="$KDIR/process.c"
KERNEL_RUST_SHIMS_C="$KDIR/rust_shims.c"
KERNEL_DEVFS_C="$FS_DIR/devfs.c"
KERNEL_PARTITION_C="$FS_DIR/partition.c"
KERNEL_GPU_C="$DRIVERS_DIR/gpu.c"
KERNEL_MOUSE_C="$DRIVERS_DIR/mouse.c"
KERNEL_LOADER_C="$KDIR/loader.c"
KERNEL_LIB_C="$COMMON_DIR/lib.c"
KERNEL_SYSCALL_ASM="$KDIR/syscall.asm"
KERNEL_ENTRY_C="$BOOT_DIR/uefi_main.c"
LINKER_SCRIPT="$KDIR/kernel.ld"
RUST_TARGET_USED="$RUST_TARGET_BARE"
RUST_LIB="$RUST_DIR/target/$RUST_TARGET_USED/release/libfoxos_rs.a"

KOBJ_TESTS="$BUILD/tests.o"
KOBJ_RELOC="$BUILD/relocation.o"
KOBJ_RELOCATOR="$BUILD/relocator.o"
KOBJ_QUIESCE="$BUILD/quiesce.o"

KOBJ_C="$BUILD/kernel.o"
KOBJ_KBD="$BUILD/keyboard.o"
KOBJ_CONS="$BUILD/console.o"
KOBJ_MEM="$BUILD/memory.o"
KOBJ_TESTS="$BUILD/tests.o"
KOBJ_VFS="$BUILD/vfs.o"
KOBJ_RAMFS="$BUILD/ramfs.o"
KOBJ_INITRD="$BUILD/initrd.o"
KOBJ_FAT32="$BUILD/fat32.o"
KOBJ_BIO="$BUILD/bio.o"
KOBJ_FOXFS="$BUILD/foxfs.o"
KOBJ_ATA="$BUILD/ata.o"
KOBJ_SERIAL="$BUILD/serial.o"
KOBJ_SCHED="$BUILD/sched.o"
KOBJ_SMP="$BUILD/smp.o"
KOBJ_PERCPU="$BUILD/percpu.o"
KOBJ_APIC="$BUILD/apic.o"
KOBJ_IO_WAIT="$BUILD/io_wait.o"
KOBJ_GDT="$BUILD/gdt.o"
KOBJ_SYSCALL_C="$BUILD/syscall_c.o"
KOBJ_PROCESS="$BUILD/process.o"
KOBJ_RUST_SHIMS="$BUILD/rust_shims.o"
KOBJ_DEVFS="$BUILD/devfs.o"
KOBJ_PARTITION="$BUILD/partition.o"
KOBJ_GPU="$BUILD/gpu.o"
KOBJ_MOUSE="$BUILD/mouse.o"
KOBJ_LOADER="$BUILD/loader.o"
KOBJ_LIB="$BUILD/lib.o"
KOBJ_SYSCALL_ASM="$BUILD/syscall_asm.o"
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
KERNEL_TARGET="x86_64-unknown-none-elf"
CFLAGS_COMMON="-target $KERNEL_TARGET -m64 -ffreestanding -fno-pic -fno-pie -fno-builtin -fno-stack-protector -mno-red-zone -nostdlib $CDEFS $INCLUDES"
CFLAGS_CORE="$CFLAGS_COMMON -flto=thin -O3 -mcmodel=kernel"
CFLAGS_TSS="-target $KERNEL_TARGET -m64 -ffreestanding -fno-pic -fno-pie -fno-builtin -fno-stack-protector -mno-red-zone -nostdlib $INCLUDES -O3 -mcmodel=large"
UEFI_CFLAGS="-m64 -ffreestanding -fno-pic -fno-pie -fno-builtin -fno-stack-protector -mno-red-zone -nostdlib $INCLUDES"

# Assemble bootloader later, after we know kernel sectors

echo "Assembling AP boot trampoline..."
nasm -f bin "$BOOT_DIR/ap_trampoline.asm" -o "$BUILD/ap_trampoline.bin"
python3 -c "import sys; data = open('$BUILD/ap_trampoline.bin', 'rb').read(); print('unsigned char ap_trampoline_code[] = { ' + ', '.join(hex(b) for b in data) + ' };\nunsigned int ap_trampoline_len = ' + str(len(data)) + ';')" > "$BUILD/ap_trampoline.h"

echo "Compiling kernel C..."
clang $CFLAGS_CORE -c "$KERNEL_C" -o "$KOBJ_C"

echo "Compiling keyboard driver..."
clang $CFLAGS_CORE -c "$KERNEL_KBD_C" -o "$KOBJ_KBD"

echo "Compiling console..."
clang $CFLAGS_CORE -c "$KERNEL_CONS_C" -o "$KOBJ_CONS"

echo "Compiling memory manager..."
clang $CFLAGS_CORE -c "$KERNEL_MEM_C" -o "$KOBJ_MEM"

echo "Compiling VFS/RAMFS/initrd..."
clang $CFLAGS_CORE -c "$KERNEL_VFS_C" -o "$KOBJ_VFS"
clang $CFLAGS_CORE -c "$KERNEL_RAMFS_C" -o "$KOBJ_RAMFS"
clang $CFLAGS_CORE -c "$KERNEL_INITRD_C" -o "$KOBJ_INITRD"

echo "Compiling FAT32 filesystem..."
clang $CFLAGS_CORE -c "$KERNEL_FAT32_C" -o "$KOBJ_FAT32"

echo "Compiling block I/O layer..."
clang $CFLAGS_CORE -c "$KERNEL_BIO_C" -o "$KOBJ_BIO"
clang $CFLAGS_CORE -c "$KERNEL_FOXFS_C" -o "$KOBJ_FOXFS"

echo "Compiling tests..."
clang $CFLAGS_CORE -c "$KERNEL_TESTS_C" -o "$KOBJ_TESTS"

echo "Compiling relocation helper..."
clang $CFLAGS_CORE -c "$KERNEL_RELOC_C" -o "$KOBJ_RELOC"

echo "Compiling relocator daemon..."
clang $CFLAGS_CORE -c "$KERNEL_RELOCATOR_C" -o "$KOBJ_RELOCATOR"

echo "Compiling quiesce helpers..."
clang $CFLAGS_CORE -c "$KERNEL_QUIESCE_C" -o "$KOBJ_QUIESCE"

echo "Compiling ATA driver..."
clang $CFLAGS_CORE -c "$KERNEL_ATA_C" -o "$KOBJ_ATA"

echo "Compiling GPU & Mouse & DevFS & Partition..."
clang $CFLAGS_CORE -c "$KERNEL_DEVFS_C" -o "$KOBJ_DEVFS"
clang $CFLAGS_CORE -c "$KERNEL_PARTITION_C" -o "$KOBJ_PARTITION"
clang $CFLAGS_CORE -c "$KERNEL_GPU_C" -o "$KOBJ_GPU"
clang $CFLAGS_CORE -c "$KERNEL_MOUSE_C" -o "$KOBJ_MOUSE"
clang $CFLAGS_CORE -c "$KERNEL_LOADER_C" -o "$KOBJ_LOADER"

echo "Compiling common lib..."
clang $CFLAGS_CORE -c "$KERNEL_LIB_C" -o "$KOBJ_LIB"

echo "Compiling serial..."
clang $CFLAGS_CORE -c "$KERNEL_SERIAL_C" -o "$KOBJ_SERIAL"

echo "Compiling scheduler..."
clang $CFLAGS_CORE -c "$KERNEL_SCHED_C" -o "$KOBJ_SCHED"

echo "Compiling SMP..."
clang $CFLAGS_CORE -c "$KERNEL_SMP_C" -o "$KOBJ_SMP"

echo "Compiling per-CPU support..."
clang $CFLAGS_CORE -c "$KERNEL_PERCPU_C" -o "$KOBJ_PERCPU"

echo "Compiling APIC support..."
clang $CFLAGS_CORE -c "$KERNEL_APIC_C" -o "$KOBJ_APIC"

echo "Compiling I/O wait infrastructure..."
clang $CFLAGS_CORE -c "$KERNEL_IO_WAIT_C" -o "$KOBJ_IO_WAIT"

echo "Compiling GDT & Syscalls & Process..."
clang $CFLAGS_CORE -c "$KERNEL_GDT_C" -o "$KOBJ_GDT"
clang $CFLAGS_CORE -c "$KERNEL_SYSCALL_C" -o "$KOBJ_SYSCALL_C"
clang $CFLAGS_CORE -c "$KERNEL_PROCESS_C" -o "$KOBJ_PROCESS"
clang $CFLAGS_CORE -c "$KERNEL_RUST_SHIMS_C" -o "$KOBJ_RUST_SHIMS"
nasm -f elf64 "$KERNEL_SYSCALL_ASM" -o "$KOBJ_SYSCALL_ASM"

echo "Compiling IDT..."
clang $CFLAGS_CORE -c "$KDIR/idt.c" -o "$KOBJ_IDT"

echo "Assembling IDT stubs..."
nasm -f elf64 "$KDIR/idt_stubs.asm" -o "$KOBJ_IDT_STUBS"

echo "Assembling kernel entry..."
nasm -f elf64 "$KDIR/kernel_entry.asm" -o "$KOBJ_KERNEL_ENTRY"

echo "Compiling timer..."
clang $CFLAGS_CORE -c "$KDIR/timer.c" -o "$KOBJ_TIMER"

echo "Compiling UEFI entry..."
clang --target=x86_64-pc-windows-gnu $UEFI_CFLAGS -c "$KERNEL_ENTRY_C" -o "$KOBJ_ENTRY"

echo "Linking kernel ELF ($LINKER_SCRIPT)..."
# Build TSS
echo "Compiling TSS..."
clang $CFLAGS_TSS -c apps/tss/main.c -o "$BUILD/tss.o"
ld.lld -m elf_x86_64 -nostdlib -Ttext=0x8000000000 -o "$BUILD/tss.fx" "$BUILD/tss.o"

echo "Embedding TSS into ramfs..."
python3 -c "import sys; data = open('$BUILD/tss.fx', 'rb').read(); print('#include \"../fs/vfs.h\"\nunsigned char tss_fx_data[] = { ' + ', '.join(hex(b) for b in data) + ' };\nunsigned int tss_fx_len = ' + str(len(data)) + ';\nvoid load_tss_into_ramfs(void) { vfs_write(\"/bin/tss.fx\", 0, (const char*)tss_fx_data, tss_fx_len); }')" > "$BUILD/tss_fs.c"
clang $CFLAGS_CORE -c "$BUILD/tss_fs.c" -o "$BUILD/tss_fs.o"

echo "Building Rust kernel library..."
if (cd "$RUST_DIR" && "$RUST_CARGO" build --target "$RUST_TARGET_BARE" --release); then
  RUST_TARGET_USED="$RUST_TARGET_BARE"
else
  echo "Rust bare-metal target unavailable, falling back to $RUST_TARGET_FALLBACK"
  RUST_TARGET_USED="$RUST_TARGET_FALLBACK"
  (cd "$RUST_DIR" && "$RUST_CARGO" build --target "$RUST_TARGET_FALLBACK" --release)
fi
RUST_LIB="$RUST_DIR/target/$RUST_TARGET_USED/release/libfoxos_rs.a"

echo "Linking kernel ELF ($LINKER_SCRIPT)..."
ld.lld -m elf_x86_64 -T "$LINKER_SCRIPT" -nostdlib -o "$KELF" \
  "$KOBJ_KERNEL_ENTRY" "$KOBJ_C" "$KOBJ_KBD" "$KOBJ_CONS" "$KOBJ_MEM" "$KOBJ_RELOC" "$KOBJ_QUIESCE" "$KOBJ_RELOCATOR" "$KOBJ_TESTS" "$KOBJ_VFS" "$KOBJ_RAMFS" "$KOBJ_INITRD" "$KOBJ_FAT32" "$KOBJ_BIO" "$KOBJ_FOXFS" "$KOBJ_ATA" "$KOBJ_SERIAL" "$KOBJ_SCHED" "$KOBJ_SMP" "$KOBJ_PERCPU" "$KOBJ_APIC" "$KOBJ_IO_WAIT" "$KOBJ_GDT" "$KOBJ_SYSCALL_C" "$KOBJ_SYSCALL_ASM" "$KOBJ_PROCESS" "$KOBJ_RUST_SHIMS" "$KOBJ_IDT" "$KOBJ_IDT_STUBS" "$KOBJ_TIMER" "$KOBJ_DEVFS" "$KOBJ_PARTITION" "$KOBJ_GPU" "$KOBJ_MOUSE" "$KOBJ_LOADER" "$KOBJ_LIB" "$BUILD/tss_fs.o" --whole-archive "$RUST_LIB" --no-whole-archive

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
  mmd -i "$ESP_IMG@@1048576" ::/bin
  mcopy -i "$ESP_IMG@@1048576" "$UEFI_BIN" ::/EFI/BOOT/BOOTX64.EFI
  mcopy -i "$ESP_IMG@@1048576" "$KELF" ::/EFI/BOOT/KERNEL.ELF
  mcopy -i "$ESP_IMG@@1048576" "$BUILD/tss.fx" ::/bin/tss.fx
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
  HUMAN_QEMU_CMD="qemu-system-x86_64 -m ${QEMU_MEM:-2G} -display gtk -no-shutdown -serial stdio -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE -drive if=ide,format=raw,file=$ESP_IMG -no-reboot"
  if [[ -n "$QEMU_VARS" ]]; then
    echo "Run in QEMU:"
    # Prefer GUI run when DISPLAY is available, otherwise recommend headless
  if [ -n "${DISPLAY:-}" ]; then
    HUMAN_QEMU_CMD="qemu-system-x86_64 -m ${QEMU_MEM:-2G} -display gtk -no-shutdown -serial stdio -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE -drive if=pflash,format=raw,file=$BUILD/OVMF_VARS.fd -drive if=ide,format=raw,file=$ESP_IMG -no-reboot"
    echo "$HUMAN_QEMU_CMD"
    echo "Headless verifier:"
    echo "qemu-system-x86_64 -m ${QEMU_MEM:-2G} -serial stdio -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE -drive if=pflash,format=raw,file=build/OVMF_VARS.fd -drive if=ide,format=raw,file=build/esp.img -display none -monitor unix:build/qemu-monitor.sock,server,nowait -no-reboot"
  else
    HUMAN_QEMU_CMD="qemu-system-x86_64 -m ${QEMU_MEM:-2G} -display none -no-shutdown -serial stdio -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE -drive if=pflash,format=raw,file=$BUILD/OVMF_VARS.fd -drive if=ide,format=raw,file=$ESP_IMG -no-reboot"
    echo "No DISPLAY detected — recommended headless run:"
    echo "$HUMAN_QEMU_CMD"
  fi
  fi

else
  echo "Run in QEMU:"
  echo "qemu-system-x86_64 -m 512 -serial stdio -drive if=ide,format=raw,file=$ESP_IMG -no-reboot"
fi

echo "Done. UEFI binary: $UEFI_BIN"
