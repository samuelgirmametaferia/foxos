#!/bin/sh
set -eu
ITER=${1:-5}
BUILD_SCRIPT="./build.sh"
LOGDIR="logs"
mkdir -p "$LOGDIR"
for i in $(seq 1 "$ITER"); do
  echo "Smoke run #$i"
  $BUILD_SCRIPT
  LOG="$LOGDIR/run_$i.log"
  qemu-system-x86_64 -m 16G -serial file:"$LOG" -display none \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
    -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
    -drive if=ide,format=raw,file=build/esp.img -no-reboot &
  QPID=$!
  # let QEMU run briefly then terminate and save logs
  sleep 5
  kill "$QPID" || true
done

echo "Smoke runs completed. Logs in $LOGDIR"
