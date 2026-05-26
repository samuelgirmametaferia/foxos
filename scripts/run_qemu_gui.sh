#!/bin/sh
set -eu
QEMU_MEM=${QEMU_MEM:-16G}
QEMU_CODE=${QEMU_CODE:-/usr/share/edk2/x64/OVMF_CODE.4m.fd}
ESP_IMG="build/esp.img"
VARS_IMG="build/OVMF_VARS.fd"
SERIAL_TCP_PORT=${SERIAL_TCP_PORT:-4444}

if [ ! -f "$QEMU_CODE" ]; then
  echo "OVMF_CODE.fd not found at $QEMU_CODE; try installing edk2-ovmf or set QEMU_CODE"
  exit 1
fi

# If no X11 DISPLAY is available, fall back to headless with serial-over-TCP so user can connect
if [ -z "${DISPLAY:-}" ]; then
  echo "No DISPLAY detected; running headless QEMU with serial over TCP on port $SERIAL_TCP_PORT"
  CMD="qemu-system-x86_64 -m $QEMU_MEM -display none -no-shutdown -serial tcp:127.0.0.1:$SERIAL_TCP_PORT,server,nowait -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE"
  if [ -f "$VARS_IMG" ]; then
    CMD="$CMD -drive if=pflash,format=raw,file=$VARS_IMG"
  fi
  CMD="$CMD -drive if=ide,format=raw,file=$ESP_IMG -no-reboot"
  echo "Connect to serial with: nc 127.0.0.1 $SERIAL_TCP_PORT"
  exec $CMD
else
  # Use GUI display and expose serial over TCP so users can connect with nc
  CMD="qemu-system-x86_64 -m $QEMU_MEM -display gtk -no-shutdown -serial tcp:127.0.0.1:$SERIAL_TCP_PORT,server,nowait -drive if=pflash,format=raw,readonly=on,file=$QEMU_CODE"
  if [ -f "$VARS_IMG" ]; then
    CMD="$CMD -drive if=pflash,format=raw,file=$VARS_IMG"
  fi
  CMD="$CMD -drive if=ide,format=raw,file=$ESP_IMG -no-reboot"

  echo "Running QEMU GUI. Use the QEMU window for keyboard input."
  echo "Connect to serial with: nc 127.0.0.1 $SERIAL_TCP_PORT"
  exec $CMD
fi
