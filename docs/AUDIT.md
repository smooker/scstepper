# Firmware Audit — 2026-03-13

Audit of stepper_sc firmware on branch `routing`, tag `v0.4-buttons`.

## Bugs

### 1. RX/TX buffer name swap — **FIXED**

Renamed to `CDC_TX_BUF_SIZE` / `CDC_RX_BUF_SIZE`, each sizing the correct buffer.

### 2. CDC_RxRead uses APP_RX_DATA_SIZE instead of RX_BUF_SIZE — **FIXED**

All RX ring buffer wrap operations now use `CDC_RX_BUF_SIZE` consistently.

### 3. evtFlags race condition — **FIXED**

```c
// ISR can set a new flag between read and clear — that flag is lost.
uint32_t flags = evtFlags;
evtFlags &= ~flags;       // NOT atomic  ← was the bug
```

**Fix applied:** `__disable_irq()` / `__enable_irq()` around the read-clear in `ProcessEvents()`:
```c
__disable_irq();
uint32_t flags = evtFlags;
evtFlags &= ~flags;
__enable_irq();
```

### 4. Stepper_Stop() called from EXTI ISR without critical section — **FIXED**

Endstop and jog-release handlers in `HAL_GPIO_EXTI_Callback` call `Stepper_Stop()`.
`Stepper_Stop()` modifies `stepsRemaining`, `decelIndex`, `stepperState` — same vars
modified by `Stepper_ISR()`. TIM2 ISR can preempt EXTI mid-update, causing torn state.

**Fix applied:** `FixNVIC_Priorities()` sets TIM2, ES_L, ES_R, and JOG EXTI all to
priority 0. Same-priority ISRs cannot preempt each other on Cortex-M4 — this makes
`Stepper_Stop()` from EXTI ISR atomic with respect to TIM2 ISR, without needing
`__disable_irq()` inside `Stepper_Stop()`.

### 5. Error_Handler deadlock — **FIXED**

`Error_Handler` now calls `SafeState_And_Blink()` which uses a NOP busy-loop
(no `HAL_Delay`), Hi-Z all functional GPIO, and fast LED blink — IRQ-safe.

### 6. decelSteps mismatch with table indices — **FIXED**

Analytic `decelSteps` clamped to `decelSize` after recalculation:
`if (decelSteps > decelSize) decelSteps = decelSize;`

### 7. dashTime macro missing parentheses — **FIXED**

`#define dashTime (3*dotTime)` and `spaceTime (7*dotTime)` — parentheses added.

### 8. Busy-wait in ISR — **FIXED**

`target` clamped to `ARR` before the spin: `if (target > arr) target = arr;`
Prevents infinite spin if counter wraps at end of period.

## Safety Concerns (CNC Motor Control)

### 9. `motorola` diagnostic mode — infinite loop, no endstop check — **BY DESIGN**

The `motorola` command (password-protected via `diag_outputs`) enters `while(1)`
doing 16k pulses each direction at 1kHz via direct TIM2 PWM — completely bypassing
the stepper state machine, EXTI, and all safety logic.

**This is intentional.** It is a lowest-level hardware diagnostic tool for use with
an oscilloscope to verify PULSE/DIR signal integrity and check that endstop EXTI
interrupts fire correctly — before trusting any firmware logic. Requires hardware
reset to exit. Only used during bring-up/debugging with the developer present.

### 10. RunHome uses hardcoded 9999mm — **BY DESIGN**

`9999mm` means "move until you hit the endstop." `RunHome` is an operator
function — the operator is expected to verify ES_L works before calling it,
or to call `home` deliberately. A broken endstop is a maintenance issue, not
a firmware issue. The machine will not self-destruct from a gentle endstop hit.

### 11. No soft position limits — **INTENTIONAL / TODO**

Soft limits require homed + measured range — not used by end users.
Deferred for future developer use. See `docs/TODO.md`.

### 12. Stepper_RunContinuous = INT32_MAX steps — **BY DESIGN**

`stepsRemaining = 0x7FFFFFFF` means "run until stopped." `int32_t` is correct —
`uint32_t` would cause signed arithmetic issues in ramp calculations (`/2`,
comparisons with `decelSteps`, float cast). `0xFFFFFFFF` as int32 = -1 → stops
immediately. Motor is bounded by physical endstops. Stopped by EXTI on button
release or endstop hit.

