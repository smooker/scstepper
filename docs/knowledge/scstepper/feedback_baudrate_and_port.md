---
name: feedback_baudrate_and_port
description: Must set 115200 8N1 baudrate and use minicom-style port init — wrong baudrate caused garbled output and hard faults
type: feedback
---

Serial port MUST be opened with 115200 8N1, matching minicom config. Default kernel baudrate is 9600 — this causes garbled data which can crash the target (hard fault from buffer overflow).

**Why:** Spent hours debugging "garbled boot output" and "home doesn't respond" — root cause was baudrate mismatch (9600 vs 115200). Wrong baudrate = corrupted bytes entering firmware RX → garbage commands → potential buffer overflow → HardFault.

**How to apply:** Always set baudrate explicitly when opening serial port. Use `stty -F /dev/ttyACMTarg -a` with minicom running to capture exact working termios settings, then replicate in Python. Never assume default kernel tty settings are correct.
