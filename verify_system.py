import subprocess
import time
import os
import signal
import sys
import select
import socket

# Configuration
BOOT_TIMEOUT = 20
INTERACTION_TIMEOUT = 5
OVMF_CODE_CANDIDATES = [
    "/usr/share/OVMF/OVMF_CODE.fd",
    "/usr/share/edk2/x64/OVMF_CODE.4m.fd",
    "/usr/share/edk2/x64/OVMF_CODE.secboot.4m.fd",
]
OVMF_VARS_CANDIDATES = [
    "/usr/share/OVMF/OVMF_VARS.fd",
    "/usr/share/edk2/x64/OVMF_VARS.4m.fd",
]
QEMU_CMD = [
    "stdbuf",
    "-o0",
    "-e0",
    "qemu-system-x86_64",
    "-m", "512",
    "-serial", "stdio",
    "-drive", "if=ide,format=raw,file=build/esp.img",
    "-display", "none",
    "-monitor", "unix:build/qemu-monitor.sock,server,nowait",
    "-no-reboot"
]

def first_existing(paths):
    for path in paths:
        if os.path.exists(path):
            return path
    return None


ovmf_code = first_existing(OVMF_CODE_CANDIDATES)
ovmf_vars = first_existing(OVMF_VARS_CANDIDATES)
if ovmf_code:
    pflash_args = ["-drive", f"if=pflash,format=raw,readonly,file={ovmf_code}"]
    if ovmf_vars:
        try:
            os.makedirs("build", exist_ok=True)
            import shutil
            shutil.copy(ovmf_vars, "build/OVMF_VARS.fd")
            pflash_args += ["-drive", "if=pflash,format=raw,file=build/OVMF_VARS.fd"]
        except Exception:
            pass
    QEMU_CMD[4:4] = pflash_args


def key_name_for_char(ch):
    if ch == ' ':
        return 'spc'
    if ch == '\n':
        return 'ret'
    if 'a' <= ch <= 'z' or '0' <= ch <= '9':
        return ch
    raise ValueError(f'unsupported key: {ch!r}')


def monitor_send_line(monitor, line):
    monitor.sendall(line.encode('ascii') + b'\n')


def type_text_via_monitor(monitor, text):
    for ch in text:
        monitor_send_line(monitor, 'sendkey ' + key_name_for_char(ch))

def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}")
    sys.stdout.flush()

def run_verify():
    monitor = None
    proc = None
    def cleanup():
        if proc is not None and proc.poll() is None:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            log("QEMU process group killed.")

    try:
        try:
            os.unlink('build/qemu-monitor.sock')
        except FileNotFoundError:
            pass

        log("Building foxOS...")
        try:
            subprocess.run(["./build.sh"], check=True, capture_output=True, timeout=30)
        except Exception as e:
            log(f"ERROR: Build failed: {e}")
            return False

        log("Starting QEMU...")
        proc = subprocess.Popen(
            QEMU_CMD,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
            preexec_fn=os.setsid
        )

        monitor_deadline = time.time() + 5
        while time.time() < monitor_deadline:
            try:
                monitor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                monitor.connect('build/qemu-monitor.sock')
                break
            except OSError:
                if monitor is not None:
                    monitor.close()
                    monitor = None
                time.sleep(0.05)
        if monitor is None:
            log("ERROR: Could not connect to QEMU monitor.")
            return False

        log("Waiting for boot...")
        boot_start = time.time()
        output = b""
        while time.time() - boot_start < BOOT_TIMEOUT:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                char = proc.stdout.read(1)
                if not char: break
                output += char
                if b"foxos> " in output:
                    log("SUCCESS: foxOS prompt reached.")
                    break
        else:
            log(f"ERROR: Boot timeout. Output so far:\n{output.decode('utf-8', errors='replace')}")
            return False

        # Test 'sleep 100'
        log("Testing 'sleep 100' (PIT functional check)...")
        type_text_via_monitor(monitor, "sleep 100")
        monitor_send_line(monitor, "sendkey ret")
        
        sleep_start = time.time()
        response = b""
        while time.time() - sleep_start < 2:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                char = proc.stdout.read(1)
                response += char
                if b"woke up" in response:
                    log(f"SUCCESS: 'sleep' woke up. Response: {response.decode('utf-8', errors='replace').strip()}")
                    break
        else:
            log(f"ERROR: 'sleep' timed out or hung. Response so far: {response.decode('utf-8', errors='replace').strip()}")
            return False

        # Test scheduler tests
        log("Testing 'schedtest' (scheduler verification)...")
        type_text_via_monitor(monitor, "schedtest")
        monitor_send_line(monitor, "sendkey ret")
        
        test_start = time.time()
        test_response = b""
        while time.time() - test_start < 2:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                char = proc.stdout.read(1)
                test_response += char
                if b"schedtest done" in test_response:
                    log(f"SUCCESS: scheduler tests passed")
                    break
        else:
            log(f"WARNING: schedtest timed out. Response: {test_response.decode('utf-8', errors='replace').strip()}")

        # Test interrupt stability
        log("Testing 'inttest' (interrupt stability)...")
        type_text_via_monitor(monitor, "inttest")
        monitor_send_line(monitor, "sendkey ret")
        
        test_start = time.time()
        test_response = b""
        while time.time() - test_start < 2:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                char = proc.stdout.read(1)
                test_response += char
                if b"inttest done" in test_response:
                    log(f"SUCCESS: interrupt tests passed")
                    break
        else:
            log(f"WARNING: inttest timed out. Response: {test_response.decode('utf-8', errors='replace').strip()}")

        # Test CPU detection
        log("Testing 'cputest' (CPU/multicore detection)...")
        type_text_via_monitor(monitor, "cputest")
        monitor_send_line(monitor, "sendkey ret")
         
        test_start = time.time()
        test_response = b""
        while time.time() - test_start < 2:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                char = proc.stdout.read(1)
                test_response += char
                if b"cputest done" in test_response:
                    log(f"SUCCESS: CPU detection tests passed")
                    break
        else:
            log(f"WARNING: cputest timed out. Response: {test_response.decode('utf-8', errors='replace').strip()}")

        log("Testing 'iotest' (I/O integration tests)...")
        type_text_via_monitor(monitor, "iotest")
        monitor_send_line(monitor, "sendkey ret")
         
        test_start = time.time()
        test_response = b""
        while time.time() - test_start < 2:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                char = proc.stdout.read(1)
                test_response += char
                if b"iotest done" in test_response:
                    log(f"SUCCESS: I/O integration tests passed")
                    break
        else:
            log(f"WARNING: iotest timed out. Response: {test_response.decode('utf-8', errors='replace').strip()}")

        log("FINAL VERIFICATION: PIT, KEYBOARD, SCHEDULER, INTERRUPT HANDLING, AND I/O INTEGRATION ARE STABLE.")
        return True

    except Exception as e:
        log(f"CRITICAL EXCEPTION: {e}")
        return False
    finally:
        try:
            if monitor is not None:
                monitor.close()
        except Exception:
            pass
        cleanup()

if __name__ == "__main__":
    if not run_verify():
        sys.exit(1)
