#!/bin/bash
# Test 99-steppersys.rules against all ttyACM devices
# Run as root on host (not chroot)

for dev in /sys/class/tty/ttyACM*; do
    name=$(basename "$dev")
    echo "--- $name ---"
    udevadm test "$dev" 2>&1 | grep -E "steppersys|SYMLINK|MODE.*0666"
    echo
done
