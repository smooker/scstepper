# Programmer Guide — scstepper firmware

Practical reference for anyone working on this codebase. Covers
environment, workflow, architecture, and hard-won lessons.

---

## 1. Environment

### Hardware
| Device | Role | Symlink |
|--------|------|---------|
| Black Magic Probe (BMP) | GDB server + SWD flash | `/dev/ttyBmpGdb` (GDB), `/dev/ttyBmpUart` (UART) |
| STM32F411CEU6 target | Firmware runs here | — |
| FX2 Saleae clone | Logic analyzer (sigrok) | `/dev/ttyACMTarg` (CDC serial) |

Symlinks are set by `scripts/99-steppersys.rules`. Install once:
```bash
sudo make sysinstall
```

### Host toolchain
- `arm-none-eabi-gcc` — compiler
- `arm-none-eabi-gdb` — debugger
- `arm-none-eabi-nm` / `arm-none-eabi-size` — ELF tools
- `sigrok-cli` + `pulseview` — logic analyzer
- GDB Dashboard (`initcfg/gdbinit`) — visual GDB TUI

### GDB Dashboard install (once per user)
```bash
make userinstall
```
Copies `initcfg/dot-gdbinit-for-home` → `~/.gdbinit` and the
`scstepper_panel.py` Dashboard module → `~/.gdbinit.d/`.

---

## 2. Repository layout

```
Core/Src/main.c         — main loop, CDC CLI, button/endstop EXTI ISR
Core/Src/stepper.c      — stepper state machine, ramp tables, EEPROM params
Core/Inc/stepper.h      — param limits, EEPROM addresses, public API
Core/Inc/defines.h      — GPIO pin macros, semaphore bits, pulse timing
build/                  — compiler output (gitignored except .gitkeep)
captures/               — sigrok .sr files committed to repo
docs/                   — AUDIT.md, TODO.md, this file
initcfg/                — GDB init, dashboard panel, SVD, QtCreator config
scripts/                — go.sh, go_gdb.sh, post_cubemx.sh, udev rules
Makefile / Makefile.template — build system (see §3)
```

---

## 3. Build

```bash
make              # build → build/scstepper.{elf,hex,bin}
make clean        # wipe build/
make size         # show flash/RAM breakdown
make check        # verify repo integrity (see §4)
```

Binary is at `-O0` (debug default). For size optimization see `docs/TODO.md`.

---

## 4. make check — three invariants

Run `make check` before every commit and after every CubeMX regeneration.
It verifies three things and fails fast with a clear message:

```
[OK]  Makefile matches template
[OK]  GPIO_MODE_IT_RISING_FALLING patch applied
[OK]  GDB scripts: no stale firmware symbol references
```

**Why each check exists:**

| Check | Failure mode without it |
|-------|------------------------|
| Makefile == template | CubeMX overwrites Makefile, losing custom targets silently |
| GPIO IT_RISING_FALLING | CubeMX regenerates RISING-only → button release events lost |
| GDB script stale symbols | Removed C vars stay in `.gdb`/`.py` → runtime GDB error, not caught by compiler |

**Rule:** when removing a firmware variable, add its name to the grep
pattern in the `check` target of `Makefile` (and `Makefile.template`).

---

## 5. CubeMX workflow

CubeMX overwrites `Makefile` and can reset GPIO interrupt modes.
After every `.ioc` change:

```bash
# In CubeMX: Project → Generate Code
bash scripts/post_cubemx.sh   # restores Makefile + patches GPIO modes
make check                    # verify
make                          # build
```

`post_cubemx.sh` does three things:
1. `cp Makefile.template Makefile` — restore custom targets
2. `sed` — patch `GPIO_MODE_IT_RISING` / `_FALLING` → `IT_RISING_FALLING`
3. `make check` — confirm

**Never edit `Makefile` directly** — edit `Makefile.template` and let
`post_cubemx.sh` copy it. Makefile is a generated artifact.

---

## 6. Flash & debug

### Flash only (no GDB session)
```bash
scripts/go.sh flash       # build + flash via BMP, runs compare-sections
```

### Interactive GDB session
```bash
scripts/go_gdb.sh         # launches arm-none-eabi-gdb with dashboard
```

Inside GDB:
```gdb
ag          — attach to target (SWD scan → attach → fwcheck)
ld          — load ELF to flash, verify with compare-sections
fwcheck     — verify running flash matches ELF on disk (also runs on ag)
c           — continue
Ctrl+C      — halt
```

`ag` automatically calls `fwcheck` after attach. If flash != ELF, you see:
```
=== FWCHECK FAILED — flash != ELF ===
  Section .text, range ...: MIS-MATCHED!
  Run 'ld' to reflash.
```

### GDB convenience commands
| Command | Description |
|---------|-------------|
| `st` | Motor state: stepperState, posSteps, posHomed, range, semaphore |
| `params` | All motor parameters (mirrors CDC `params` command) |
| `rxbuf` | CDC RX ring buffer occupancy + raw bytes |
| `mem_regions` | STM32F411 memory map |
| `inject COMMAND` | Write command into CDC RX buffer; firmware executes on `c` |
| `pr` | Dump EEPROM flash pages raw (debug EEPROM layout) |

