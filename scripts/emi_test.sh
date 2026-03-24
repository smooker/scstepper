#!/bin/bash
# EMI endstop test — 1000 cycles moveto 0 <-> 3.13
# Single socat PTY bridge — one process owns the real serial port.
# Script reads/writes via the PTY. No port contention.
#
# Features:
#   - GDB hard reset before test (clean start)
#   - Background monitor: anomaly detection + boot banner reset counting
#   - Timeout escalation: serial stop → GDB Stepper_Stop() → GDB reset → bell
#   - Sentinel file pattern for async abort

PORT=/dev/ttyACMTarg
GDB_PORT=/dev/ttyBmpGdb
PTY=/tmp/emi_pty
LOG=/home/claude-agent/work/claude2smooker/emi_test.log
FLAG=/home/claude-agent/work/claude2smooker/emi_stop.flag
CYCLES=1000
MOVE_WAIT=6
HOME_WAIT=15
RANGE_WAIT=25
RESET_COUNT_FILE=""

# --- Pre-flight checks ---
pgrep -f "minicom.*ttyACM" >/dev/null && { echo "ABORT: minicom is running on ttyACM — close it first"; exit 1; }
pgrep -f "arm-none-eabi-gdb" >/dev/null && { echo "ABORT: GDB is running — detach first"; exit 1; }
[ -e "$PORT" ] || { echo "ABORT: $PORT not found — is target connected?"; exit 1; }
[ -e "$GDB_PORT" ] || { echo "ABORT: $GDB_PORT not found — is BMP connected?"; exit 1; }

# Kill stale socat from previous run (matching our PTY or PORT)
if pgrep -f "socat.*$PTY" >/dev/null || pgrep -f "socat.*$PORT" >/dev/null; then
    echo "WARNING: killing stale socat from previous run"
    pkill -f "socat.*$PTY" 2>/dev/null
    pkill -f "socat.*$PORT" 2>/dev/null
    sleep 1
fi
# Remove stale PTY symlink
rm -f "$PTY" "${PTY}.lock"

RESET_COUNT_FILE=$(mktemp /tmp/emi_reset_count.XXXXXX)
echo 0 > "$RESET_COUNT_FILE"

cleanup() {
    kill $SOCAT_PID $MONITOR_PID 2>/dev/null
    rm -f "$PTY" "${PTY}.lock"
    local resets=0
    if [ -n "$RESET_COUNT_FILE" ] && [ -f "$RESET_COUNT_FILE" ]; then
        resets=$(cat "$RESET_COUNT_FILE")
        rm -f "$RESET_COUNT_FILE"
    fi
    if [ "$resets" -gt 0 ] 2>/dev/null; then
        echo "WARNING: $resets unexpected reset(s) during test" | tee -a "$LOG"
    fi
    echo "=== CLEANUP: $(date) ===" >> "$LOG"
}
trap cleanup EXIT

rm -f "$FLAG" "$PTY"
echo "=== EMI TEST START: $(date) ===" | tee "$LOG"
echo "Cycles: $CYCLES, range: 0 <-> 3.13 mm" | tee -a "$LOG"

# --- Step 0: Hard reset via GDB (clean start) ---
echo "--- Step 0: hard reset via GDB ---" | tee -a "$LOG"
arm-none-eabi-gdb -batch \
    -ex "target extended-remote $GDB_PORT" \
    -ex "monitor hard_reset" \
    -ex "detach" \
    -ex "quit" 2>>"$LOG"
sleep 3  # USB re-enumeration after reset

# Re-check serial port after reset (USB re-enum may take time)
if [ ! -e "$PORT" ]; then
    echo "Waiting for $PORT after reset..." | tee -a "$LOG"
    for i in $(seq 1 10); do
        sleep 1
        [ -e "$PORT" ] && break
    done
    [ -e "$PORT" ] || { echo "ABORT: $PORT did not reappear after reset"; exit 1; }
fi

# --- Step 1: Single socat: real port <-> PTY ---
socat "PTY,link=$PTY,rawer,wait-slave" "$PORT,rawer,b115200" &
SOCAT_PID=$!
sleep 1

if [ ! -e "$PTY" ]; then
    echo "ERROR: PTY not created, socat failed" | tee -a "$LOG"
    exit 1
fi

# --- send command + log ---
send() {
    echo ">>> $1" >> "$LOG"
    printf '%s\r' "$1" > "$PTY"
}

# --- send command, wait, check PTY health ---
send_wait() {
    local cmd="$1" wait_sec="$2"
    send "$cmd"
    sleep "$wait_sec"
    # Check: can we still write to PTY? (detects USB disconnect / socat death)
    if ! printf '\r' > "$PTY" 2>/dev/null; then
        echo "$(date) TIMEOUT: PTY write failed after '$cmd'" >> "$LOG"
        escalate
    fi
}

