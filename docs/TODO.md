# TODO

## Project-agnostic STM32 firmware template — new repo

Extract the workflow and tooling from scstepper into a reusable
`stm32fw-template` project that any new STM32 firmware project can
start from.

### What to extract (project-agnostic)

| Component | What it provides |
|-----------|-----------------|
| `Makefile.template` | Custom targets: `check`, `size`, `userinstall`, `sysinstall`, `post_cubemx`, `qtc` |
| `scripts/post_cubemx.sh` | CubeMX regeneration safety: restore Makefile + patch GPIO IT modes |
| `scripts/go.sh` | Build / flash / capture / view one-liner |
| `scripts/go_gdb.sh` | GDB launch with dashboard |
| `scripts/99-<project>.rules` | udev symlinks for BMP + target CDC + FX2 |
| `initcfg/gdbinit` | GDB Dashboard (project-agnostic) |
| `initcfg/project.gdb` | `inject`, `fwcheck`, `ld`, `ag`, `mem_regions` — all project-agnostic |
| `initcfg/STM32F4xx.svd` | SVD for peripheral register access in GDB |
| `initcfg/dot-gdbinit-for-home` | `~/.gdbinit` auto-load enable |
| `make check` | Makefile==template + GPIO patch + stale GDB symbol checks |

### What stays project-specific

- `initcfg/scstepper_panel.py` — Dashboard panel (project variables)
- `initcfg/project.gdb` `st`/`params`/`rxbuf` defines (project symbols)
- `Core/` firmware source
- `captures/` sigrok files
- Stale symbol list in `make check`

### Implementation sketch

```
stm32fw-template/
├── Makefile.template       # rename TARGET, add project sources
├── scripts/
│   ├── post_cubemx.sh      # parameterize GPIO patch patterns
│   ├── go.sh               # parameterize device symlinks
│   ├── go_gdb.sh
│   └── 99-template.rules   # rename symlinks
├── initcfg/
│   ├── gdbinit             # GDB Dashboard (unchanged)
│   ├── project.gdb         # inject + fwcheck + ag + ld (unchanged)
│   ├── panel_template.py   # minimal Dashboard panel stub
│   └── STM32F411.svd
└── docs/
    ├── PROGRAMMER_GUIDE.md # (this guide, generalized)
    └── AUDIT.md            # blank template
```

`post_cubemx.sh` GPIO patch: make the IT mode patterns configurable
via a variable at the top of the script rather than hardcoded regex.

### Why bother

Starting a new STM32 project from CubeMX bare gives you:
- No `make check`
- No CubeMX regeneration safety
- No GDB Dashboard
- No `inject` / `fwcheck`
- No logic analyzer workflow

Starting from this template gives you all of it in 15 minutes.

## ~~Cross-parameter validation in Stepper_SetParam()~~ — DONE 2026-03-20

Currently each parameter is validated individually against its min/max range.
Missing: **consistency checks between parameters** — a user can set individually
valid values that together produce dangerous or broken behavior.

### Scenarios to catch

| Condition | Effect | Check |
|-----------|--------|-------|
| `mmpsmin >= mmpsmax` | `BuildRampTables` while-loop never runs → 0-size ramp → ISR goes straight to DECEL on step 1 | After setting either: require `mmpsmin < mmpsmax` |
| Ramp overflow: `(mmpsmax²−mmpsmin²) / (2·dvdtacc·spmm) > MAX_RAMP_STEPS` | Accel table truncated at 512 → motor never reaches mmpsmax cleanly | After setting mmpsmax/mmpsmin/dvdtacc/spmm: compute predicted accel steps and warn if > MAX_RAMP_STEPS |
| Same for decel: `(mmpsmax²−mmpsmin²) / (2·dvdtdecc·spmm) > MAX_RAMP_STEPS` | Decel table truncated → motor can't stop smoothly from full speed | Same, for dvdtdecc |
| `homespd > mmpsmax` | Home command runs faster than the ramp allows | After setting homespd or mmpsmax |
| `homespd < mmpsmin` | Home command slower than min speed — starts below ramp | After setting homespd or mmpsmin |
| `spmm` changed while `jogmm` or `stepmm` fixed → effective distance changes silently | User may not realize jog/step distance in mm is now wrong | Warn on spmm change: print new effective jog/step step counts |
| `mmpsmax` so high that step pulse freq exceeds driver capability: `mmpsmax * spmm > DRIVER_MAX_FREQ` | Driver misses pulses → position loss | Add `PARAM_MAX_STEP_FREQ` define (e.g. 200000 Hz for typical drivers) and check `mmpsmax * spmm` |

### Implementation sketch

Add `Stepper_ValidateParams()` called at end of `Stepper_SetParam()` after
successful individual validation:

```c
static void Stepper_ValidateParams(void)
{
    int ok = 1;

    if (motorParams.mmpsmin.f >= motorParams.mmpsmax.f) {
        printf("WARN: mmpsmin (%.2f) >= mmpsmax (%.2f) — ramp broken!\r\n",
               motorParams.mmpsmin.f, motorParams.mmpsmax.f);
        ok = 0;
    }

    float vmax = motorParams.mmpsmax.f * (float)motorParams.spmm.u;
    float vmin = motorParams.mmpsmin.f * (float)motorParams.spmm.u;
    float dv2  = vmax * vmax - vmin * vmin;

    float accel_steps = dv2 / (2.0f * motorParams.dvdtacc.f  * (float)motorParams.spmm.u);
    float decel_steps = dv2 / (2.0f * motorParams.dvdtdecc.f * (float)motorParams.spmm.u);

    if (accel_steps > MAX_RAMP_STEPS)
        printf("WARN: accel ramp = %.0f steps > MAX_RAMP_STEPS (%d) — increase dvdtacc or reduce mmpsmax\r\n",
               accel_steps, MAX_RAMP_STEPS);

    if (decel_steps > MAX_RAMP_STEPS)
        printf("WARN: decel ramp = %.0f steps > MAX_RAMP_STEPS (%d) — increase dvdtdecc or reduce mmpsmax\r\n",
               decel_steps, MAX_RAMP_STEPS);

    if (motorParams.homespd.f > motorParams.mmpsmax.f)
        printf("WARN: homespd (%.2f) > mmpsmax (%.2f)\r\n",
               motorParams.homespd.f, motorParams.mmpsmax.f);

    if (motorParams.homespd.f < motorParams.mmpsmin.f)
        printf("WARN: homespd (%.2f) < mmpsmin (%.2f)\r\n",
               motorParams.homespd.f, motorParams.mmpsmin.f);

    float max_freq = motorParams.mmpsmax.f * (float)motorParams.spmm.u;
    if (max_freq > PARAM_MAX_STEP_FREQ)
        printf("WARN: step freq at mmpsmax = %.0f Hz > driver limit (%lu Hz)\r\n",
               max_freq, PARAM_MAX_STEP_FREQ);

    if (ok) printf("params ok\r\n");
}
```

Add to `stepper.h`:
```c
#define PARAM_MAX_STEP_FREQ  200000UL   /* Hz — typical stepper driver limit */
```

Also call `Stepper_ValidateParams()` once at boot after `Stepper_LoadParams()`
to catch corrupted EEPROM combinations.

---

## Unified ramp table with index=0 as center

Currently two separate tables: `accelTable[]` and `decelTable[]`, each indexed
from 0 (slow) to size-1 (fast), walked in opposite directions.

**Idea:** single table `rampTable[MAX_RAMP_STEPS]` with index 0 = center (full speed),
positive indices = decelerating, negative indices = accelerating. Symmetric by design.

Benefits:
- One `BuildRampTables()` pass instead of two
- Accel and decel walk the same table in opposite directions
- `decelIndex` starts at 0 and increments — no negative index boundary issue
- Natural representation of trapezoidal profile

Effort: significant refactor of `StartMove()`, `Stepper_ISR()`, index management.

---

## Soft position limits (post-home)

After homing + `range` measurement, `posSteps` is tracked but not enforced.
A `move 999` can run past ES_R if EXTI debounce misses a trigger.

**When to implement:** when developer-facing `moveto` / scripted positioning is used.
End users do not use `moveto` — deferred intentionally.

**Implementation sketch:**
- After `home` + `range`: `softLimitMax = (int32_t)(rangeUsableMm * motorParams.spmm.u)`
- Before `Stepper_Move` / `Stepper_MoveSteps`: if `posHomed`, clamp target to `[0, softLimitMax]`
- No new EEPROM params — derived from `rangeUsableMm` at runtime

---

## WDT — да или не?

### Аргументи ЗА
- Ако firmware-ът увисне (безкраен цикъл, deadlock), WDT рестартира контролера
- Стандартна практика за production embedded системи
- STM32F411 има IWDG (independent watchdog, RC осцилатор — работи дори при clock fail)

### Аргументи ПРОТИВ (специфично за тази машина)
- При WDT reset по средата на движение: мотора спира рязко без decel → загуба на позиция
- `SafeState_And_Blink()` вече покрива HardFault/NMI/BusFault — реалните crash случаи
- При debug сесия: breakpoint = CPU спрян = WDT изтича = reset = сесията се разпада
  → трябва IWDG да се спира при debug (DBGMCU_APB1_FZ бит) или да се кика от GDB
- USB CDC: ако host е бавен при TX, firmware чака → WDT трябва да се кика в TX loop-а
  → добавя complexity навсякъде

### Предложение
Ако се добавя — само IWDG, с timeout ≥ 2s, kick в main loop heartbeat.
DBGMCU freeze при debug задължително:
```c
__HAL_DBGMCU_FREEZE_IWDG();  /* спира IWDG при debug halt */
```

**Решение:** открито — зависи дали машината ще работи unattended.

---

## opencm3 / LL branch

Reimplement firmware from scratch on a new branch using either:
- **libopencm3** — open-source STM32 peripheral library, no CubeMX dependency
- **STM32 LL (Low-Level) API** — ST-provided thin layer, no HAL overhead

Goals:
- Eliminate CubeMX regeneration problem entirely
- Smaller binary (HAL adds ~20-30 KB overhead)
- Full control over clock/timer/USB init
- Keep same CDC CLI interface and motion control logic

Effort: significant (USB CDC from scratch is non-trivial with libopencm3).
