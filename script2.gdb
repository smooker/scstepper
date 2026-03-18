set remote exec-file ./build/stepper_sc.elf
set max-value-size unlimited
source ./PyCortexMDebug/scripts/gdb.py
svd_load ./STM32F411.svd

define ld
file ./build/stepper_sc.elf
load ./build/stepper_sc.hex
set remote exec-file ./build/stepper_sc.elf
compare-sections
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
