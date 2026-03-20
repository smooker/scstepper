#!/bin/bash
# go.sh — build, flash, capture, view
# Usage: go.sh [command]
#
# Commands:
#   build          Build firmware
#   flash          Build + flash via BMP GDB
#   capture        Capture mover+movel (asks before moving!)
#   view [file]    Open in pulseview with decoders
#   speed [file]   Generate speed analog channel + open
#   all            build + flash + capture + speed + view
#
# Examples:
#   go.sh build
#   go.sh flash
#   go.sh capture
#   go.sh view ../captures/capture_both.sr
#   go.sh speed ../captures/capture_both.sr
#   go.sh all

set -e
cd "$(dirname "$0")/.."
SPM=400

cmd_build() {
    echo "=== BUILD ==="
    make
}

cmd_flash() {
    cmd_build
    echo "=== FLASH ==="
    arm-none-eabi-gdb -batch \
        -ex "file build/scstepper.elf" \
        -ex "target extended-remote /dev/ttyBmpGdb" \
        -ex "monitor swdp_scan" \
        -ex "attach 1" \
        -ex "mem 0x40000000 0x50000000 rw" \
        -ex "load" \
        -ex "compare-sections" \
        -ex "detach"
    echo "Flash OK"
}

cmd_capture() {
    local SR="captures/capture_both.sr"
    echo "=== CAPTURE ==="
    echo "Will send: mover 5, then movel 5"
    read -p "Motor safe to move? [y/N] " yn
    [ "$yn" = "y" ] || [ "$yn" = "Y" ] || { echo "Aborted"; exit 1; }

    sigrok-cli -d fx2lafw -c samplerate=100K --channels D6,D7 --samples 600000 -o "$SR" &
    local PID=$!
    sleep 0.5
    echo "mover 5" > /dev/ttyACMTarg
    echo "  mover 5 sent"
    sleep 3
    echo "movel 5" > /dev/ttyACMTarg
    echo "  movel 5 sent"
    wait $PID
    echo "Capture done: $SR"
}