### 13. CLI move commands clear esBlocked — **FIXED**

`move`/`movel`/`mover`/`steps` now check direction vs `esBlocked` before moving:
- Direction away from blocked endstop → allowed, clears `esBlocked`
- Direction toward blocked endstop → refused with `"blocked: ES_L/R"`

## Good Practices

- **Direction inversion via EEPROM** — wiring flexibility without firmware change
- **Hardware FPU sqrt** — `vsqrt.f32` inline assembly, avoids libm overhead
- **Debounced endstops** — 10-sample polling (50ms window) during homing
- **Power-loss safe EEPROM** — data before header, page-swap compaction
- **Boot stuck-input check** — CQ CQ CQ morse loop, prevents unintended motion
- **Buttons disabled until boot completes** — `buttonsEn` starts at 0
- **Separate accel/decel rates** — asymmetric dvdtacc/dvdtdecc for stepper tuning
- **Git hash + build date in banner** — essential for firmware version tracking
- **GDB command injection** (`inject_cmd.sh`) — creative remote testing approach
- **Sigrok captures in repo** — hardware-validated development with real traces

## Suggestions

| # | Suggestion | Impact |
|---|-----------|--------|
| 1 | Critical section in `Stepper_Stop()` | Prevents state corruption |
| 2 | Atomic `evtFlags` read-clear | Prevents lost events |
| 3 | ~~Parameter validation (`spmm=0` = div by zero)~~ | **FIXED** 2026-03-20 |
| 4 | Fix `Error_Handler` to use NOP loop | LED blinks as intended |
| 5 | Replace `-O0` with `-Og` in Makefile | Better debug perf |
| 6 | Remove `-dM` from CFLAGS (debug leftover) | Clean build output |
| 7 | Enable `TIM_AUTORELOAD_PRELOAD` | Glitch-free period changes |
| 8 | Remove dead code (ProcessLineOld, initParams, etc.) | Smaller binary |
| 9 | Consistent ring buffer size constant | Prevents mismatch bugs |

## Priority Fix Order

1. ~~**evtFlags race**~~ — **FIXED** 2026-03-20
2. **Stepper_Stop() critical section** — state machine corruption
3. ~~**Parameter validation**~~ — **FIXED** 2026-03-20
4. **Error_Handler** — use NOP loop instead of HAL_Delay

---

*Audited by Claude Opus 4.6 — laptop instance, 2026-03-13*
*Firmware developed in collaboration with Claude Opus 4.6 — sw2 instance*

---

## Second Opinion — 2026-03-20

Independent assessment by a fresh Claude Sonnet 4.6 instance, given only the source files
(no prior context about what was done or why).

### Maturity Scorecard

| Dimension               | Score | Notes                                                                 |
|-------------------------|-------|-----------------------------------------------------------------------|
| Developer ergonomics    | A+    | Best-in-class GDB tooling, clean build/debug/flash workflow           |
| Code organization       | A     | Clear separation (stepper.c / main.c), USER CODE sections work        |
| Fault handling          | B+    | SafeState_And_Blink() solid; Error_Handler deadlock breaks it         |
| Motor control logic     | B     | Physics-based ramps; decelSteps mismatch + no soft limits             |
| CubeMX robustness       | B−    | Survives regen via template; GPIO patch is fragile; .ioc is in repo   |
| Unfixed known bugs      | D     | 7+ documented issues still open (see Bugs section above)              |
| Watchdog / recovery     | F     | No watchdog timer; no stall detection; no position sanity check       |

### Highlighted Strengths

- **GDB tooling is industry-grade**: `inject` writes test commands into CDC RX buffer
  without recompiling; `scstepper_panel.py` Dashboard module shows live motor state,
  ring buffer occupancy, jog events. Real time-saver.
- **Makefile survives CubeMX regeneration**: `post_cubemx.sh` + `Makefile.template`
  + `make check` is a mature, disciplined approach to CubeMX workflow.
- **Documentation is honest**: audit explicitly lists bugs with fixes. Sigrok captures
  are in the repo and referenced in analysis. Not marketing docs.
- **Boot safety**: stuck-input CQ CQ CQ morse loop, `buttonsEn = 0` until boot
  completes, 10-sample endstop debouncing. Defensive programming.
- **Hardware FPU sqrt** (`vsqrt.f32` inline asm) — avoids libm overhead.

### Minimum Roadmap to "Not Dangerous"

