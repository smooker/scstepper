#!/bin/bash
# pulse.sh — open capture in pulseview with stepper decoders
# Usage: pulse.sh [capture.sr] [steps_per_mm]
#
# Examples:
#   pulse.sh                                     # open captures/capture_both.sr, 400 spm
#   pulse.sh captures/capture_mover.sr           # open specific file
#   pulse.sh captures/capture_both.sr 80         # override steps_per_mm

cd "$(dirname "$0")/.."

SR="${1:-captures/capture_both.sr}"
SPM="${2:-400}"

if [ ! -f "$SR" ]; then
    echo "File not found: $SR"
    exit 1
fi

PVS="${SR%.sr}.pvs"

SPM_HEX=$(python3 -c "import struct; print(''.join(f'\\\\x{b:02x}' for b in struct.pack('<d', $SPM)))")

cat > "$PVS" << EOF
[General]
decode_signals=2
generated_signals=0
views=1
meta_objs=0

[D6]
name=D6
enabled=true

[D7]
name=D7
enabled=true

[decode_signal0]
name=Edge Counter
enabled=true
decoders=1
channels=1
decoder0\\id=counter
decoder0\\visible=true
decoder0\\options=1
decoder0\\option0\\name=data_edge
decoder0\\option0\\type=s
decoder0\\option0\\value=@ByteArray(rising\\x00)
channel0\\name=data
channel0\\initial_pin_state=0
channel0\\assigned_signal_name=D7

[decode_signal1]
name=Stepper Motor
enabled=true
decoders=1
channels=2
decoder0\\id=stepper_motor
decoder0\\visible=true
decoder0\\options=2
decoder0\\option0\\name=unit
decoder0\\option0\\type=s
decoder0\\option0\\value=@ByteArray(mm\\x00)
decoder0\\option1\\name=steps_per_mm
decoder0\\option1\\type=d
decoder0\\option1\\value=@ByteArray($SPM_HEX)
channel0\\name=step
channel0\\initial_pin_state=0
channel0\\assigned_signal_name=D7
channel1\\name=dir
channel1\\initial_pin_state=0
channel1\\assigned_signal_name=D6

[view0]
EOF

pulseview "$SR" -s "$PVS" &
echo "pulseview opened: $SR with $PVS (steps_per_mm=$SPM)"
