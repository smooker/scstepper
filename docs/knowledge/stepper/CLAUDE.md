# stepper — Project Context

**Location**: `/home/claude/work/stepper/`
**Purpose**: STM32F411CEU6 (Black Pill) stepper motor controller

## Structure
- `firmware/` — active (lookup table, not committed); `firmware.bck/` — Lindstedt version (git)
- `pcb/stepper.kicad_pro` + `pcb/stepper.kicad_sch` — KiCad 9 (components placed, no wires yet)
- `doc/build-and-debug.md` — build, flash, debug, CLI + Black Pill ASCII pinout

## KiCad IPC Status
- **9.0.2**: `view.redraw` via external IPC did NOT repaint live — confirmed broken
- **9.0.6**: live repaint WORKS — `demo_orbit.py` animates in real time

## PCB Layout Patterns
- **kipy bounding box**: `board.get_item_bounding_box(fp)` → `Box2` with `.pos`/`.size` (Vector2), both in nm
- **Equal body spacing**: fix leftmost anchor, gap = (total_span - sum_of_widths) / (n-1)
- **SOT-23 pads at rot=0**: pad1(B) upper-left, pad2(E) lower-left, pad3(C) right
- **NPN buffer group layout** (Q rot=0, R_base rot=90, R_pulldown rot=-90): no wire crossings
- **Polarity**: all electrolytic caps rot=+90 puts pad1(+) at bottom, pad2(-) at top
- **KiCad rotation**: +90=CW on screen, -90=CCW on screen
- **Pin alignment**: find target pad y via `get_pads()` filtering by net name

## PCB / KiCad Pipeline
- **DO NOT hand-roll S-expressions** — use `kicad-sch-api`
- **Workflow**: `stepper.net` → `netlist_to_schematic.py` → `stepper.kicad_sch` → KiCad ERC → PCBnew
- **venv**: `/home/claude/work/stepper/venv/`
- KiCad: `kicad.sh` wrapper; wxPython compiled from source
- KiCad format rules: `../stepper_sc/KICAD_RULES.md`

## kicad-sch-api WARNING
- **DO NOT use to save .kicad_sch files** — strips `lib_symbols`, causes segfault
- Safe for READ operations only
- For footprint changes: direct Edit on .kicad_sch file (string replace)

## kicad-sch-api Key Facts
- `sch.get_component_pin_position(ref, pin_num)` → Point
- `sch.list_component_pins(ref)` → [(pin_name, Point), ...]
- `sch.no_connects.add(position=(x,y))` — NOT `add_no_connect()`
- Power refs MUST be `#PWRnnn` format

## Black Pill v3.1 Pin Mapping (USB on LEFT)
```
Front (1-20): B12,B13,B14,B15,A8,A9,A10,A11,A12,A15,B3,B4,B5,B6,B7,B8,B9,5V,GND,3V3
Back  (21-40): VBAT,C13,C14,C15,RST,A0,A1,A2,A3,A4,A5,A6,A7,B0,B1,B2,B10,3V3,GND,5V
```
Key signals: pin3=PB14(DIR), pin4=PB15(BUZZ), pin29=PA3(ES_L), pin30=PA4(ES_R),
pin32=PA6(JOGL), pin33=PA7(JOGR), pin34=PB0(STEPL), pin35=PB1(STEPR), pin37=PB10(PULSE)
Connectors: J11A=front(1-20), J11B=back(21-40)

## Hardware (confirmed BOM)
- **Driver**: CWD556, opto-isolated PUL/DIR, 4A, 24V
- **NPN buffer**: BC547, R_base=1k, R_bgnd=10k, R_col=470 to +5V (Q1=PUL, Q2=DIR)
- **Power**: 78M05 (24V→5V); C1=47uF+C2=100nF in, C3=100uF+C4=100nF out, C5=100nF BP VIN
- **Terminals**: J1=PWR, J2=PUL, J3=DIR, J4-J10=signals

## Firmware Key Facts
- **Toolchain**: arm-none-eabi-gcc 14.x
- **Build**: `cd firmware && make clean && make` → `build/stepper.{elf,hex,bin}`
- **Motion**: `BuildRampTables()` precomputes accel/decelTable[] (512 entries); ISR does table lookup
- **Timer**: TIM2 CH3 → PB10=STEP pulse (PWM 50us), PB14=DIR
- **USB CDC**: virtual COM, printf via `_write()`, RX ring buffer
- **EEPROM**: flash sectors 6&7, 7 params
- **Inputs**: PA3=ES_L, PA4=ES_R, PA6=JOGL, PA7=JOGR, PB0=STEPL, PB1=STEPR (all EXTI)

## Debug
- **Programmer**: Black Magic Probe, `/dev/ttyBmpGdb`
- **GDB**: `./go_gdb.sh` → `arm-none-eabi-gdb -x script2.gdb build/stepper.elf`
- **Custom commands**: `ag`=connect BMP+attach, `ld`=flash+verify
- **SVD**: `firmware/STM32F411.svd` + `firmware/PyCortexMDebug/`

## Known Issues
- `CONST → DECEL` ISR: sets `decelIndex = 0` (slow end) instead of fast end — needs fix
