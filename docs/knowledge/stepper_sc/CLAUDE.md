# stepper_sc — Project Context

**GitHub**: `git@github-scstepper:smooker/scstepper.git` (branch `routing`)
**Purpose**: Clean re-implementation of stepper with firmware-driven pipeline
**Milestone**: `v0.4-buttons` — homing, endstops, EEPROM params, boot self-test, audit report (2026-03-13)

## Firmware (stepper_sc/firmware/)
- **Docs**: `firmware/FIRMWARE.md` — full documentation
- **Target**: `TARGET = stepper_sc` in Makefile → `build/stepper_sc.{elf,hex,bin}`
- **HAL fix**: `-Wno-error=unused-parameter` in CFLAGS; `post_cubemx.sh` re-applies after regen
- **Debug**: `go_gdb.sh` → `arm-none-eabi-gdb -x .gdbinit -x script2.gdb`
- **GDB commands**: `ag` = attach BMP, `ld` = load+verify firmware
- **SVD**: PyCortexMDebug + STM32F411.svd for peripheral register inspection
- **`.gdbinit.tohome`** — install to ~/.gdbinit on target machine for auto-load

## Pipeline (all working, ERC clean)
`firmware/Core/Inc/main.h` → `fw_to_net.py` → `howto_sch_and_then_api/stepper_sc.net` → `stepper_sc_gen.py` → `stepper_sc/stepper_sc.kicad_sch`

## Key Scripts
- `fw_to_net.py` — parse main.h GPIO defines → KiCad Format-E netlist (24 components, 17 nets)
- `howto_sch_and_then_api/stepper_sc_gen.py` — netlist → schematic (POSITIONS dict uses R3/R4 for DIR buffer)
- `stepper_sc/pcb_place.py` — place footprints via kipy IPC API
- `stepper_sc/pcb_edge.py` — draw Edge.Cuts 90×120mm (x=25..115, y=25..145)
- `stepper_sc/demo_orbit.py` — demo: orbit first footprint in circle

## kipy IPC — CRITICAL: footprint placement
- **NEVER** use `fp.proto.position.x_nm = value` — bypasses property setter, only moves anchor `+`
- **ALWAYS** use `fp.position = Vector2.from_xy_mm(x, y)` — moves anchor + ALL children
- **ALWAYS** use `fp.orientation = Angle.from_degrees(rot)` — NOT `fp.proto.orientation.value_degrees`
- `Vector2.from_xy_mm(x, y)` — correct method name (NOT `from_mm`)
- `Angle.from_degrees(deg)` — correct method name

## PCB State
- KiCad 9.0.6 — live repaint CONFIRMED working
- Board: 90×120mm, Edge.Cuts at (25,25)..(115,145)
- All 24 footprints PROPERLY placed (real bodies, not just anchor marks)
- J4-J10 at x=30, rot=90° (left edge column); J11/J12 at x=95/108, rot=0° (Black Pill headers)
- Power row at y=35; PUL buffer at y=70; DIR buffer at y=90
- **Run scripts**: from `stepper_sc/stepper_sc/` dir: `KICAD_API_SOCKET=ipc:///home/claude/tmp/kicad/api.sock python3 pcb_place.py`
- **Component refs**: R1/R2 (PULSE buffer) + R3/R4 (DIR buffer) — NOT R4/R5