1. Fix `evtFlags` race — `__disable_irq()` around read-clear (5 min)
2. Replace `Error_Handler` `HAL_Delay` with NOP busy-loop (15 min)
3. Add soft position limits + parameter range validation (2 hrs)
4. Guard `motorola` diagnostic loop with endstop checks (1 hr)
5. Add watchdog timer (1 hr)
6. Smoke test on hardware (2 hrs)

### Verdict

> *"A capable, well-documented development firmware with exceptional developer tooling —
> but not suitable for production hardware. It works brilliantly in the lab because the
> developer is present to reset it. The audit itself is excellent; the fact that all
> critical issues remain unfixed suggests this is prototype/exploratory work, not a
> commercial product."*

---

*Second opinion by Claude Sonnet 4.6 — sw2 instance, 2026-03-20*

---

## Self-Audit Round — 2026-03-20

After the second opinion flagged issues, we went through every finding systematically
ourselves — not waiting for an external auditor. The process: read the second opinion,
trace each issue back to the code, fix or justify. Where we fixed something, we
re-read the surrounding code looking for follow-on issues. This uncovered bugs
the second opinion missed.

### What the second opinion found → what we actually did

| Second opinion finding | Resolution |
|------------------------|------------|
| `evtFlags` race condition | **FIXED** — `__disable_irq()` / `__enable_irq()` wrap |
| `Error_Handler` uses `HAL_Delay` | **FIXED** — `SafeState_And_Blink()`, NOP loop, Hi-Z GPIO |
| No parameter validation | **FIXED** — `Stepper_ValidateParams()` + cross-param checks |
| Soft position limits missing | **DEFERRED** — BY DESIGN for this use case (see TODO.md) |
| `decelSteps` mismatch | **FIXED** — clamp to `decelSize` after recalculation |
| `motorola` bypasses safety | **BY DESIGN** — hardware diagnostic tool, developer present |
| No watchdog timer | **DEFERRED** — documented in TODO.md with pros/cons analysis |

### Bugs we found ourselves (not in second opinion)

**`decelIndex` out-of-bounds read** — `STEPPER_DECEL` case read `decelTable[decelIndex]`
before checking `decelIndex >= 0`. When `decelSteps` was analytically calculated as 0
(very short move), `decelIndex` went negative on first ISR tick → array underread.
```c
/* Before: read then check */
currentPeriod = decelTable[decelIndex];  // ← UB if decelIndex < 0
decelIndex--;
if (decelIndex < 0) ...

/* After: check then read */
if (decelIndex < 0) {
    currentPeriod = maxPeriod;
} else {
    currentPeriod = decelTable[decelIndex];
    decelIndex--;
}
```

**`esBlocked` software shadow** — we had a flag `esBlocked` that shadowed the
hardware endstop state. It was read-modify-write from multiple contexts (ISR,
main loop, CLI), with subtle clear-on-move semantics. On closer inspection:
the flag added no value — the hardware state is always available via `snapA`.
Removed entirely. All endstop checks now read `snapA` (GPIOA IDR snapshot taken
at ISR entry), which is the single source of truth.

**`snapA/snapB` scope too narrow** — declared `static volatile` inside the EXTI
callback's local scope. Functions earlier in the file (`ProcessLine`, jog handling)
could not see them, leading to some code still using `HAL_GPIO_ReadPin()` live reads
instead of the snapshot. Promoted to file-scope `volatile` — now the whole file
uses a single consistent view of pin state per ISR tick.

**`HAL_Delay(100)` inside `moveto` error path** — blocking 100ms call in the main
loop to produce a buzz beep. Blocks USB CDC TX, button polling, everything.
Replaced with `buzzRequest = 1` (non-blocking, handled by main loop timer).

**`post_cubemx.sh` missed FALLING edges** — script patched `GPIO_MODE_IT_RISING`
→ `IT_RISING_FALLING` but not `GPIO_MODE_IT_FALLING` → `IT_RISING_FALLING`.
After CubeMX regen, button release events were lost. Fixed: both patterns patched.

### UX improvements from self-audit

Buzzer feedback added at every meaningful user action:

