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

### CRC patching (automatic)

`crc_patch.py` runs automatically as part of `make`. After `objcopy` generates
the BIN, it computes STM32 CRC32 over the entire 256KB program area
(Sectors 0-5, [0x08000000..0x0803FFFC)) and patches the last 4 bytes
(0x0803FFFC — just before the EEPROM region). BIN and HEX are updated in-place.

```
  CRC32: 0x810F7A01  (was 0xDEADBEEF)  @ 0x0803FFFC
```

The firmware checks this CRC as **the very first action at boot** using the
CRC32 hardware peripheral. Morse "." = OK. Morse "SOS" + halt = flash corrupt.

**Flash layout:**
```
0x08000000  Sectors 0-5  256KB  program (firmware + 0xFF padding + CRC word)
0x0803FFFC               4B     CRC32 word  ← .fw_crc section (FW_CRC region)
0x08040000  Sectors 6-7  256KB  EEPROM emulation  ← never in HEX, not touched by fl
```

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
| `st` | Key state: stepperState, posSteps, posHomed, range, snapA/B, txBusy, tim9Ms |
| `params` | All motor parameters (mirrors CDC `params` command) |
| `rxbuf` | CDC RX ring buffer (rxHead/rxTail/pending + first 64 bytes of rxRing) |
| `txbuf` | CDC TX ring buffer (txHead/txTail/txBusy/pending + txRing + txChunk) |
| `mem_regions` | STM32F411 memory map |
| `inject COMMAND` | Write command into rxRing; firmware executes on `c` |
| `fwcheck` | Verify running flash matches ELF on disk (compare-sections) |
| `eecheck` | Check EEPROM emulation region (page status + eepromStatus) |
| `pr` | Dump EEPROM flash pages raw (debug EEPROM layout) |

**`inject` example** — test a command without typing in minicom:
```gdb
inject set mmpsmax 80
c
```
Firmware processes it on the next main-loop tick.

**`inject` + EMI diagnostics** — exercise motor while monitoring endstop events:
```gdb
inject di
inject set debug 1
c
# ... let firmware enable diag mode ...
# Ctrl+C
inject moveto 3.13
c
# ... wait for move to complete ...
# Ctrl+C
inject moveto 0
c
```
With `diagMode ON` + `debug bit0`: endstop events print `ES_L hit` / `ES_R hit`
but do NOT stop the motor — safe for EMI diagnosis during motion.

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

## 9. Boot sequence

```
[power on / reset]
    │
    ├─ CRC32 check — STM32 CRC peripheral over [0x08000000..0x0803FF80)
    │     morse "E" (dit) = OK
    │     morse "SOS" + halt = flash corrupt
    │
    ├─ morse "V"  — boot started
    │
    ├─ USB enumeration (1200ms wait)
    │
    ├─ Stepper_LoadParams() — read EEPROM
    │     eepromStatus=0: "EEPROM OK — params loaded from flash"
    │     eepromStatus=2: "EEPROM has data but magic absent — type 'save' to confirm"
    │     eepromStatus=1: *** BLOCKING PROMPT *** "Init with defaults? [y/n]"
    │                      y → Stepper_InitDefaults() + beep
    │                      n → defaults in RAM only
    │
    ├─ banner + params dump
    │
    ├─ morse "G"  — ready
    │
    └─ morse "Z"  — main loop starts (non-blocking state machine)

buttons enabled
XXXX.XX >
```

**Normal boot output (terminal):**
```
params ok
  stepper init ok

===============================================
  stepper_sc  44b9a5e  2026-03-20_14:24
  STM32F411CEU6 @ 96 MHz
  type 'help' for commands
===============================================

-----------------------------------------------
  mmpsmax........:   1.000 mm/s
  mmpsmin........:   0.500 mm/s
  dvdtacc........:  50.000 mm/s2
  dvdtdecc.......: 100.000 mm/s2
  jogmm..........:   0.200 mm
  stepmm.........:   1.000 mm
  spmm...........:     400 steps/mm
  dirinv.........:       1 (inverted)
  homespd........:   0.500 mm/s
  homeoff........:      10 steps
  debug..........: 0x0000
-----------------------------------------------
  pulse_ticks....: 5000
  min_period.....: 240000 ticks (1.0 mm/s)
  max_period.....: 480000 ticks (0.5 mm/s)
-----------------------------------------------
  EEPROM OK — params loaded from flash
XXXX.XX >
buttons enabled
```

---

## 10. Firmware architecture

### Two files, clear split
- `stepper.c` — physics: ramp table generation, ISR (TIM2), EEPROM R/W, parameter validation
- `main.c` — everything else: CDC CLI, button/endstop EXTI, jog state machine, homing

### Single source of truth for GPIO
All GPIO pin state decisions use **snapshots taken at EXTI ISR entry**:
```c
volatile uint32_t snapA;  // GPIOA IDR — ES_L, ES_R, JOGL, JOGR
volatile uint32_t snapB;  // GPIOB IDR — STEPL, STEPR
```
Seeded from `GPIOA->IDR` / `GPIOB->IDR` at boot (before main loop),
then updated on every EXTI event and TIM9 debounce confirmation.

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

## 11. Button debounce — two-phase design (TIM9)

**Fixed.** The old single-phase ISR had no debounce guard on the release path — bounce on
release fired multiple `EVT_JOGL_UP` events and reset the press timer unconditionally.

### Design