cmd_speed() {
    local SR="${1:-captures/capture_both.sr}"
    local OUT="${SR%.sr}_speed.sr"
    echo "=== SPEED ==="
    python3 - "$SR" "$OUT" "$SPM" << 'PYEOF'
import zipfile, struct, sys

sr_file, out_file, spm = sys.argv[1], sys.argv[2], float(sys.argv[3])
z = zipfile.ZipFile(sr_file)
chunks = sorted([f for f in z.namelist() if f.startswith('logic-1-')])
raw = b''.join(z.read(c) for c in chunks)
meta = z.read('metadata').decode()

samplerate = 100000
for line in meta.split('\n'):
    if line.startswith('samplerate='):
        s = line.split('=')[1].strip()
        s = s.replace(' ', '')
        if s.lower().endswith('khz'):
            samplerate = int(float(s[:-3]) * 1000)
        elif s.lower().endswith('mhz'):
            samplerate = int(float(s[:-3]) * 1000000)
        elif s.lower().endswith('hz'):
            samplerate = int(float(s[:-2]))
        else:
            samplerate = int(s)

speed = [0.0] * len(raw)
prev_rising = None
last_period = None
prev_d7 = raw[0] & 0x80
current_speed = 0.0

for i in range(1, len(raw)):
    d7 = raw[i] & 0x80
    d6 = raw[i] & 0x40
    if d7 and not prev_d7:
        if prev_rising is not None:
            last_period = i - prev_rising
            freq = samplerate / last_period
            current_speed = freq / spm
            if not d6:
                current_speed = -current_speed
        prev_rising = i
    if prev_rising is not None and last_period is not None:
        if (i - prev_rising) > last_period * 2:
            current_speed = 0.0
    speed[i] = current_speed
    prev_d7 = d7

speed_data = struct.pack(f'<{len(speed)}f', *speed)

new_meta = meta.rstrip() + '\n'
new_meta = new_meta.replace('total analog=0', 'total analog=1\nanalog9=Speed mm/s')
if 'total analog' not in new_meta:
    new_meta = new_meta.replace('unitsize=1', 'total analog=1\nanalog9=Speed mm/s\nunitsize=1')

CHUNK = 4 * 1024 * 1024
with zipfile.ZipFile(out_file, 'w', zipfile.ZIP_DEFLATED) as zout:
    zout.writestr('version', '2')
    zout.writestr('metadata', new_meta)
    for c in chunks:
        zout.writestr(c, z.read(c))
    n = 1
    off = 0
    while off < len(speed_data):
        zout.writestr(f'analog-1-9-{n}', speed_data[off:off+CHUNK])
        off += CHUNK
        n += 1

print(f"Speed range: {min(speed):.1f} to {max(speed):.1f} mm/s")
print(f"Written: {out_file}")
PYEOF

    local PVS="${OUT%.sr}.pvs"
    cat > "$PVS" << 'PVSEOF'
[General]
decode_signals=2
generated_signals=0
meta_objs=0
views=1

[D6]
enabled=true
name=D6

[D7]
enabled=true
name=D7

[Speed%20mm]
s\enabled=true
s\name=Speed mm/s

[decode_signal0]
channel0\assigned_signal_name=D7
channel0\initial_pin_state=0
channel0\name=Data
channel1\assigned_signal_name=D6
channel1\initial_pin_state=0
channel1\name=Reset
channels=2
decoder0\ann_class0\visible=true
decoder0\ann_class1\visible=true
decoder0\ann_class2\visible=true
decoder0\id=counter
decoder0\option0\name=data_edge
decoder0\option0\type=s
decoder0\option0\value=@ByteArray(rising\0)
decoder0\option1\name=reset_edge
decoder0\option1\type=s
decoder0\option1\value=@ByteArray(any\0)
decoder0\options=2
decoder0\row0\visible=true
decoder0\row1\visible=true
decoder0\row2\visible=true
decoder0\visible=true
decoders=1
enabled=true
name=Edge Counter

[decode_signal1]
channel0\assigned_signal_name=D7
channel0\initial_pin_state=0
channel0\name=Step
channel1\assigned_signal_name=D6
channel1\initial_pin_state=0
channel1\name=Direction
channels=2
decoder0\ann_class0\visible=true
decoder0\ann_class1\visible=true
decoder0\id=stepper_motor
decoder0\option0\name=steps_per_mm
decoder0\option0\type=d
decoder0\option0\value=@ByteArray(\0\0\0\0\0\0y@)
decoder0\option1\name=unit
decoder0\option1\type=s
decoder0\option1\value=@ByteArray(mm\0)
decoder0\options=2
decoder0\row0\visible=true
decoder0\row1\visible=true
decoder0\visible=true
decoders=1
enabled=true
name=Stepper Motor

[view0]
D6\trace_height=64
D7\trace_height=64
Speed%20mm\s\autoranging=true
Speed%20mm\s\display_type=0
Speed%20mm\s\div_height=128
Speed%20mm\s\neg_vdivs=1
Speed%20mm\s\pos_vdivs=1
Speed%20mm\s\scale_index=4
segment_display_mode=1
PVSEOF
    echo "Session: $PVS"
}

cmd_view() {
    local SR="${1:-captures/capture_both_speed.sr}"
    local PVS="${SR%.sr}.pvs"
    echo "=== VIEW ==="
    if [ ! -f "$PVS" ]; then
        echo "No .pvs found, opening raw"
        pulseview "$SR" &
    else
        pulseview "$SR" -s "$PVS" &
    fi
    echo "pulseview: $SR"
}

case "${1:-help}" in
    build)   cmd_build ;;
    flash)   cmd_flash ;;
    capture) cmd_capture ;;
    speed)   cmd_speed "$2" ;;
    view)    cmd_view "$2" ;;
    all)
        cmd_flash
        sleep 2
        cmd_capture
        cmd_speed "captures/capture_both.sr"
        cmd_view "captures/capture_both_speed.sr"
        ;;
    *)
        echo "Usage: go.sh {build|flash|capture|speed|view|all} [file]"
        ;;
esac
