#!/bin/bash
# Inject one or more commands into firmware's CDC RX ring buffer via GDB
# Usage: ./inject_cmd.sh "cmd1" "cmd2" "cmd3"
# All commands are written in a single GDB session (one attach/detach).
# Each command gets CR (0x0D) appended so firmware processes it as KEY_ENTER.

if [ $# -eq 0 ]; then
    echo "Usage: $0 \"command1\" [\"command2\" ...]"
    exit 1
fi

# Build GDB script: read rxHead, write all commands + CRs, update rxHead
TMPGDB=$(mktemp /tmp/gdb_inject.XXXXXX)

cat > "$TMPGDB" <<'HEADER'
set rxHead = 0
set rxTail = 0
set lineLen = 0
set $h = 0
set $buf = (uint8_t *)&UserRxBufferFS
HEADER

# Write each command + CR
for CMD in "$@"; do
    for (( i=0; i<${#CMD}; i++ )); do
        BYTE=$(printf '%d' "'${CMD:$i:1}")
        echo "set \$buf[\$h] = $BYTE" >> "$TMPGDB"
        echo "set \$h = (\$h + 1) % 512" >> "$TMPGDB"
    done
    # Append CR (0x0D) for KEY_ENTER
    echo "set \$buf[\$h] = 13" >> "$TMPGDB"
    echo "set \$h = (\$h + 1) % 512" >> "$TMPGDB"
done

cat >> "$TMPGDB" <<'FOOTER'
set rxHead = $h
detach
FOOTER

arm-none-eabi-gdb -nx -batch \
  -ex "set mem inaccessible-by-default off" \
  -ex "target extended-remote /dev/ttyBmpGdb" \
  -ex "monitor swdp_scan" \
  -ex "attach 1" \
  -ex "file ./build/stepper_sc.elf" \
  -x "$TMPGDB" \
  2>&1 | grep -v "^Python\|^arm-none\|^Could not\|^Limited\|^Suggest\|warning:\|^$"

rm -f "$TMPGDB"
