set remote exec-file ./build/scstepper.elf
set max-value-size unlimited
source ./PyCortexMDebug/scripts/gdb.py
svd_load ./initcfg/STM32F411.svd
source ./initcfg/project.gdb

define ld
file ./build/scstepper.elf
set remote exec-file ./build/scstepper.elf
end
document ld
Load ELF symbols into GDB (no flash write).
Run after 'ag'. Required before 'st', 'params', 'inject', etc.
end

define fl
load ./build/scstepper.hex
fwcheck
end
document fl
Flash build/scstepper.hex to target, then verify with fwcheck.
Run 'ag' first. Does NOT load ELF symbols — run 'ld' or 'fwc' after.
end

define ag
dashboard -style discard_scrollback False
set mi-async on
set mem inaccessible-by-default off
target extended-remote /dev/ttyBmpGdb
monitor swdp_scan
attach 1
monitor traceswo
end
document ag
Attach to target via BMP (SWD scan, attach, enable traceswo).
No symbols loaded — run 'fwc' next to load ELF and verify flash.
end

define fwc
ld
fwcheck
end
document fwc
Load ELF symbols ('ld') then compare all flash sections against ELF.
Use before 'c' to confirm flash matches build. Use after 'fl' to verify.
end
