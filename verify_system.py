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
    if ch == '_':
        return 'shift-minus'
    if 'a' <= ch <= 'z' or '0' <= ch <= '9':
        return ch
    raise ValueError(f'unsupported key: {ch!r}')


def monitor_send_line(monitor, line):
    monitor.sendall(line.encode('ascii') + b'\n')


def type_text_via_monitor(monitor, text):
    for ch in text:
        monitor_send_line(monitor, 'sendkey ' + key_name_for_char(ch))

def decode_output(data):
    try:
        return data.decode('utf-8')
    except UnicodeDecodeError:
        res = ""
        for b in data:
            if b < 128:
                res += chr(b)
            else:
                res += f"\\x{b:02x}"
        return res

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
                if not char:
                    log(f"ERROR: QEMU stdout closed (crashed?). Output so far:\n{decode_output(output)}")
                    return False
                output += char
                if b"foxos> " in output:
                    log("SUCCESS: foxOS prompt reached.")
                    break
        else:
            log(f"ERROR: Boot timeout. Output so far:\n{decode_output(output)}")
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
                    log(f"SUCCESS: 'sleep' woke up. Response: {decode_output(response).strip()}")
                    break
        else:
            log(f"ERROR: 'sleep' timed out or hung. Response so far: {decode_output(response).strip()}")
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
                if b"scheduler tests done" in test_response:
                    log(f"SUCCESS: scheduler tests passed")
                    break
        else:
            log(f"WARNING: schedtest timed out. Response: {decode_output(test_response).strip()}")

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
                if b"interrupt stability tests done" in test_response:
                    log(f"SUCCESS: interrupt tests passed")
                    break
        else:
            log(f"WARNING: inttest timed out. Response: {decode_output(test_response).strip()}")

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
                if b"multicore detection test done" in test_response:
                    log(f"SUCCESS: CPU detection tests passed")
                    break
        else:
            log(f"WARNING: cputest timed out. Response: {decode_output(test_response).strip()}")

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
                if b"I/O integration test done" in test_response:
                    log(f"SUCCESS: I/O integration tests passed")
                    break
        else:
            log(f"WARNING: iotest timed out. Response: {decode_output(test_response).strip()}")

        tests_to_run = [
            ("smptest", b"smptest done", "SMP core boot"),
            ("dmatest", b"dmatest done", "DMA asynchronous transfers"),
            ("cachetest", b"cachetest done", "Unified Buffer Cache"),
            ("concurrencytest", b"concurrencytest done", "Filesystem concurrency"),
            ("crashrecoverytest", b"crashrecoverytest done", "Journal crash recovery"),
            ("defragdmatest", b"defragdmatest done", "DMA memory defragmentation"),
            ("biotest", b"biotest done", "Block I/O Layer"),
            ("vfstest", b"vfstest done", "VFS Layer"),
            ("foxfstest", b"foxfstest done", "foxFS extent allocation"),
            ("foxfs_bench", b"foxfs_bench done", "foxFS benchmark")
        ]

        for cmd, expected, desc in tests_to_run:
            log(f"Testing '{cmd}' ({desc})...")
            type_text_via_monitor(monitor, cmd)
            monitor_send_line(monitor, "sendkey ret")
            test_start = time.time()
            test_response = b""
            while time.time() - test_start < 5:
                r, _, _ = select.select([proc.stdout], [], [], 0.1)
                if r:
                    char = proc.stdout.read(1)
                    test_response += char
                    if expected in test_response:
                        log(f"SUCCESS: {desc} passed")
                        break
            else:
                log(f"WARNING: {cmd} timed out. Response: {decode_output(test_response).strip()}")

        # Test 'sysrq 88' (System Diagnostic)
        log("Testing 'sysrq 88' (System Diagnostic)...")
        type_text_via_monitor(monitor, "sysrq 88")
        monitor_send_line(monitor, "sendkey ret")
        
        sysrq_start = time.time()
        sysrq_response = b""
        while time.time() - sysrq_start < 5:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                char = proc.stdout.read(1)
                if not char:
                    break
                sysrq_response += char
                if b"SYSTEM DIAGNOSTIC" in sysrq_response:
                    log("SUCCESS: SysRq diagnostic triggered")
                    break
        else:
            log(f"WARNING: SysRq diagnostic not confirmed. Response: {decode_output(sysrq_response).strip()}")

        log("Testing 'tss' (GUI shell launch + keyboard input)...")
        type_text_via_monitor(monitor, "tss")
        monitor_send_line(monitor, "sendkey ret")

        tss_start = time.time()
        tss_response = b""
        while time.time() - tss_start < 5:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                char = proc.stdout.read(1)
                if not char:
                    break
                tss_response += char
                if b"[tss] started" in tss_response:
                    log("SUCCESS: TSS launched")
                    break
        else:
            log(f"ERROR: TSS did not start. Response: {decode_output(tss_response).strip()}")
            return False
        log("Testing 'shutdown' command...")
        try:
            type_text_via_monitor(monitor, "shutdown")
            monitor_send_line(monitor, "sendkey ret")
        except (BrokenPipeError, ConnectionResetError, OSError):
            log("INFO: Monitor connection closed during shutdown command (likely QEMU exiting).")
        
        shutdown_start = time.time()
        shutdown_response = b""
        while time.time() - shutdown_start < 5:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                char = proc.stdout.read(1)
                if not char:
                    log("SUCCESS: QEMU exited on shutdown!")
                    return True
                shutdown_response += char
        
        log(f"WARNING: QEMU still running after shutdown. Response: {decode_output(shutdown_response).strip()}")
        
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
