#!/bin/sh
set -eu
QEMU_MEM=${QEMU_MEM:-16G}
QEMU_CODE=${QEMU_CODE:-/usr/share/edk2/x64/OVMF_CODE.4m.fd}
ESP_IMG="build/esp.img"
VARS_IMG="build/OVMF_VARS.fd"

if [ ! -f "$QEMU_CODE" ]; then
  echo "OVMF_CODE.fd not found at $QEMU_CODE; try installing edk2-ovmf or set QEMU_CODE"
  exit 1
fi

CMD="qemu-system-x86_64 -m $QEMU_MEM -serial stdio -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE"
if [ -f "$VARS_IMG" ]; then
  CMD="$CMD -drive if=pflash,format=raw,file=$VARS_IMG"
fi
CMD="$CMD -drive if=ide,format=raw,file=$ESP_IMG"

echo "Running QEMU GUI with command:\n$CMD"
exec $CMD
