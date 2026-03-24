#!/usr/bin/env python3
"""EMI test — no GDB, reset via CDC command.
   reset → boot (buttons enabled) → flush → home → range → N cycles.

   Port opened with exact minicom --noinit termios settings (from stty.txt).
   Log to file + stderr. No stdout mixing with serial data.
   Silent during boot — zero TX until "buttons enabled".
"""
import os, time, re, sys, termios, fcntl, select

PORT = "/dev/ttyACMTarg"
CYCLES = 1000
LOG = "/home/claude-agent/work/claude2smooker/emi_test.log"
PROMPT_RE = re.compile(rb'([\-0-9.]+)\s*> $')

logf = None
fd = -1

def log(msg):
    line = f"{time.strftime('%H:%M:%S')} {msg}\n"
    if logf: logf.write(line); logf.flush()
    sys.stderr.write(line); sys.stderr.flush()

def open_port():
    """Open serial exactly like minicom --noinit (settings from stty.txt)."""
    f = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    fl = fcntl.fcntl(f, fcntl.F_GETFL)
    fcntl.fcntl(f, fcntl.F_SETFL, fl & ~os.O_NONBLOCK)

    a = termios.tcgetattr(f)

    # c_iflag: ignbrk only
    a[0] = termios.IGNBRK

    # c_oflag: raw
    a[1] = 0

    # c_cflag: cs8 hupcl cread clocal crtscts -parenb -cstopb
    a[2] = termios.CS8 | termios.HUPCL | termios.CREAD | termios.CLOCAL | termios.CRTSCTS

    # c_lflag: raw
    a[3] = 0

    # speeds: ispeed 9600, ospeed 115200
    a[4] = termios.B9600     # ispeed
    a[5] = termios.B115200   # ospeed

    # cc: VMIN=1 VTIME=5 (500ms)
    a[6][termios.VMIN] = 1
    a[6][termios.VTIME] = 5

    termios.tcsetattr(f, termios.TCSANOW, a)
    return f

def wait_port():
    """Wait for /dev/ttyACMTarg to appear, open it."""
    for i in range(60):
        if os.path.exists(PORT):
            try: return open_port()
            except: pass
        time.sleep(0.25)
    log("ABORT: port gone"); sys.exit(1)

def fd_read(timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try: buf += os.read(fd, 512)
            except: pass
    return buf

def read_until(marker, timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try: chunk = os.read(fd, 512)
            except: chunk = b""
            if chunk:
                buf += chunk
                if logf: logf.write(chunk.decode(errors='replace')); logf.flush()
                if marker in buf: return buf
    log(f"TIMEOUT {timeout}s for {marker}")
    return buf

def flush_linebuf():
    os.write(fd, b'\r'); time.sleep(0.3); fd_read(0.3)
    os.write(fd, b'\r'); time.sleep(0.3); fd_read(0.3)

def get_pos():
    os.write(fd, b'\r')
    buf = b""
    end = time.time() + 3
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try: buf += os.read(fd, 256)
            except: pass
        m = PROMPT_RE.search(buf)
        if m:
            try: return float(m.group(1))
            except: return None
    return None

def moveto_wait(target, timeout=15):
    os.write(fd, f"moveto {target}\r".encode())
    time.sleep(0.3); fd_read(0.3)
    last_pos = None; same = 0; start = time.time()
    while time.time() - start < timeout:
        time.sleep(0.25)
        pos = get_pos()
        if pos is None: continue
        if abs(pos - target) < 0.05: return True, pos
        if last_pos is not None and abs(pos - last_pos) < 0.01:
            same += 1
            if same >= 6: return False, pos
        else: same = 0
        last_pos = pos
    return False, last_pos

# ============================================================
os.makedirs(os.path.dirname(LOG), exist_ok=True)
logf = open(LOG, "w")
log("=== EMI TEST START ===")

# OPEN + FLUSH + RESET
fd = wait_port()
flush_linebuf()
log("RESET")
os.write(fd, b'reset\r')
time.sleep(0.5)
os.close(fd)
time.sleep(5)

# REOPEN after USB re-enum
fd = wait_port()
log("REOPEN")

# BOOT: silent wait
buf = read_until(b"buttons enabled", 40)
if b"buttons enabled" not in buf:
    log("ABORT boot"); sys.exit(1)
log("BOOT OK")
time.sleep(1)
flush_linebuf()

# HOME
log("HOME")
os.write(fd, b'home\r')
buf = read_until(b"0.00 >", 60)
if b"0.00 >" not in buf:
    log("HOME FAIL"); sys.exit(1)
fd_read(0.5)
log("HOME OK")

# RANGE
log("RANGE")
os.write(fd, b'range\r')
buf = read_until(b"usable", 60)
m = re.search(rb'([\d.]+) mm usable', buf)
if not m: log("RANGE FAIL"); sys.exit(1)
range_mm = float(m.group(1))
read_until(b"0.00 >", 30)
fd_read(0.5)
move_target = round(range_mm - 0.01, 2)
log(f"RANGE OK {range_mm} target={move_target}")

# CYCLES
stalls = 0; t0 = time.time()
for i in range(1, CYCLES + 1):
    ok, p = moveto_wait(move_target)
    if not ok: stalls += 1; log(f"STALL {i} fwd @ {p}")
    ok, p = moveto_wait(0.0)
    if not ok: stalls += 1; log(f"STALL {i} ret @ {p}")
    if i % 50 == 0:
        e = time.time() - t0
        eta = (e / i) * (CYCLES - i)
        log(f"cycle {i}/{CYCLES} stalls={stalls} {e:.0f}s ~{eta:.0f}s ETA")

e = time.time() - t0
log(f"=== DONE {CYCLES} cycles, {stalls} stalls, {e:.0f}s ===")
os.close(fd); logf.close()