| Event | Buzz |
|-------|------|
| Jog button press (initial jog fires) | 1 beep |
| Step button press (step move fires) | 1 beep |
| Endstop hit during motion | 1 beep (was already there) |
| `save` — params written to EEPROM | 1 beep |
| `save` — blocked by validation warning | no beep (error message only) |
| `initeeprom` — defaults written | 1 beep |
| Blocked CDC move (endstop active) | 1 beep |
| `moveto` out of range | 1 beep |
| Home sequence complete | 1 beep (was already there) |

`Stepper_SaveParams()` changed from `void` to `int` (1=saved, 0=blocked) so callers
can condition the beep on actual success.

### Updated maturity scorecard (self-assessment)

| Dimension               | Score before | Score after | Change |
|-------------------------|-------------|-------------|--------|
| Fault handling          | B+          | A−          | SafeState solid, no deadlock |
| Motor control logic     | B           | B+          | decelIndex OOB fixed, clamp added |
| GPIO consistency        | C+          | A+          | Single source of truth, snapshots everywhere |
| Parameter validation    | F           | B+          | Range + cross-param + EEPROM guard |
| UX / feedback           | C           | B+          | Buzzer on all meaningful events |
| Watchdog / recovery     | F           | F           | Still no WDT (deferred by design) |
| Dead code               | C           | A           | Removed: ProcessLineOld, initParams, writeParams, readParams |

### Self-audit takeaway

The second opinion was useful for a structured overview, but the most interesting bugs
(decelIndex OOB, esBlocked shadow, snapA scope) were found during implementation — not
by static reading alone. Each fix forced a re-read of surrounding code, which found
the next issue. The process compounds.

### Post-audit: stale GDB script reference (found later)

After removing `esBlocked` from the firmware, the GDB `st` command in `project.gdb`
still referenced it. This was not caught by `make clean` or `make` — and it
**never will be**, because GDB scripts are interpreted text files, not compiled.
The C compiler checks C symbols. GDB only fails when you actually run `st` in a
live session.

**Root cause:** when removing a firmware symbol, only the `.c`/`.h` files were
searched. Dev tooling (`.gdb`, `.py` dashboard panels, `inject_cmd.sh`) was not.

**Fix applied:** added a third check to `make check`:
```makefile
GHOST=$(grep -rhoE '\b(esBlocked|initParams|...)\b' initcfg/ scripts/)
```
If any removed symbol appears in GDB scripts, `make check` fails immediately.

**Rule:** when removing a firmware variable, grep the whole repo — not just source
files. Extend the `make check` symbol list with the removed name.
`make check` is now the single place that enforces this: three checks, one command.

---

*Self-audit by Claude Sonnet 4.6 + smooker — sw2 instance, 2026-03-20*

---

## TODO: Code Size Optimization

### Quick wins (low risk)

**1. Switch `-O0` → `-Os` in Makefile**
```makefile
OPT = -Os   # optimize for size
```
`-O0` = no optimization (debug default). `-Os` = size-optimized, still debuggable with
`-g3 -gdwarf-2`. Expected savings: 30-50% flash. For debug builds keep `-Og`.

**2. Remove dead code** (never called):
- `ProcessLineOld()` — main.c:845 (~285 lines)
- `initParams()` — main.c:268
- `writeParams()` — main.c:280
- `readParams()` — main.c:322

Note: `dumpVars()` IS called via `dump` CLI command — keep it.

**3. Remove `-u _printf_float` / `-u _scanf_float` from LDFLAGS**
Forces linking of float printf/scanf from newlib-nano even if unused.
Check: does firmware actually print floats via `printf`? If only via CDC
`sprintf` → check if `%f` is used in format strings. If yes, keep; if
`printf` is integer-only, remove and save ~5-8 KB.

**4. Remove `-dM` from CFLAGS if present**
Dumps all predefined macros to stdout during compile — debug leftover.
Currently not in Makefile — already clean.

**5. Enable LTO (Link Time Optimization)**
```makefile
CFLAGS  += -flto
LDFLAGS += -flto
```
Eliminates dead code across translation units. Works well with `-Os`.
Risk: longer build time, occasional LTO bugs with HAL — test carefully.

### Workflow suggestion

Add a `size` target to Makefile:
```makefile
.PHONY: size
size: $(BUILD_DIR)/$(TARGET).elf
	$(SZ) --format=berkeley $<
```
Run before/after each optimization to track flash/RAM usage.

### Priority order

1. `-Os` (5 min, biggest gain)
2. Dead code removal (30 min)
3. Check `_printf_float` necessity (15 min)
4. LTO (test carefully, last)

