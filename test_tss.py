import subprocess
import time
import os
import signal
import sys
import socket
import select

QEMU_CMD = [
    "qemu-system-x86_64",
    "-m", "2G",
    "-serial", "stdio",
    "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd",
    "-drive", "if=pflash,format=raw,file=build/OVMF_VARS.fd",
    "-drive", "if=ide,format=raw,file=build/esp.img",
    "-display", "none",
    "-monitor", "unix:build/qemu-monitor.sock,server,nowait",
    "-no-reboot"
]

def monitor_send_line(monitor, line):
    monitor.sendall(line.encode('ascii') + b'\n')

def type_text_via_monitor(monitor, text):
    for ch in text:
        key = ch
        if ch == ' ': key = 'spc'
        elif ch == '\n': key = 'ret'
        elif ch == '/': key = 'slash'
        elif ch == '.': key = 'dot'
        monitor_send_line(monitor, f'sendkey {key}')

def run():
    try:
        os.unlink('build/qemu-monitor.sock')
    except:
        pass

    proc = subprocess.Popen(
        QEMU_CMD,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        preexec_fn=os.setsid
    )

    monitor = None
    deadline = time.time() + 10
    while time.time() < deadline:
        try:
            monitor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            monitor.connect('build/qemu-monitor.sock')
            break
        except:
            time.sleep(0.1)
    
    if not monitor:
        print("Failed to connect to monitor")
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        return

    output = b""
    boot_deadline = time.time() + 60
    while time.time() < boot_deadline:
        r, _, _ = select.select([proc.stdout], [], [], 0.1)
        if r:
            line = proc.stdout.readline()
            if not line: break
            output += line
            sys.stdout.write(line.decode('utf-8', 'ignore'))
            if b"foxos> " in line:
                break
    
    print("\nSending 'tss' command...")
    type_text_via_monitor(monitor, "tss\n")
    
    tss_deadline = time.time() + 20
    while time.time() < tss_deadline:
        r, _, _ = select.select([proc.stdout], [], [], 0.1)
        if r:
            line = proc.stdout.readline()
            if not line: break
            output += line
            sys.stdout.write(line.decode('utf-8', 'ignore'))
            if b"[tss] v1.0.1 starting" in line:
                print("\nTSS v1.0.1 started, waiting for more logs...")
            if b"[rust] sys_getinfo: success" in line:
                print("\nKernel reported sys_getinfo success!")
            if b"[vfs] sys_mmap: mapping complete" in line:
                print("\nFramebuffer mapped successfully!")
                return # Success!
    
    print("\nKilling QEMU...")
    os.killpg(os.getpgid(proc.pid), signal.SIGTERM)

if __name__ == "__main__":
    run()
