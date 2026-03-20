# Build and Debug Guide

## Prerequisites

### ARM Toolchain
The project requires `arm-none-eabi-gcc` (GCC 14.x tested).

On Gentoo, the toolchain lives at:
```
/usr/x86_64-pc-linux-gnu/arm-none-eabi/gcc-bin/14/
```

Ensure it is in PATH:
```bash
export PATH="/usr/x86_64-pc-linux-gnu/arm-none-eabi/gcc-bin/14:$PATH"
```

Verify:
```bash
arm-none-eabi-gcc --version
```

### Chroot Setup
If building inside a chroot (e.g. `/chroot/claude`), copy the toolchain from the host:

```bash
# binaries
rsync -av /usr/bin/arm-none-eabi-* /chroot/claude/usr/bin/
rsync -av /usr/x86_64-pc-linux-gnu/ /chroot/claude/usr/x86_64-pc-linux-gnu/
rsync -av /usr/lib/gcc/arm-none-eabi/ /chroot/claude/usr/lib/gcc/arm-none-eabi/

# compiler plugins (cc1, etc.)
rsync -av /usr/libexec/gcc/arm-none-eabi/ /chroot/claude/usr/libexec/gcc/arm-none-eabi/

# shared libraries (binutils)
rsync -av /usr/lib64/binutils/arm-none-eabi/ /chroot/claude/usr/lib64/binutils/arm-none-eabi/
```

---

## Build

```bash
# full clean build
make clean && make

# incremental build
make
```

### Output files
```
build/stepper_sc.elf   — ELF with debug symbols (for GDB)
build/stepper_sc.hex   — Intel HEX (for flashing)
build/stepper_sc.bin   — raw binary
build/stepper_sc.map   — linker map
```

### Expected size (lookup table build, -O0)
```
text: ~79KB   data: ~900B   bss: ~14KB
```

---

## Flash and Debug

### Hardware
- **Programmer**: Black Magic Probe (BMP) connected via `/dev/ttyBmpGdb`
- **Interface**: SWD (PA13=SWDIO, PA14=SWCLK)

### GDB Setup
`initcfg/gdbinit` — GDB Dashboard (visual TUI: source, registers, disassembly).
`initcfg/project.gdb` — project-specific commands.
`PyCortexMDebug/` — SVD peripheral register viewer (git submodule).

Install once per user:
```bash
make userinstall
```

### Launch GDB
```bash
scripts/go_gdb.sh
```

### GDB commands

**Connection & flash:**

| Command | Action |
|---------|--------|
| `ag` | Connect BMP, SWD scan, attach target (no symbols) |
| `ld` | Load ELF symbols into GDB (no flash write) |
| `fwc` | `ld` + compare-sections: verify flash matches ELF on disk |
| `fl` | Flash `build/scstepper.hex` to target + verify with fwcheck |

**Inspection (requires `ld` or `fwc` first):**

| Command | Action |
|---------|--------|
| `st` | Motor state: stepperState, posSteps, posHomed, semaphore… |
| `params` | All motor parameters |
| `rxbuf` | CDC RX ring buffer occupancy + raw bytes |
| `mem_regions` | STM32F411 memory map |
| `eecheck` | EEPROM page status (raw flash) + eepromStatus from firmware |
| `inject CMD` | Write CDC command into RX buffer (firmware executes on `c`) |
| `svd TIM2` | STM32 peripheral registers via SVD |

### Typical session
```
(gdb) ag          ← attach (no symbols yet)
(gdb) fwc         ← load symbols + check flash vs ELF
(gdb) c           ← run
Ctrl+C            ← halt
(gdb) st          ← inspect state
```

Flash new firmware:
```
(gdb) ag
(gdb) fwc         ← see what's currently on target
(gdb) fl          ← flash + verify
(gdb) c
```

---

## Black Pill v3.1 Pinout (WeAct STM32F411CEU6)

USB-C connector on the LEFT. DIP-style numbering: pins 1–20 on the front row
(towards you), pins 21–40 on the back row right-to-left (wraps like a DIP IC).

