---
name: feedback_gdb_run_before_test
description: Every test session must start with GDB 'run' to reset the target cleanly — user listens for morse boot beeps as audio confirmation
type: feedback
---

Every test session must start with GDB `run` to reset the target from a clean state.

**Why:** Target may be stuck in a blocking function (e.g. RunHomeEx waiting for ES_L). Only GDB `run` (SWD reset) can recover. The user listens for the buzzer morse boot sequence (E, V, Z, OK) as audio feedback that the target rebooted.

**How to apply:** Test sequence is always: GDB attach → `run` → wait USB re-enum → open serial (once!) → wait boot prompt → test commands. After `run`, USB re-enumerates so serial port must be reopened (this is the ONLY valid reason to reopen it).