# --- 4-step escalation: serial → GDB halt → GDB reset → bell ---
escalate() {
    echo "!!! ESCALATION at $(date)" | tee -a "$LOG"

    # Step 1: stop via serial (may already be dead)
    printf 'stop\r' > "$PTY" 2>/dev/null
    sleep 2

    # Step 2: GDB halt + Stepper_Stop()
    arm-none-eabi-gdb -batch \
        -ex "target extended-remote $GDB_PORT" \
        -ex "call Stepper_Stop()" \
        -ex "detach" -ex "quit" 2>>"$LOG" || true
    sleep 1

    # Step 3: GDB hard reset (last resort to stop motor)
    arm-none-eabi-gdb -batch \
        -ex "target extended-remote $GDB_PORT" \
        -ex "monitor hard_reset" \
        -ex "detach" -ex "quit" 2>>"$LOG" || true

    # Step 4: Alert user
    echo "EMI test ESCALATION — target unresponsive!" > /home/claude-agent/work/claude2smooker/bell.txt

    echo "ESCALATION — test aborted" > "$FLAG"
}

# --- Step 2: background monitor: read PTY, log, detect anomaly + resets ---
cat "$PTY" | while IFS= read -r line; do
    echo "$line" >> "$LOG"

    # Anomaly detection: false button/endstop triggers from EMI
    if echo "$line" | grep -qE "ES_L hit|ES_R hit|JOGL_|JOGR_|BUTT_STEP"; then
        echo "$(date) ANOMALY: $line" > "$FLAG"
        echo "!!! ANOMALY at $(date): $line" >> "$LOG"
        printf 'stop\r' > "$PTY"
    fi

    # Reset detection: boot banner mid-test = unexpected firmware reset
    if echo "$line" | grep -q "scstepper v"; then
        local_count=$(cat "$RESET_COUNT_FILE" 2>/dev/null || echo 0)
        local_count=$((local_count + 1))
        echo "$local_count" > "$RESET_COUNT_FILE"
        echo "$(date) UNEXPECTED RESET #$local_count" >> "$LOG"
        if [ "$local_count" -ge 3 ]; then
            echo "$(date) TOO MANY RESETS ($local_count) — aborting" > "$FLAG"
            printf 'stop\r' > "$PTY"
        fi
    fi
done &
MONITOR_PID=$!

# --- Step 3: Wait for boot banner ---
sleep 3

# --- Step 4: Home ---
echo "--- Step 4: home ---" | tee -a "$LOG"
send_wait "home" $HOME_WAIT
[ -f "$FLAG" ] && echo "ABORT during home: $(cat "$FLAG")" | tee -a "$LOG" && exit 1

# --- Step 5: Range ---
echo "--- Step 5: range ---" | tee -a "$LOG"
send_wait "range" $RANGE_WAIT
[ -f "$FLAG" ] && echo "ABORT during range: $(cat "$FLAG")" | tee -a "$LOG" && exit 1

# --- Step 6: Enable diag mode ---
echo "--- Step 6: diag ON + debug 1 ---" | tee -a "$LOG"
send "di"
sleep 0.5
send "set debug 1"
sleep 1

# --- Step 8: 1000 cycles ---
echo "--- Step 8: $CYCLES cycles moveto 0 <-> 3.13 ---" | tee -a "$LOG"
for i in $(seq 1 $CYCLES); do
    if [ -f "$FLAG" ]; then
        echo "STOPPED at cycle $i — $(cat "$FLAG")" | tee -a "$LOG"
        echo "=== EMI TEST STOPPED: $(date) ===" | tee -a "$LOG"
        exit 1
    fi

    send_wait "moveto 3.13" $MOVE_WAIT

    if [ -f "$FLAG" ]; then
        echo "STOPPED at cycle $i (return) — $(cat "$FLAG")" | tee -a "$LOG"
        echo "=== EMI TEST STOPPED: $(date) ===" | tee -a "$LOG"
        exit 1
    fi

    send_wait "moveto 0" $MOVE_WAIT

    if (( i % 50 == 0 )); then
        echo "  cycle $i/$CYCLES OK — $(date)" | tee -a "$LOG"
    fi
done

# --- Step 9: Cleanup ---
send "di"
send "set debug 0"
sleep 1

# --- Final report ---
RESETS=$(cat "$RESET_COUNT_FILE" 2>/dev/null || echo 0)
if [ "$RESETS" -gt 0 ]; then
    echo "WARNING: $RESETS unexpected reset(s) during test" | tee -a "$LOG"
fi

echo "=== EMI TEST PASSED: $CYCLES cycles, 0 anomalies, $RESETS reset(s) ===" | tee -a "$LOG"
echo "=== FINISHED: $(date) ===" | tee -a "$LOG"
