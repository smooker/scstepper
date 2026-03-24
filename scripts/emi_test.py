#!/usr/bin/env python3
"""EMI endstop test — 1000 cycles moveto 0 <-> 3.12

Architecture:
  GDB FIFO (/tmp/gdb_fifo)  -> SWD reset via 'run', stays attached
  pyserial (/dev/ttyACMTarg) -> CDC serial CLI, prompt-based completion
  GDB never detaches.  Serial opened ONCE after USB re-enum, never closed.

Prompt detection: firmware prompt has no trailing newline.
  pyserial reads raw bytes, regex matches prompt at end of buffer.
  Move commands produce TWO prompts (immediate + completion).
  We wait for prompt + 0.5s silence = move done.
"""

import serial
import subprocess
import signal
import sys
import os
import re
import time
import threading
from pathlib import Path

# --- Configuration ---
PORT = "/dev/ttyACMTarg"
GDB_PORT = "/dev/ttyBmpGdb"
GDB_FIFO = "/tmp/gdb_fifo"
GDB_LOG = "/tmp/gdb_out.log"
LOG = Path("/home/claude-agent/work/claude2smooker/emi_test.log")
FLAG = Path("/home/claude-agent/work/claude2smooker/emi_stop.flag")
BELL = Path("/home/claude-agent/work/claude2smooker/bell.txt")
ELF = "build/scstepper.elf"

CYCLES = 1000
BOOT_TIMEOUT = 30
HOME_TIMEOUT = 60
RANGE_TIMEOUT = 60
MOVE_TIMEOUT = 15
MOVE_TARGET = 3.12        # avoid 3.13 — float boundary with rangeUsableMm
STABLE_SILENCE = 0.5      # seconds of silence after prompt = move done

# Prompt regex — matches "  3.12 > " or "XXXX.XX > " or " -1.23 > "
PROMPT_RE = re.compile(rb'[\-0-9 X.]+> $')

# Anomaly patterns — any of these in serial output = stop immediately
ANOMALY_RE = re.compile(rb'ES_L hit|ES_R hit|JOGL_|JOGR_|BUTT_STEP')

# Boot banner — firmware reset detected
BANNER_RE = re.compile(rb'stepper_sc')


