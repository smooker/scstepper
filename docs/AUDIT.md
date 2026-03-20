# Firmware Audit — 2026-03-13

Audit of stepper_sc firmware on branch `routing`, tag `v0.4-buttons`.

## Bugs

### 1. RX/TX buffer name swap (main.c:113-121)

```c
#define RX_BUF_SIZE  512
static uint8_t UserTxBufferFS[RX_BUF_SIZE];   // TX sized by RX

#define TX_BUF_SIZE  512
static uint8_t UserRxBufferFS[TX_BUF_SIZE];   // RX sized by TX
```

Both are 512 so no runtime effect, but dangerous if sizes ever diverge.

### 2. CDC_RxRead uses APP_RX_DATA_SIZE instead of RX_BUF_SIZE (main.c:718)

```c
rxTail = (rxTail + 1) % APP_RX_DATA_SIZE;
```

But `MyCDC_Receive_FS` wraps with `RX_BUF_SIZE`. If they differ, ring buffer corruption.

**Fix:** Use a single `#define CDC_RX_BUF_SIZE 512` everywhere.

### 3. evtFlags race condition (main.c:1556) — HIGH PRIORITY

```c
uint32_t flags = evtFlags;
evtFlags &= ~flags;       // NOT atomic
```

ISR can set a new flag between read and clear — that flag is lost.

**Fix:** Disable interrupts around the read-clear:
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

### 5. Error_Handler deadlock (main.c:1645-1653)

```c
__disable_irq();
while (1) {
    HAL_GPIO_WritePin(...);
    HAL_Delay(50);          // needs SysTick — but IRQs are disabled!
```

`HAL_Delay` hangs forever. LED stays in one state instead of blinking.

**Fix:** Use a NOP busy-loop instead of `HAL_Delay`, or re-enable SysTick specifically.

### 6. decelSteps mismatch with table indices (stepper.c:236-263)

`BuildRampTables()` sets `decelSteps` from table sizes, then the analytic calculation
overwrites it. If analytic `decelSteps` exceeds `decelSize`, ISR enters DECEL too late
and may under-decelerate (abrupt stop).

**Fix:** Use `min(analyticDecelSteps, decelSize)` or ensure consistency.

### 7. dashTime macro missing parentheses (defines.h)

```c
#define dashTime  3*dotTime
```

In expression `dashTime + 1` → `3*60 + 1 = 181`, not `3*61`.

**Fix:** `#define dashTime (3*dotTime)`

### 8. Busy-wait in ISR (stepper.c:389)

```c
while (__HAL_TIM_GET_COUNTER(stepTim) <= ccr + 10);
```

Short spin (~100ns at 96MHz), but if counter wraps, theoretical risk of spinning
for a full 32-bit TIM2 period (~44 seconds).

## Safety Concerns (CNC Motor Control)

### 9. `motorola` diagnostic mode — infinite loop, no endstop check

The `motorola` command enters `while(1)` doing 16k pulses each direction at 1kHz.
No escape except reset. No endstop checking. Can cause physical damage if motor
is near a limit.

**Fix:** Add endstop checks inside the loop, or at least a step count limit.

### 10. RunHome uses hardcoded 9999mm (main.c:1456, 1486)

If endstop fails to trigger (wire break, wrong polarity), motor runs ~4M steps.

**Fix:** Configurable max travel limit. Abort and report error if exceeded.

### 11. No soft position limits

`posSteps` tracks position but there are no software travel limits.
After homing, `move 999` runs past the right endstop if EXTI debounce misses.

**Fix:** Add configurable soft limits (EEPROM params), check before move.

### 12. Stepper_RunContinuous = INT32_MAX/2 steps (stepper.c:327)

If jog button release EXTI fails to fire, motor runs ~6.6 hours at 50mm/s.

**Fix:** Add a maximum continuous run timeout or step limit.

### 13. CLI move commands clear esBlocked (main.c:788-802)

`move`, `movel`, `mover`, `steps` all set `esBlocked = 0`, allowing motion back
into an already-triggered endstop. Deliberate but risky.

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
| 3 | Parameter validation (`spmm=0` = div by zero) | Prevents crash |
| 4 | Fix `Error_Handler` to use NOP loop | LED blinks as intended |
| 5 | Replace `-O0` with `-Og` in Makefile | Better debug perf |
| 6 | Remove `-dM` from CFLAGS (debug leftover) | Clean build output |
| 7 | Enable `TIM_AUTORELOAD_PRELOAD` | Glitch-free period changes |
| 8 | Remove dead code (ProcessLineOld, initParams, etc.) | Smaller binary |
| 9 | Consistent ring buffer size constant | Prevents mismatch bugs |

## Priority Fix Order

1. **evtFlags race** — can lose endstop/button events
2. **Stepper_Stop() critical section** — state machine corruption
3. **Parameter validation** — division by zero on `spmm=0`
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
