---
name: feedback_serial_keep_open
description: Never close/reopen the serial port to the target — CDC_IsConnected resets and firmware stops TX
type: feedback
---

Never close and reopen the serial port to the target during a session. Keep it open throughout.

**Why:** Every `ser.close()` / `serial.Serial()` cycle causes USB CDC disconnect → `CDC_IsConnected = 0` in firmware → firmware stops sending output → appears unresponsive. This is what caused the Phase 3 test failure — port was opened/closed between test phases.

**How to apply:** Open the port once at session start. Pass the `ser` object to all functions. Never close until the entire test/session is done. In emi_test.py, `serial_open()` should only be called once (and again only after a GDB `run` reset which re-enumerates USB).