class EMITest:
    def __init__(self):
        self.ser = None
        self.gdb_proc = None
        self.gdb_fd = None
        self.reset_count = 0
        self.logf = None

    # --- Logging ---
    def log(self, msg):
        line = f"{time.strftime('%H:%M:%S')} {msg}"
        print(line, flush=True)
        if self.logf:
            self.logf.write(line + "\n")
            self.logf.flush()

    def log_raw(self, data):
        """Log raw firmware output (bytes)."""
        if self.logf and data:
            self.logf.write(data.decode(errors="replace"))
            self.logf.flush()

    # --- Serial (opened ONCE, never closed until end) ---
    def serial_open(self):
        """Open serial port immediately when device appears after USB re-enum.
        Called once. Never call serial_close() during normal operation."""
        self.log(f"  waiting for {PORT}...")
        for i in range(60):
            if os.path.exists(PORT):
                try:
                    self.ser = serial.Serial(PORT, timeout=0.1)
                    self.log(f"  serial open: {PORT}")
                    return
                except serial.SerialException:
                    pass
            time.sleep(0.25)
        self.log(f"ABORT: {PORT} did not reappear after 15s")
        sys.exit(1)

    def serial_reopen(self):
        """Reopen serial after GDB 'run' causes USB re-enum.
        This is the ONLY valid reason to close and reopen."""
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.ser = None
        self.serial_open()

    def send(self, cmd):
        """Send command to firmware."""
        self.log(f">>> {cmd}")
        self.ser.write(f"{cmd}\r".encode())

    def read_until_prompt(self, timeout, label=""):
        """Read until prompt appears. Returns (buf, got_prompt).
        Checks for anomalies and unexpected resets in real-time."""
        buf = b""
        start = time.time()

        while time.time() - start < timeout:
            if FLAG.exists():
                self.log_raw(buf)
                return buf, False

            chunk = self.ser.read(512)
            if chunk:
                buf += chunk

                # Anomaly detection (real-time)
                if ANOMALY_RE.search(chunk):
                    self.log_raw(buf)
                    anomaly = chunk.decode(errors="replace").strip()
                    self.log(f"!!! ANOMALY: {anomaly}")
                    FLAG.write_text(
                        f"{time.strftime('%H:%M:%S')} ANOMALY: {anomaly}\n")
                    self.ser.write(b"stop\r")
                    return buf, False

                # Reset detection
                if BANNER_RE.search(chunk):
                    self.reset_count += 1
                    self.log(f"UNEXPECTED RESET #{self.reset_count}")
                    if self.reset_count >= 3:
                        self.log(f"TOO MANY RESETS ({self.reset_count})")
                        FLAG.write_text(
                            f"{time.strftime('%H:%M:%S')} TOO MANY RESETS\n")
                        self.log_raw(buf)
                        return buf, False

            # Prompt detection
            if PROMPT_RE.search(buf):
                self.log_raw(buf)
                return buf, True

        self.log_raw(buf)
        self.log(f"TIMEOUT: no prompt after {timeout}s for '{label}'")
        return buf, False

    def send_wait(self, cmd, timeout):
        """Send command, wait for prompt. For non-move commands."""
        self.send(cmd)
        _, ok = self.read_until_prompt(timeout, label=cmd)
        if not ok:
            self.escalate()
            return False
        return True

    def send_wait_move(self, cmd, timeout):
        """Send move command, wait for prompt + silence.

        Move commands are non-blocking in firmware: ProcessLine sends prompt
        immediately, then main loop sends a second prompt when motor stops.
        We wait for prompt + STABLE_SILENCE seconds of no more data.
        """
        self.send(cmd)
        buf = b""
        last_prompt_time = 0
        start = time.time()

        while time.time() - start < timeout:
            if FLAG.exists():
                self.log_raw(buf)
                return False

            chunk = self.ser.read(256)
            if chunk:
                buf += chunk
                self.log_raw(chunk)

                # Anomaly detection
                if ANOMALY_RE.search(chunk):
                    anomaly = chunk.decode(errors="replace").strip()
                    self.log(f"!!! ANOMALY: {anomaly}")
                    FLAG.write_text(
                        f"{time.strftime('%H:%M:%S')} ANOMALY: {anomaly}\n")
                    self.ser.write(b"stop\r")
                    return False

                # Reset detection
                if BANNER_RE.search(chunk):
                    self.reset_count += 1
                    self.log(f"UNEXPECTED RESET #{self.reset_count}")
                    if self.reset_count >= 3:
                        FLAG.write_text(
                            f"{time.strftime('%H:%M:%S')} TOO MANY RESETS\n")
                        return False

                if PROMPT_RE.search(buf):
                    last_prompt_time = time.time()

            elif last_prompt_time > 0:
                if time.time() - last_prompt_time >= STABLE_SILENCE:
                    return True

        self.log(f"TIMEOUT: {timeout}s for '{cmd}'")
        self.escalate()
        return False

    # --- GDB FIFO ---
    def gdb_start(self):
        """Start GDB with FIFO input. Stays alive entire test.
        Uses threading to avoid FIFO open deadlock."""
        if os.path.exists(GDB_FIFO):
            os.unlink(GDB_FIFO)
        os.mkfifo(GDB_FIFO)

        # Writer in background thread (unblocks reader in Popen)
        gdb_fd_holder = [None]
        def open_writer():
            gdb_fd_holder[0] = open(GDB_FIFO, "w")
        writer_thread = threading.Thread(target=open_writer, daemon=True)
        writer_thread.start()

        gdb_log_fd = open(GDB_LOG, "w")
        self.gdb_proc = subprocess.Popen(
            ["arm-none-eabi-gdb", "-nx"],
            stdin=open(GDB_FIFO, "r"),
            stdout=gdb_log_fd,
            stderr=gdb_log_fd,
        )

        writer_thread.join(timeout=5)
        self.gdb_fd = gdb_fd_holder[0]
        self.log(f"  GDB started, PID={self.gdb_proc.pid}")

    def gdb_send(self, cmd, delay=1.0):
        """Send command to GDB via FIFO."""
        if self.gdb_fd:
            self.gdb_fd.write(cmd + "\n")
            self.gdb_fd.flush()
            time.sleep(delay)

    def gdb_stop(self):
        """Close FIFO -> GDB reads EOF -> quits."""
        if self.gdb_fd:
            try:
                self.gdb_fd.close()
            except Exception:
                pass
            self.gdb_fd = None
        if self.gdb_proc:
            try:
                self.gdb_proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.gdb_proc.kill()
                self.gdb_proc.wait()
            self.gdb_proc = None
        if os.path.exists(GDB_FIFO):
            os.unlink(GDB_FIFO)

    # --- Escalation ---
    def escalate(self):
        """serial stop -> GDB Stepper_Stop -> GDB run -> reconnect serial."""
        self.log("!!! ESCALATION")

        # Step 1: stop via serial
        try:
            self.ser.write(b"stop\r")
        except Exception:
            pass
        time.sleep(2)

        # Step 2: GDB call Stepper_Stop()
        self.gdb_send("call Stepper_Stop()", delay=1)

        # Step 3: GDB run (full restart via SWD — causes USB re-enum)
        self.gdb_send("run", delay=3)

        # Step 4: reopen serial (USB re-enumerated)
        self.serial_reopen()

        # Step 5: wait for boot (12s for morse boot sequence)
        time.sleep(12)
        self.ser.reset_input_buffer()
        self.ser.write(b"\r")

        _, got = self.read_until_prompt(BOOT_TIMEOUT, "boot (escalation)")
        if not got:
            self.log("ESCALATION FAILED — target unresponsive!")
            BELL.write_text("EMI test ESCALATION FAILED!\n")
            FLAG.write_text(f"{time.strftime('%H:%M:%S')} ESCALATION FAILED\n")

    # --- Cleanup ---
    def cleanup(self):
        self.gdb_stop()
        # Serial: let Python close it on exit — don't close explicitly
        if self.reset_count > 0:
            self.log(f"WARNING: {self.reset_count} unexpected reset(s)")
        self.log(f"=== CLEANUP: {time.strftime('%Y-%m-%d %H:%M:%S')} ===")
        if self.logf:
            self.logf.close()
            self.logf = None

    # --- Pre-flight checks ---
    def preflight(self):
        r = subprocess.run(["pgrep", "-f", "minicom.*ttyACM"],
                           capture_output=True)
        if r.returncode == 0:
            print("ABORT: minicom is running on ttyACM — close it first")
            sys.exit(1)

        # Kill stale GDB — only ONE GDB allowed
        subprocess.run(["pkill", "-9", "-f", "arm-none-eabi-gdb"],
                        capture_output=True)
        time.sleep(2)

        if not os.path.exists(PORT):
            print(f"ABORT: {PORT} not found — is target connected?")
            sys.exit(1)

        if not os.path.exists(GDB_PORT):
            print(f"ABORT: {GDB_PORT} not found — is BMP connected?")
            sys.exit(1)

        # Kill stale socat from previous bash runs
        subprocess.run(["pkill", "-f", f"socat.*{PORT}"],
                        capture_output=True)

    # --- Main ---
    def run(self):
        self.preflight()
        FLAG.unlink(missing_ok=True)

        LOG.parent.mkdir(parents=True, exist_ok=True)
        self.logf = open(LOG, "w")
        self.log(f"=== EMI TEST START: {time.strftime('%Y-%m-%d %H:%M:%S')} ===")
        self.log(f"Cycles: {CYCLES}, range: 0 <-> {MOVE_TARGET} mm")

        # --- Step 0: GDB attach + run ---
        self.log("--- Step 0: GDB attach + run ---")
        self.gdb_start()
        self.gdb_send(f"file {ELF}", 1)
        self.gdb_send(f"target extended-remote {GDB_PORT}", 3)
        self.gdb_send("monitor swdp_scan", 2)
        self.gdb_send("attach 1", 2)
        self.gdb_send("set confirm off", 1)
        self.gdb_send("run", 3)
        self.log("  target resetting — listen for beeps!")

        # --- Step 1+2: USB re-enum + serial open ---
        self.log("--- Step 1+2: USB re-enum + serial open ---")
        self.serial_open()

        # --- Step 3: Wait for boot to complete ---
        self.log("--- Step 3: waiting for boot (morse + buttons check)... ---")
        time.sleep(12)

        # --- Step 4: Get prompt ---
        self.log("--- Step 4: get prompt ---")
        self.ser.reset_input_buffer()
        self.ser.write(b"\r")
        _, got = self.read_until_prompt(BOOT_TIMEOUT, "boot")
        if not got:
            # Maybe EEPROM blank — try 'y'
            self.log("  no prompt — trying 'y' for EEPROM init")
            self.ser.write(b"y\r")
            time.sleep(5)
            self.ser.reset_input_buffer()
            self.ser.write(b"\r")
            _, got = self.read_until_prompt(15, "boot-eeprom")
        if not got:
            self.log("ABORT: boot timeout")
            sys.exit(1)
        self.log("  boot OK")

        # --- Step 5: Home ---
        self.log("--- Step 5: home ---")
        if not self.send_wait("home", HOME_TIMEOUT):
            self.log("ABORT during home")
            sys.exit(1)
        self.log("  home done")

        # --- Step 6: Range ---
        self.log("--- Step 6: range ---")
        if not self.send_wait("range", RANGE_TIMEOUT):
            self.log("ABORT during range")
            sys.exit(1)
        self.log("  range done")

        # --- Step 7: Enable diag mode + debug ---
        self.log("--- Step 7: diag ON + debug 1 ---")
        self.send("di")
        time.sleep(1)
        self.send("set debug 1")
        time.sleep(1)
        self.ser.read(4096)  # drain

        # --- Step 8: N cycles ---
        self.log(f"--- Step 8: {CYCLES} cycles moveto 0 <-> {MOVE_TARGET} ---")
        for i in range(1, CYCLES + 1):
            if FLAG.exists():
                self.log(f"STOPPED at cycle {i} — {FLAG.read_text().strip()}")
                sys.exit(1)

            if not self.send_wait_move(f"moveto {MOVE_TARGET}", MOVE_TIMEOUT):
                self.log(f"STOPPED at cycle {i}")
                sys.exit(1)

            if FLAG.exists():
                self.log(f"STOPPED at cycle {i} (ret) — "
                         f"{FLAG.read_text().strip()}")
                sys.exit(1)

            if not self.send_wait_move("moveto 0", MOVE_TIMEOUT):
                self.log(f"STOPPED at cycle {i} (return)")
                sys.exit(1)

            if i % 50 == 0:
                self.log(f"  cycle {i}/{CYCLES} OK")

        # --- Step 9: Cleanup ---
        self.send("di")
        self.send("set debug 0")
        time.sleep(1)

        # --- Final report ---
        status = "PASSED" if self.reset_count == 0 else "PASSED (with resets)"
        self.log(f"=== EMI TEST {status}: {CYCLES} cycles, "
                 f"0 anomalies, {self.reset_count} reset(s) ===")
        self.log(f"=== FINISHED: {time.strftime('%Y-%m-%d %H:%M:%S')} ===")


def main():
    test = EMITest()

    def sig_handler(signum, frame):
        test.log(f"Signal {signum} — cleaning up")
        test.cleanup()
        sys.exit(1)

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    try:
        test.run()
    finally:
        test.cleanup()


if __name__ == "__main__":
    main()
