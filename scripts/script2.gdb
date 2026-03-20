set remote exec-file ./build/scstepper.elf
set max-value-size unlimited
source ./PyCortexMDebug/scripts/gdb.py
svd_load ./initcfg/STM32F411.svd
source ./initcfg/project.gdb

define ld
file ./build/scstepper.elf
set remote exec-file ./build/scstepper.elf
end

define fl
fwc
load ./build/scstepper.hex
fwcheck
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

define fwc
ld
fwcheck
end