```
Phase 1 — HAL_GPIO_EXTI_Callback (hardware edge, may be bouncing):
  EVT_DB_x == 0  →  first edge → arm: lastTick = now, set EVT_DB_x, return
  EVT_DB_x == 1  →  bounce within window → ignore (TIM9 is already watching)

Phase 2 — TIM1_BRK_TIM9_IRQHandler (TIM9, 1 ms period):
  EVT_DB_x set && now − lastTick >= DEBOUNCE:
    → update snapA/snapB → call HAL_GPIO_EXTI_Callback(pin) directly
    → re-enters: EVT_DB_x==1, time expired → reads settled pin → fires real event
```

`EVT_DB_x` is the state machine discriminator:
- `0` = idle
- `1` + fresh edge = arming (return, wait for TIM9)
- `1` + time expired = settled → fire + clear

### Key insight — LL thinking

`HAL_GPIO_EXTI_Callback` is just a C function at an address. The NVIC jumps to it on a
hardware edge. TIM9 ISR can jump to it too — it is not "owned" by the EXTI hardware.
Thinking at the register/assembly level (LL, not HAL) reveals patterns that HAL naming
conventions obscure. The callback becomes a reusable state machine, not a hardware-bound routine.

### TIM9 configuration

- APB2 clock 96 MHz, PSC=95 → 1 MHz tick, ARR=999 → **1 ms period**
- IRQ vector: `TIM1_BRK_TIM9_IRQn` (shared with TIM1 break; TIM1 unused)
- Priority 2 (preempted by endstops/TIM2 at priority 0, not by itself)
- ISR defined in `main.c` (not `stm32f4xx_it.c`) — keeps `static` variables accessible

### CubeMX survival

| What | Where in main.c | Protected by |
|------|-----------------|--------------|
| `static void MX_TIM9_Init(void);` | `USER CODE BEGIN PFP` | CubeMX preserves |
| `MX_TIM9_Init();` call | `USER CODE BEGIN 2` | CubeMX preserves |
| `MX_TIM9_Init()` body | `USER CODE BEGIN 4` | CubeMX preserves |
| `TIM1_BRK_TIM9_IRQHandler` | `USER CODE BEGIN 4` | CubeMX preserves |
| Detection | `make check` | `[OK] TIM9 debounce timer init present` |

---

## 12. Lessons learned (from AUDIT.md)

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

## 13. Reasoning principles (LL thinking)

Hard-won design principles from this project. Useful far beyond it.

### A callback is just a function at an address

`HAL_GPIO_EXTI_Callback`, ISR handlers, HAL callbacks — they are ordinary C functions.
The NVIC jumps to them via a vector table entry. Any other code can call them directly.

**Consequence:** When you need deferred or periodic re-entry into the same logic, don't
duplicate the logic. Call the function. The debounce design exploits this: TIM9 ISR calls
`HAL_GPIO_EXTI_Callback(pin)` directly, re-entering the same state machine with fresh pin state.

HAL naming (`HAL_GPIO_EXTI_Callback`, `HAL_SYSTICK_Callback`) implies ownership by a
hardware event. LL thinking removes that illusion: there is an address, and anything can
branch to it.

### Don't share SysTick — use a dedicated timer

`HAL_SYSTICK_Callback` runs from SysTick, which HAL already uses for `HAL_GetTick()` and
`HAL_Delay()`. Hooking into it adds coupling and timing uncertainty.

When you need a periodic tick for your own purpose: pick a free timer (TIM9 here),
configure it with direct register writes, define the IRQ handler in the file where your
`static` variables live. No HAL handle needed. No extern gymnastics.

```c
/* Direct register init — no HAL handle, no overhead */
__HAL_RCC_TIM9_CLK_ENABLE();
TIM9->PSC = 95; TIM9->ARR = 999;   /* 96 MHz → 1 ms */
TIM9->EGR = TIM_EGR_UG;  TIM9->SR = 0;
TIM9->DIER = TIM_DIER_UIE;
HAL_NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 2, 0);
HAL_NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);
TIM9->CR1 = TIM_CR1_CEN;
```

### Use flags as state machine discriminators

A single `volatile uint32_t evtFlags` bit can carry both "pending" and "which phase" meaning.
`EVT_DB_JOGL`:
- `0` = idle — ISR arms on first edge
- `1` + time not expired = bouncing — ISR ignores, TIM9 waits
- `1` + time expired = settled — TIM9 fires callback, callback reads pin and fires real event

No extra confirm flag, no separate state variable. The existing flag IS the state.

### Define IRQ handlers where the data lives

IRQ handlers don't have to live in `stm32f4xx_it.c`. That file is convention, not law.
The linker resolves weak symbols — any `.c` file can define a strong `TIM1_BRK_TIM9_IRQHandler`.

Keeping `TIM1_BRK_TIM9_IRQHandler` in `main.c` means all the `static volatile` debounce
variables are directly accessible — no `extern`, no header pollution.

### CubeMX eats everything outside USER CODE sections

Any code placed outside a `/* USER CODE BEGIN x */ ... /* USER CODE END x */` block
is silently erased on the next CubeMX regeneration. Rules:

| What | Where |
|------|-------|
| Custom function prototypes | `USER CODE BEGIN PFP` |
| Init calls in main() | `USER CODE BEGIN 2` |
| Custom function bodies | `USER CODE BEGIN 4` |
| Custom ISR bodies | `USER CODE BEGIN 4` |

Add a `make check` guard for any critical init that must survive regen.

---

## 14. TODO (open items)

See `docs/TODO.md` for full details:
- Unified ramp table (index 0 = center, symmetric accel/decel)
- Soft position limits (post-home, post-range)
- WDT decision (unattended operation?)
- `-Os` size optimization + dead code audit
- `TIM2` AutoReload preload enable
- opencm3 / LL branch (eliminate CubeMX dependency)

---

*Last updated: 2026-03-24*