```
    USB end                              non-USB end
      ↓                                      ↓
  ┌───┴──────────────────────────────────────┴───┐
  │  [USB-C]   STM32F411CEU6   WeAct v3.1        │
  └──┬───────────────────────────────────────┬───┘
     │                                       │
     │  FRONT ROW  (pins 1–20, left→right)   │
     ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●
     1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20
    B12 B13 B14 B15 A8 A9 A10 A11 A12 A15 B3 B4 B5 B6 B7 B8 B9 5V GND 3V3

     ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●
    40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21
    5V GND 3V3 B10 B2 B1 B0 A7 A6 A5 A4 A3 A2 A1 A0 RST C15 C14 C13 VBAT
     │  BACK ROW  (pins 21–40, right→left = pin 21 at non-USB end)  │
```

**Key signals for this project:**

| BP Pin | Signal | GPIO | Role |
|--------|--------|------|------|
| 3  | PB14   | PB14 | DIR output → NPN buffer → CWD556 DIR+ |
| 4  | PB15   | PB15 | BUZZ output |
| 18 | 5V     | —    | Power input (from 78M05) |
| 19 | GND    | —    | Ground |
| 20 | 3V3    | —    | 3.3V ref |
| 29 | PA3    | PA3  | ES_L — endstop left, active LOW |
| 30 | PA4    | PA4  | ES_R — endstop right, active LOW |
| 32 | PA6    | PA6  | JOGL — jog left button, active LOW |
| 33 | PA7    | PA7  | JOGR — jog right button, active LOW |
| 34 | PB0    | PB0  | STEPL — step left button, active LOW |
| 35 | PB1    | PB1  | STEPR — step right button, active LOW |
| 37 | PB10   | PB10 | PULSE output (TIM2 CH3 PWM → CWD556 PUL+) |
| 38 | 3V3    | —    | 3.3V ref |
| 39 | GND    | —    | Ground |
| 40 | 5V     | —    | Power input (from 78M05) |

> Schematic uses two 1×20 sockets: **J11A** = front row (BP pins 1–20),
> **J11B** = back row (BP pins 21–40, J11B pin 1 = BP pin 21).

---

## Pin Reference

| Signal     | Pin  | Direction | Description              |
|------------|------|-----------|--------------------------|
| PULSE      | PB10 | Output    | STEP pulse (TIM2 CH3 PWM)|
| DIR        | PB14 | Output    | Direction                |
| BUZZ       | PB15 | Output    | Buzzer                   |
| LED_USER   | PC13 | Output    | User LED                 |
| ES_L       | PA3  | Input     | Endstop left — active LOW, both edges, internal pull-up  |
| ES_R       | PA4  | Input     | Endstop right — active LOW, both edges, internal pull-up |
| BUTT_JOGL  | PA6  | Input     | Jog left — active LOW, both edges, internal pull-up      |
| BUTT_JOGR  | PA7  | Input     | Jog right — active LOW, both edges, internal pull-up     |
| BUTT_STEPL | PB0  | Input     | Step left — active LOW, both edges, internal pull-up     |
| BUTT_STEPR | PB1  | Input     | Step right — active LOW, both edges, internal pull-up    |
| USB DM/DP  | PA11/PA12 | USB  | USB CDC (virtual COM)    |
| SWDIO      | PA13 | Debug     | SWD data                 |
| SWCLK      | PA14 | Debug     | SWD clock                |

---

## USB Serial Terminal

The firmware exposes a USB CDC virtual COM port. Connect with any terminal at any baud rate.

Recommended:
```bash
./minicom.sh
# or
./listen.sh
```

### CLI commands
```
move <mm>          move by mm (positive = right, negative = left)
movel <mm>         move left by mm
mover <mm>         move right by mm
steps <n>          move by N steps
set mmpsmax  <f>   max velocity mm/s      (default: 50.0)
set mmpsmin  <f>   min velocity mm/s      (default: 1.0)
set dvdtacc  <f>   acceleration mm/s²     (default: 100.0)
set dvdtdecc <f>   deceleration mm/s²     (default: 80.0)
set jogmm    <f>   jog distance mm        (default: 1.0)
set stepmm   <f>   step distance mm       (default: 1.0)
set spmm     <n>   steps per mm           (default: 80)
params             dump current parameters
save               save parameters to EEPROM
dump               dump EEPROM vars and semaphore state
stop               decelerate to stop
cls                clear screen
uptime             show uptime in ms
reset              system reset
help               show command list
```
