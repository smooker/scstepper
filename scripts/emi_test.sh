#!/bin/bash
# EMI endstop test — 1000 cycles moveto 0 <-> 3.13
#
# Architecture:
#   GDB FIFO (/tmp/gdb_fifo)  → SWD reset via 'run', stays attached
#   socat PTY (/tmp/emi_pty)   → CDC serial CLI, firmware prompt, monitoring
#   GDB never detaches. Socat restarts after each USB re-enumeration.

set -euo pipefail

PORT=/dev/ttyACMTarg
GDB_PORT=/dev/ttyBmpGdb
PTY=/tmp/emi_pty
GDB_FIFO=/tmp/gdb_fifo
GDB_LOG=/tmp/gdb_out.log
LOG=/home/claude-agent/work/claude2smooker/emi_test.log
FLAG=/home/claude-agent/work/claude2smooker/emi_stop.flag
CYCLES=1000
BOOT_TIMEOUT=20
HOME_TIMEOUT=30
RANGE_TIMEOUT=40
MOVE_TIMEOUT=15

RESET_COUNT_FILE=""
GDB_PID=""
SOCAT_PID=""
MONITOR_PID=""

# --- Pre-flight checks ---
pgrep -f "minicom.*ttyACM" >/dev/null && { echo "ABORT: minicom is running on ttyACM — close it first"; exit 1; }
pgrep -f "arm-none-eabi-gdb" >/dev/null && { echo "ABORT: GDB is running — detach first"; exit 1; }
[ -e "$PORT" ] || { echo "ABORT: $PORT not found — is target connected?"; exit 1; }
[ -e "$GDB_PORT" ] || { echo "ABORT: $GDB_PORT not found — is BMP connected?"; exit 1; }

# Kill stale socat from previous run
pkill -f "socat.*$PTY" 2>/dev/null || true
pkill -f "socat.*$PORT" 2>/dev/null || true
sleep 1
rm -f "$PTY" "${PTY}.lock" "$GDB_FIFO"

RESET_COUNT_FILE=$(mktemp /tmp/emi_reset_count.XXXXXX)
echo 0 > "$RESET_COUNT_FILE"