**`inject` example** — test a command without typing in minicom:
```gdb
inject set mmpsmax 80
c
```
Firmware processes it on the next main-loop tick.

---

## 7. Serial console

```bash
scripts/minicom.sh        # /dev/ttyACMTarg 115200 8N1
scripts/listen.sh         # cat /dev/ttyACMTarg (read-only)
```

Key CDC commands:
```
params              — show all parameters
set <name> <value>  — set parameter (validated, not saved)
save                — save parameters to EEPROM (blocked if validation fails)
initeeprom          — write defaults to EEPROM (hidden/installer only)
home                — run homing sequence
range               — measure travel range
jog / step buttons  — physical buttons or: move <mm>, movel/mover <mm>
dump                — dump internal state variables
```

---

## 8. Logic analyzer

```bash
scripts/go.sh capture     # capture PULSE+DIR from FX2 → captures/capture_both.sr
scripts/go.sh speed       # add computed speed channel → *_speed.sr
scripts/go.sh view        # open in pulseview with stepper_motor + counter decoders
scripts/go.sh all         # flash + capture + speed + view in one shot
```

Captures are committed to the repo — they are part of the hardware validation
record. When making a motion change, run a new capture and commit it.

---

## 9. Firmware architecture

### Two files, clear split
- `stepper.c` — physics: ramp table generation, ISR (TIM2), EEPROM R/W, parameter validation
- `main.c` — everything else: CDC CLI, button/endstop EXTI, jog state machine, homing

### Single source of truth for GPIO
All GPIO pin state decisions use **snapshots taken at EXTI ISR entry**:
```c
volatile uint32_t snapA = 0;  // GPIOA IDR — ES_L, ES_R, JOGL, JOGR
volatile uint32_t snapB = 0;  // GPIOB IDR — STEPL, STEPR
```
Written once in `HAL_GPIO_EXTI_Callback`, read everywhere else.
`HAL_GPIO_ReadPin()` is only used at boot (before EXTI is active, snapA=0).

**Never add a new `HAL_GPIO_ReadPin()` call** outside the boot stuck-input
check. Use `snapA`/`snapB`.

### ISR safety
TIM2 ISR and all EXTI ISRs share `stepsRemaining`, `decelIndex`, `stepperState`.
`FixNVIC_Priorities()` sets all of them to priority 0 — same-priority ISRs
cannot preempt each other on Cortex-M4. No `__disable_irq()` inside `Stepper_Stop()`.

`evtFlags` read-clear in `ProcessEvents()` is atomic:
```c
__disable_irq();
uint32_t flags = evtFlags;
evtFlags &= ~flags;
__enable_irq();
```

### Parameter system
- All 11 parameters in `motorParams` struct (float/uint union for EEPROM)
- Individual range validation: `PARAM_MIN_*` / `PARAM_MAX_*` in `stepper.h`
- Cross-parameter consistency: `Stepper_ValidateParams()` — checks ramp overflow,
  homespd vs mmpsmax/mmpsmin, step frequency vs driver limit
- `Stepper_SaveParams()` calls `ValidateParams()` first — blocked if warnings
- EEPROM blank detection: magic key at `EE_ADDR_MAGIC` (`0x5AFEC0DE`)
- Boot: if EEPROM blank → 3s CDC prompt → `initeeprom` or skip

### Buzzer feedback
`buzzRequest = 1` from any context; main loop fires a 50ms beep.
Beeps on: jog press, step press, endstop hit, home complete,
`save` success, `initeeprom`, blocked CDC move.

---

## 10. Lessons learned (from AUDIT.md)

### GDB scripts are not compiled
Removing a C variable from firmware does not break `.gdb`/`.py` files —
those only fail at runtime when you run the specific GDB command.
`make clean` does not help. `make check` does.

### snapshots beat live reads
Multiple `HAL_GPIO_ReadPin()` calls at different points in time can give
inconsistent state. Take one IDR snapshot at ISR entry, use it everywhere.

### Shadow flags hide hardware state
`esBlocked` was a software flag that duplicated information already in
the hardware. It drifted out of sync. Removed. Hardware state via snapA is
always authoritative.

### decelIndex boundary
Always check the index before reading an array, not after:
```c
// wrong: UB if decelIndex went negative
currentPeriod = decelTable[decelIndex]; decelIndex--;
if (decelIndex < 0) ...

// right
if (decelIndex < 0) { currentPeriod = maxPeriod; }
else { currentPeriod = decelTable[decelIndex--]; }
```

### HAL_Delay() in main loop
Blocks USB CDC TX, button polling, buzzer, everything. Never use it outside
boot. Use `HAL_GetTick()` deltas for all timing. Use `buzzRequest` for beeps.

### Makefile is generated
Edit `Makefile.template`, not `Makefile`. CubeMX will overwrite `Makefile`.

---

## 11. TODO (open items)

See `docs/TODO.md` for full details:
- Unified ramp table (index 0 = center, symmetric accel/decel)
- Soft position limits (post-home, post-range)
- WDT decision (unattended operation?)
- `-Os` size optimization + dead code audit
- `TIM2` AutoReload preload enable
- opencm3 / LL branch (eliminate CubeMX dependency)

---

*Last updated: 2026-03-20*
