# stepper_sc User Manual

## Boot Sequence

1. Power on / reset (`reset` command clears screen first)
2. Buzzer plays **V** (morse: `...-`) — blocking
3. Clear screen + boot banner with git hash and build date
4. Parameter dump (from EEPROM)
5. Buzzer plays **G** (morse: `--.`) — blocking
6. Prompt `>` appears — CDC commands work from here
7. RX buffer flush (discards any minicom init strings)
8. Buzzer plays **Z** (morse: `--..`) — non-blocking, you can type during it
9. **3 second delay** — buttons disabled, endstops active
10. **Input self-test** — reads 5 inputs (4 buttons + ES_R)
    - ES_L is excluded — motor may be parked on home switch after homing.
    - All clear → buzzer plays **OK** (morse: `--- -.-`) → buttons enabled → system ready
    - Any input stuck LOW → prints which input is stuck (e.g. `STUCK: ES_L`) → buzzer plays **CQ CQ CQ DE LZ1CCM** → 2s pause → re-checks → repeats until fault cleared

### Input Self-Test Fault Loop

If a button is physically stuck, a cable is shorted, or an endstop is blocked at power-on,
the system refuses to enable buttons and loops the CQ call until the problem is fixed.
This prevents unintended motor movement from a stuck jog button at startup.

Buttons can also be manually controlled via CDC commands (`buttons on` / `buttons off`)
at any time, regardless of boot state.

## CDC Terminal Commands

Connect via serial terminal (minicom, screen, etc.) at any baud rate (USB CDC).

### Movement

| Command | Description | Parameter |
|---------|-------------|-----------|
| `mover <mm>` | Move CW (positive) | distance in mm |
| `movel <mm>` | Move CCW (negative) | distance in mm |
| `move <mm>` | Move by signed distance | + = CW, - = CCW |
| `steps <n>` | Move by raw step count | signed integer |
| `stop` | Decelerate and stop | — |
| `home` | Homing procedure — find ES_L, backoff, park | — |
| `range` | Measure travel range between endstops; requires `home`; returns to 0.00 | — |
| `moveto <mm>` | Absolute positioning; requires `home` + `range`; beeps on invalid target | position in mm |

### Homing Procedure (`home`)

1. **Approach** ES_L at `homespd` mm/s CCW — debounced polling (10×5ms LOW)
2. **Settle** 500ms — wait for mechanical vibrations
3. **Backoff** at `homespd/10` mm/s CW — debounced polling (10×5ms HIGH)
4. **Park** `homeoff` steps CW from switch edge (default 400)

If already on ES_L at start: skips approach and settle, goes directly to backoff + park.

EXTI endstops are disabled during homing — uses GPIO polling with debounce
to avoid EMI false triggers.

### Parameters

| Command | Description |
|---------|-------------|
| `set mmpsmax <f>` | Max velocity (mm/s) — top speed of ramp |
| `set mmpsmin <f>` | Min velocity (mm/s) — start/end speed of ramp |
| `set dvdtacc <f>` | Acceleration (mm/s²) |
| `set dvdtdecc <f>` | Deceleration (mm/s²) — can differ from accel for asymmetric ramps |
| `set jogmm <f>` | Jog distance (mm) — used by jog buttons |
| `set stepmm <f>` | Step distance (mm) — used by step buttons |
| `set spmm <n>` | Steps per mm — depends on driver microstepping and lead screw pitch |
| `set dirinv <0/1>` | Invert DIR pin — compensates for optocoupled driver polarity |
| `set homespd <f>` | Homing approach speed (mm/s) — backoff is 1/10 of this |
| `set homeoff <n>` | Homing offset from switch (steps) — park distance after backoff |
| `set debug <n>` | Debug flags (bitfield) — bit0: verbose button messages |
| `params` | Show all current parameters |
| `save` | Save parameters to EEPROM (persists across resets) |

### Input Control

| Command | Description |
|---------|-------------|
| `buttons on` | Enable button inputs (default after boot self-test passes) |
| `buttons off` | Disable button inputs — EXTI events ignored |
| `endstops on` | Enable endstop inputs (default always on) |
| `endstops off` | Disable endstop inputs — **use with caution** |

### Morse

| Command | Description |
|---------|-------------|
| `morse <text>` | Play text in morse code — non-blocking, letters + digits + spaces |

### Diagnostics

| Command | Description |
|---------|-------------|
| `diag_inputs` or `di` | Toggle input diagnostics — buttons/endstops print name only, no motor movement |
| `diag_outputs` or `do` | PULSE+DIR test loop — 16000 steps each direction @ 1kHz. **Password protected** (`motorola`). Reset to stop |
| `combo` | Run 4 test moves: triangle L, triangle R, trapezoid L, trapezoid R |
| `dump` | Debug variable dump |
| `uptime` | Print milliseconds since boot |
| `cls` | Clear terminal screen |
| `reset` | Software reset (NVIC_SystemReset) |
| `help` | Print command list |

## Physical Buttons

Active low (press connects to GND). External 10 kΩ pull-up resistors to 3.3 V on PCB.
Internal MCU pull-ups also enabled (GPIO_PULLUP) as a secondary safety measure — harmless in parallel with external resistors.
All 6 input pins (buttons + endstops) configured for **RISING + FALLING** edges in CubeMX.
Buttons start **disabled** at boot — enabled after input self-test passes (morse OK).