cleanup() {
    exec 3>&- 2>/dev/null || true
    kill $SOCAT_PID $MONITOR_PID $GDB_PID 2>/dev/null || true
    rm -f "$PTY" "${PTY}.lock" "$GDB_FIFO"
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

rm -f "$FLAG"
echo "=== EMI TEST START: $(date) ===" | tee "$LOG"
echo "Cycles: $CYCLES, range: 0 <-> 3.13 mm" | tee -a "$LOG"

# --- GDB FIFO setup (stays alive entire test) ---
mkfifo "$GDB_FIFO"
arm-none-eabi-gdb -nx < "$GDB_FIFO" > "$GDB_LOG" 2>&1 &
GDB_PID=$!
exec 3>"$GDB_FIFO"

gdb_send() {
    echo "$1" >&3
    sleep "${2:-0.5}"
}

# --- kill ALL old socat, start fresh after USB re-enum ---
start_socat() {
    # Kill ALL socat on our PTY/PORT + monitor (not just $SOCAT_PID)
    pkill -f "socat.*$PTY" 2>/dev/null || true
    pkill -f "socat.*$PORT" 2>/dev/null || true
    kill $MONITOR_PID 2>/dev/null || true
    rm -f "$PTY" "${PTY}.lock"
    sleep 2

    # Verify they're dead
    if pgrep -f "socat.*$PORT" >/dev/null; then
        pkill -9 -f "socat.*$PORT" 2>/dev/null || true
        sleep 1
    fi

    # Wait for ttyACMTarg to reappear
    echo "  waiting for $PORT..." | tee -a "$LOG"
    local waited=0
    while [ ! -e "$PORT" ]; do
        sleep 0.5
        waited=$((waited + 1))
        if [ "$waited" -ge 30 ]; then
            echo "ABORT: $PORT did not reappear after 15s" | tee -a "$LOG"
            exit 1
        fi
    done
    sleep 2  # settle — let USB CDC fully initialize

    # Start socat
    socat "PTY,link=$PTY,rawer,wait-slave" "$PORT,rawer,b115200" &
    SOCAT_PID=$!
    sleep 1
    [ -e "$PTY" ] || { echo "ERROR: PTY not created, socat failed" | tee -a "$LOG"; exit 1; }

    # Verify PTY is alive
    if ! printf '\r' > "$PTY" 2>/dev/null; then
        echo "ERROR: PTY write failed after socat start" | tee -a "$LOG"
        exit 1
    fi

    # Start background monitor
    cat "$PTY" | while IFS= read -r line; do
        echo "$line" >> "$LOG"
        # Anomaly detection
        if echo "$line" | grep -qE "ES_L hit|ES_R hit|JOGL_|JOGR_|BUTT_STEP"; then
            echo "$(date) ANOMALY: $line" > "$FLAG"
            echo "!!! ANOMALY at $(date): $line" >> "$LOG"
            printf 'stop\r' > "$PTY"
        fi
        # Reset detection
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
}

# --- send command to target via socat PTY ---
send() {
    echo ">>> $1" >> "$LOG"
    printf '%s\r' "$1" > "$PTY"
}

# --- wait for firmware prompt in log ---
wait_prompt() {
    local timeout="$1" label="$2"
    local start elapsed
    start=$(date +%s)
    while true; do
        [ -f "$FLAG" ] && return 1
        if tail -1 "$LOG" 2>/dev/null | grep -qE '^[0-9 X.]+> '; then
            return 0
        fi
        elapsed=$(( $(date +%s) - start ))
        if [ "$elapsed" -ge "$timeout" ]; then
            echo "$(date) TIMEOUT: no prompt after ${timeout}s waiting for '$label'" | tee -a "$LOG"
            return 1
        fi
        sleep 0.5
    done
}

# --- send command, wait for prompt ---
send_wait() {
    local cmd="$1" timeout="$2"
    send "$cmd"
    if ! wait_prompt "$timeout" "$cmd"; then
        escalate
        return 1
    fi
    return 0
}

# --- escalation: serial stop → GDB Stepper_Stop → GDB run → bell ---
escalate() {
    echo "!!! ESCALATION at $(date)" | tee -a "$LOG"

    # Step 1: stop via serial
    printf 'stop\r' > "$PTY" 2>/dev/null || true
    sleep 2

    # Step 2: GDB call Stepper_Stop()
    gdb_send "call Stepper_Stop()" 1

    # Step 3: GDB run (full restart)
    gdb_send "run" 1

    # Step 4: restart socat after USB re-enum
    start_socat

    # Step 5: wait for boot
    if ! wait_prompt $BOOT_TIMEOUT "boot (escalation)"; then
        echo "EMI test ESCALATION FAILED — target unresponsive!" > /home/claude-agent/work/claude2smooker/bell.txt
        echo "ESCALATION FAILED" > "$FLAG"
    fi
}

# =====================================================================
# MAIN
# =====================================================================

# --- Step 0: GDB attach + run ---
echo "--- Step 0: GDB attach + run ---" | tee -a "$LOG"
gdb_send "file build/scstepper.elf" 0.5
gdb_send "target extended-remote $GDB_PORT" 0.5
gdb_send "monitor swdp_scan" 0.5
gdb_send "attach 1" 1
gdb_send "set confirm off" 0.3
gdb_send "run" 1

# --- Step 1+2: Wait for USB re-enum + start socat ---
echo "--- Step 1+2: USB re-enum + socat ---" | tee -a "$LOG"
start_socat

# --- Step 4: Wait for boot prompt ---
echo "--- Step 4: waiting for boot prompt ---" | tee -a "$LOG"
if ! wait_prompt $BOOT_TIMEOUT "boot"; then
    echo "ABORT: boot timeout" | tee -a "$LOG"
    exit 1
fi
echo "  boot prompt received" | tee -a "$LOG"

# --- Step 5: Home ---
echo "--- Step 5: home ---" | tee -a "$LOG"
send_wait "home" $HOME_TIMEOUT || { echo "ABORT during home" | tee -a "$LOG"; exit 1; }
echo "  home done" | tee -a "$LOG"

# --- Step 6: Range ---
echo "--- Step 6: range ---" | tee -a "$LOG"
send_wait "range" $RANGE_TIMEOUT || { echo "ABORT during range" | tee -a "$LOG"; exit 1; }
echo "  range done" | tee -a "$LOG"

# --- Step 7: Enable diag mode ---
echo "--- Step 7: diag ON + debug 1 ---" | tee -a "$LOG"
send "di"
sleep 1
send "set debug 1"
sleep 1

# --- Step 8: 1000 cycles ---
echo "--- Step 8: $CYCLES cycles moveto 0 <-> 3.13 ---" | tee -a "$LOG"
for i in $(seq 1 $CYCLES); do
    [ -f "$FLAG" ] && { echo "STOPPED at cycle $i — $(cat "$FLAG")" | tee -a "$LOG"; exit 1; }

    send_wait "moveto 3.13" $MOVE_TIMEOUT || { echo "STOPPED at cycle $i" | tee -a "$LOG"; exit 1; }

    [ -f "$FLAG" ] && { echo "STOPPED at cycle $i (return) — $(cat "$FLAG")" | tee -a "$LOG"; exit 1; }

    send_wait "moveto 0" $MOVE_TIMEOUT || { echo "STOPPED at cycle $i (return)" | tee -a "$LOG"; exit 1; }

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
