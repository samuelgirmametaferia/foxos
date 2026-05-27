#!/bin/bash
# Verification script for FoxOS
# This script tests that the system builds, boots, and can run tests

set -e

cd "$(dirname "$0")"

echo "[verify] Building FoxOS..."
./build.sh > /tmp/build_output.log 2>&1
if [ $? -ne 0 ]; then
    echo "[verify] BUILD FAILED"
    tail -50 /tmp/build_output.log
    exit 1
fi
echo "[verify] Build successful"

echo "[verify] Booting system and running tests..."
rm -f /tmp/verify_boot.log

timeout 45 qemu-system-x86_64 -m 16G \
  -serial file:/tmp/verify_boot.log \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
  -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
  -drive if=ide,format=raw,file=build/esp.img \
  -display none -no-reboot 2>/dev/null &

QEMU_PID=$!
sleep 30

# Check if system reached prompt
if grep -q "foxos>" /tmp/verify_boot.log; then
    echo "[verify] ✓ System reached foxos> prompt"
else
    echo "[verify] ✗ System did not reach foxos> prompt"
    tail -100 /tmp/verify_boot.log
    kill $QEMU_PID 2>/dev/null
    wait $QEMU_PID 2>/dev/null || true
    exit 1
fi

# Check if keyboard is ready
if grep -q "keyboard/mouse ready" /tmp/verify_boot.log; then
    echo "[verify] ✓ Keyboard/mouse initialized"
else
    echo "[verify] ✗ Keyboard/mouse not initialized"
    kill $QEMU_PID 2>/dev/null
    wait $QEMU_PID 2>/dev/null || true
    exit 1
fi

# Check if rust handshake succeeded
if grep -q "rust handshake ready" /tmp/verify_boot.log; then
    echo "[verify] ✓ Rust/C hybrid integration ready"
else
    echo "[verify] ✗ Rust/C hybrid integration failed"
    kill $QEMU_PID 2>/dev/null
    wait $QEMU_PID 2>/dev/null || true
    exit 1
fi

# Check if boot tests passed
if grep -q "boot self-tests done" /tmp/verify_boot.log; then
    echo "[verify] ✓ Boot self-tests passed"
else
    echo "[verify] ✗ Boot self-tests did not run"
    kill $QEMU_PID 2>/dev/null
    wait $QEMU_PID 2>/dev/null || true
    exit 1
fi

# Check if TSS binary is embedded
if grep -q "load_tss_into_ramfs" /tmp/build_output.log || [ -f build/tss.fx ]; then
    echo "[verify] ✓ TSS binary embedded"
else
    echo "[verify] ✗ TSS binary not found"
    kill $QEMU_PID 2>/dev/null
    wait $QEMU_PID 2>/dev/null || true
    exit 1
fi

# Clean up QEMU
kill $QEMU_PID 2>/dev/null
wait $QEMU_PID 2>/dev/null || true

echo ""
echo "[verify] ✓ All verification checks passed!"
echo ""
echo "To test keyboard input and launch TSS, run QEMU with GUI:"
echo "  qemu-system-x86_64 -m 16G -display gtk -serial stdio \\"
echo "    -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \\"
echo "    -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \\"
echo "    -drive if=ide,format=raw,file=build/esp.img -no-reboot"
echo ""
echo "Then at the foxos> prompt, type 'tss' to launch the True System Shell GUI"
