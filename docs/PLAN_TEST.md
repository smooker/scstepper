# EMI Endstop Test Plan

## Goal

Exercise motor 1000 cycles between 0 and 3.13 mm in diag mode,
monitoring for false endstop/button triggers caused by EMI from the
stepper driver during motion.

## Prerequisites

- Target flashed with current firmware (diag mode + debug bit0 support)
- Minicom closed — serial port free for socat
- GDB free — used only for reset if needed
- BMP GDB port accessible at `/dev/ttyBMPGDB`

## Pre-flight checks (script must verify before starting)

```bash
# 1. No minicom on target port — would steal serial data
pgrep -f "minicom.*ttyACM" && { echo "ABORT: minicom is running"; exit 1; }

# 2. No GDB attached — would halt target on connect
pgrep -f "arm-none-eabi-gdb" && { echo "ABORT: GDB is running"; exit 1; }

# 3. Serial port exists and is accessible
[ -e /dev/ttyACMTarg ] || { echo "ABORT: /dev/ttyACMTarg not found"; exit 1; }

# 4. BMP GDB port exists (needed for hard reset + escalation)
[ -e /dev/ttyBMPGDB ] || { echo "ABORT: /dev/ttyBMPGDB not found"; exit 1; }
```

## Procedure

| Step | Action | Wait |
|------|--------|------|
| 0 | GDB attach → `monitor hard_reset` → detach (clean start) | 3s (USB re-enum) |
| 1 | socat PTY bridge: `/dev/ttyACMTarg` ↔ `/tmp/emi_pty` | 1s |
| 2 | Start background monitor (log + anomaly detection + reset counting) | — |
| 3 | Wait for boot (USB enum + banner) | 3s |
| 4 | `home` — approach ES_L, backoff, set posHomed | 15s |
| 5 | `range` — measure ES_L→ES_R, store rangeUsableMm | 25s |
| 6 | `di` — enable diag mode (endstops don't stop motor) | 0.5s |
| 7 | `set debug 1` — enable endstop event printing | 1s |
| 8 | 1000× `moveto 3.13` → sleep 6s → `moveto 0` → sleep 6s | ~3.3h |
| 9 | Cleanup: `di` (disable diag) + `set debug 0` | 1s |

## Monitoring

- Background reader on PTY → `emi_test.log`
- Grep for: `ES_L hit`, `ES_R hit`, `JOGL_`, `JOGR_`, `BUTT_STEP`
- On anomaly: send `stop`, write `emi_stop.flag`, halt test loop

### Reset detection

- Monitor greps for boot banner (`scstepper v`) in serial output
- Each mid-test banner = unexpected firmware reset → logged as `UNEXPECTED RESET #N`
- Counter stored in temp file (subshell-safe)
- 3+ resets → auto-abort (write `emi_stop.flag`)

### Timeout escalation

If PTY write fails after a command (target unresponsive):

| Step | Action | Wait |
|------|--------|------|
| 1 | `stop` via socat (serial) | 2s |
| 2 | GDB attach → `call Stepper_Stop()` → detach | 1s |
| 3 | GDB attach → `monitor hard_reset` → detach | — |
| 4 | Alert user via `bell.txt` | — |

Timeout threshold: PTY write failure after command send.
Never leave a motor running unattended without confirmed feedback.

## Sentinel sync

- `claude2smooker/emi_stop.flag` — created by monitor on anomaly, timeout, or 3+ resets
- Test loop checks before each moveto — stops if flag exists

## Rules

- **Never flash** during test — only reset if needed
- **Count resets** in the log (each banner = firmware reboot; logged + counted)
- **On anomaly**: stop motor, log event + timestamp, write flag
- **On clean finish**: log "1000 cycles OK" + timestamp + reset count summary
- **On timeout**: escalate through all channels (serial → GDB halt → GDB reset → bell)

## Log

- Full serial output: `claude2smooker/emi_test.log`
- Progress every 50 cycles
- Anomaly details with timestamp
- Reset count summary at end

## Script

`scripts/emi_test.sh` — single socat PTY bridge, sentinel file pattern,
GDB hard reset at start, timeout escalation, boot banner reset counting

## Expected duration

~3.3 hours (1000 cycles × 12s + home/range overhead)
