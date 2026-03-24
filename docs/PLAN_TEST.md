# EMI Endstop Test Plan

## Goal

Exercise motor 1000 cycles between 0 and 3.13 mm in diag mode,
monitoring for false endstop/button triggers caused by EMI from the
stepper driver during motion.

## Architecture

Two parallel channels to the target, each on its own USB interface:

- **GDB** (SWD via `/dev/ttyBmpGdb`) — reset + keeps target running.
  Stays attached for the entire test. Commands via FIFO (`/tmp/gdb_fifo`).
- **socat** (CDC via `/dev/ttyACMTarg`) — serial CLI, firmware prompt,
  anomaly monitoring. Started after USB re-enumeration completes.

GDB never detaches. Target runs only with debugger attached.
Feedback comes from socat (firmware prompt `"> "`), not from GDB.

## Prerequisites

- Target flashed with current firmware (diag mode + debug bit0 support)
- Minicom closed — serial port free for socat
- No other GDB sessions — script starts its own
- BMP GDB port accessible at `/dev/ttyBmpGdb`
- Kill stale socat from previous runs before starting

## Pre-flight checks (script must verify before starting)

```bash
# 1. No minicom on target port — would steal serial data
pgrep -f "minicom.*ttyACM" && { echo "ABORT: minicom is running"; exit 1; }

# 2. No GDB attached — would halt target on connect
pgrep -f "arm-none-eabi-gdb" && { echo "ABORT: GDB is running"; exit 1; }

# 3. Serial port exists and is accessible
[ -e /dev/ttyACMTarg ] || { echo "ABORT: /dev/ttyACMTarg not found"; exit 1; }

# 4. BMP GDB port exists
[ -e /dev/ttyBmpGdb ] || { echo "ABORT: /dev/ttyBmpGdb not found"; exit 1; }

# 5. Kill stale socat from previous run
pkill -f "socat.*/tmp/emi_pty" 2>/dev/null
pkill -f "socat.*/dev/ttyACMTarg" 2>/dev/null
```

## Procedure

| Step | Action | Completion check |
|------|--------|------------------|
| 0 | GDB FIFO: `attach 1` → `run` (stays attached, never detach) | GDB prints `"Starting program"` |
| 1 | Wait for USB re-enumeration (`ttyACMTarg` reappears) | `while [ ! -e /dev/ttyACMTarg ]; do sleep 0.5; done` |
| 2 | socat PTY bridge: `/dev/ttyACMTarg` ↔ `/tmp/emi_pty` | PTY symlink exists |
| 3 | Start background monitor (log + anomaly + reset counting) | — |
| 4 | Wait for boot prompt (`"> "` in log) | `wait_prompt BOOT_TIMEOUT` |
| 5 | `home` — approach ES_L, backoff, set posHomed | `wait_prompt HOME_TIMEOUT` |
| 6 | `range` — measure ES_L→ES_R, store rangeUsableMm | `wait_prompt RANGE_TIMEOUT` |
| 7 | `di` + `set debug 1` — enable diag mode + event printing | 1s sleep |
| 8 | 1000× `moveto 3.13` → wait prompt → `moveto 0` → wait prompt | `wait_prompt MOVE_TIMEOUT` per command |
| 9 | Cleanup: `di` + `set debug 0` | 1s sleep |
| 10 | Close GDB FIFO (`exec 3>&-` → GDB quits) | — |

### wait_prompt

Polls last line of log for firmware prompt pattern `^[0-9 X.]+> `.
Must NOT match `>>> cmd` log lines (script's own send markers).
Timeout → escalation.

## GDB FIFO lifecycle

```bash
mkfifo /tmp/gdb_fifo
arm-none-eabi-gdb -nx < /tmp/gdb_fifo > /tmp/gdb_out.log 2>&1 &
GDB_PID=$!
exec 3>/tmp/gdb_fifo   # hold FIFO open

# Send commands:
echo "file build/scstepper.elf" >&3
echo "target extended-remote /dev/ttyBmpGdb" >&3
echo "monitor swdp_scan" >&3
echo "attach 1" >&3
echo "set confirm off" >&3
echo "run" >&3
# GDB stays attached, target runs freely

# At end of test (or escalation reset):
exec 3>&-   # close FIFO → GDB reads EOF → quits
```

**Rules:**
- Always `-nx` (skip `.gdbinit`)
- Never `detach` — target needs debugger attached
- Never `monitor hard_reset` — BMP does not support it
- `run` restarts from `main()` via SWD — this is the only reset method
- `interrupt` as text does NOT work — GDB expects SIGINT, not a command

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

If `wait_prompt` times out (no firmware prompt within timeout):

| Step | Action |
|------|--------|
| 1 | `stop` via socat PTY (serial) |
| 2 | GDB FIFO: `call Stepper_Stop()` (target still attached) |
| 3 | GDB FIFO: `run` (full restart via SWD) |
| 4 | Wait for USB re-enum + new socat |
| 5 | If still unresponsive → alert user via `bell.txt` |

Never leave a motor running unattended without confirmed feedback.

## Sentinel sync

- `claude2smooker/emi_stop.flag` — created by monitor on anomaly, timeout, or 3+ resets
- Test loop checks before each moveto — stops if flag exists

## Rules

- **Never flash** during test — only `run` reset if needed
- **Count resets** in the log (each banner = firmware reboot; logged + counted)
- **On anomaly**: stop motor, log event + timestamp, write flag
- **On clean finish**: log "1000 cycles OK" + timestamp + reset count summary
- **On timeout**: escalate (serial stop → GDB Stepper_Stop → GDB run → bell)
- **GDB stays attached** for the entire test — never detach

## Log

- Full serial output: `claude2smooker/emi_test.log`
- GDB output: `/tmp/gdb_out.log`
- Progress every 50 cycles
- Anomaly details with timestamp
- Reset count summary at end

## Script

`scripts/emi_test.sh` — GDB FIFO + socat PTY bridge, sentinel file pattern,
prompt-based completion check, boot banner reset counting

## Expected duration

~3.3 hours (1000 cycles, prompt-based wait per move + home/range overhead)
