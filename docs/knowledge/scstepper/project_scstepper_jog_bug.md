---
name: scstepper_jog_bug
description: Jog infinite spin bug — jogState stays JOG_PRESSED after StartMove busy reject, triggers JOG_CONT without button held
type: project
originSessionId: 4606b3fc-a20f-4ad9-8e08-947da3723a4a
---
Bug: rapid jog button presses cause infinite motor spin.

**Root cause:** `StartMove()` (stepper.c:326) returns on "stepper busy" without signaling failure. Caller in `ProcessEvents()` (main.c:~1707) already set `jogState = JOG_PRESSED`. When motor finishes previous move and 300ms elapsed from `jogPressTick`, hold detection triggers `JOG_CONT` -> `Stepper_RunContinuous()` — but button is already released, so `Stepper_Stop()` never fires.

**Fix plan (2026-04-28):**
1. `StartMove()` returns bool (success/fail)
2. On fail -> reset `jogState` to `JOG_IDLE`
3. Hold detection should also check real GPIO state, not just state machine
4. Consider ISR/main-loop race on `jogState` access

**Files:** stepper.c (StartMove ~line 326), main.c (ProcessEvents ~line 1707, hold detection ~1736)