| Button | Pin | Short Press | Long Hold (>300ms) |
|--------|-----|-------------|---------------------|
| JOG L | PA6 | Jog CCW by `jogmm` with ramps | Continuous CCW at `mmpsmax` until release |
| JOG R | PA7 | Jog CW by `jogmm` with ramps | Continuous CW at `mmpsmax` until release |
| STEP L | PB0 | Move CCW by `stepmm` with ramps | — |
| STEP R | PB1 | Move CW by `stepmm` with ramps | — |

Jog button release triggers immediate stop — `Stepper_Stop()` called directly in ISR. After release, a 50ms lockout (`DEBOUNCE_REL_MS`) prevents mechanical bounce from re-triggering a press event (which would cause a spurious second movement).

## Button Combo: Homing Gesture

When the machine is stopped at ES_L (after hitting the left endstop):

1. **Hold JOGL** (left jog button)
2. **Press JOGR** (right jog button) while JOGL is held

→ Triggers the full homing procedure (`home`)

**During the procedure**: both buttons must remain held — 300ms tolerance for non-simultaneous release.

**On button release**: procedure aborts → `home: ABORT`

**On success**: 1 second pause → 150ms beep → `\r\n   0.00 >`

Note: homing progress messages (`home: approach...`, `home: ES_L confirmed...` etc.) are only printed when `debug & 1` is set. `home: ABORT` is always printed.

## Endstops

| Endstop | Pin | Behavior |
|---------|-----|----------|
| ES_L | PA3 | Immediate deceleration stop. Falling edge (active low) |
| ES_R | PA4 | Immediate deceleration stop. Falling edge (active low) |

Endstops have a **30ms edge lockout** in normal mode — `Stepper_Stop()` fires on the falling edge (active low), repeated edges within 30ms are ignored (EMI / vibration protection).
In `diag_inputs` mode, endstops have 30ms debounce and only print — no stop.
During `home`, EXTI endstops are disabled — GPIO polling with debounce is used instead.

## Endstop Direction Blocking

After an endstop hit, movement in that direction is blocked:

- **ES_L hit** → CCW (left) jog blocked, CW (right) jog allowed
- **ES_R hit** → CW (right) jog blocked, CCW (left) jog allowed

Block clears automatically when jogging in the opposite direction, or on any CDC move command.
Step buttons remain blocked in both directions after endstop hit.

## Buzzer

Self-oscillating buzzer on PB15 (active low).

- **Boot**: morse V → G → Z → (3s) → OK sequence
- **Boot fault**: CQ CQ CQ DE LZ1CCM loop if inputs stuck
- **Button/endstop press**: 50ms beep on any EXTI event
- **`morse` command**: arbitrary text playback, non-blocking

## LED (PC13)

- **Normal**: toggles every 500ms (1Hz heartbeat) — confirms main loop is running
- **Homing/combo**: fast toggle 50ms — indicates blocking operation in progress
- **Error handler**: fast blink 50ms — indicates crash
- **GDB attached**: LED frozen — CPU halted
- **Morse**: blinks in sync with buzzer during boot sequence

## Velocity Profile

Trapezoidal/triangular velocity profile with configurable acceleration and deceleration.

- Short moves (distance < ramp distance): **triangle** profile — accel then decel, never reaches max speed
- Long moves: **trapezoid** profile — accel → constant speed → decel
- Asymmetric ramps supported: set `dvdtdecc` different from `dvdtacc`

## Position Tracking

After a successful `home` command, the prompt shows absolute position in mm:
```
   0.00 >
```

Before homing, position is unknown:
```
XXXX.XX >
```

Position updates automatically when the motor stops (from any source: jog, step, CDC command, endstop). On every motor stop (busy→idle transition) the firmware prints **two blank lines** before the prompt (`\r\n\r\n`). Blocking commands (`home`, `range`, button combo) end the same way.

Prompt format is `%7.2f > ` (7-character width, aligns with `XXXX.XX >`).

## EEPROM

Parameters persist across power cycles. Stored in internal flash (sectors 6 & 7)
with wear-leveling. Use `save` command after changing parameters.

11 parameters stored: mmpsmax, mmpsmin, dvdtacc, dvdtdecc, jogmm, stepmm, spmm, dirinv, homespd, homeoff, debug.

## Safety Notes

- `diag_outputs` is password protected — moves motor without ramps at constant speed
- Endstops always active in normal mode (no debounce, immediate stop)
- `stop` command triggers smooth deceleration from any state
- Homing uses debounced GPIO polling (not EXTI) to avoid EMI false triggers
- EMI from solar inverters / stepper drivers can cause false button/endstop triggers without hardware filtering (external pull-ups + caps recommended on PCB)
- Buttons disabled at boot until input self-test passes
- Jog release stops motor immediately (ISR-level) + 50ms lockout suppresses bounce re-press
- Endstop 30ms edge lockout in normal mode — EMI / vibration protection
- Endstop direction blocking prevents re-entering the same endstop
- Endstop direction check in ISR (snapA) — blocked jogs rejected before queuing
- `moveto <mm>` requires both `home` AND `range` to have been run — out-of-range targets (< 0 or > range) produce a 100ms beep and are rejected without movement
