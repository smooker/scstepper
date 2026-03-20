# Bug Fixes — scstepper firmware

Completed items are shown with ~~strikethrough~~ and fix date.
Open items are listed without strikethrough.

---

## ~~BUG-01: Decel ramp never starts (decelIndex=0)~~ — fixed 2026-03-13

**Symptom:** Motor accelerates normally but never decelerates — runs at max speed until the
last step, then stops abruptly.

**Root cause:** `decelIndex` was initialized to 0 (same as `accelIndex`), so the deceleration
phase immediately satisfied the "done" condition and was skipped entirely.

**Fix:** Initialize `decelIndex` to the correct ramp table offset for the deceleration phase.

---

## ~~BUG-02: R1 — Prompt flooding on jog/step motor stop~~ — fixed 2026-03-20

**Symptom:** After a CDC `move`/`mover`/`movel` command, releasing a jog button (or any
subsequent motor-stop event) printed an extra prompt, flooding the terminal.

**Root cause:** `PrintPrompt()` was called unconditionally on every motor stop, regardless of
whether a CDC command was active.

**Fix:** Added `cdcMoveActive` flag — prompt is printed only when a CDC-initiated move
completes, not on jog/step stops.

---

## ~~BUG-03: Buzzer beeps on every button press/release~~ — fixed 2026-03-20

**Symptom:** Buzzer fired on every jog/step button EXTI event (press and release), making
it impossible to distinguish button feedback from endstop hits.

**Root cause:** `buzzRequest = 1` was set unconditionally at the top of the EXTI ISR,
before the per-pin switch/case.

**Fix:** Moved `buzzRequest = 1` into the ES_L and ES_R cases only — buzzer now fires
exclusively on endstop hits.

---

## ~~BUG-04: JOG_STEP pulse count non-deterministic (41–79 instead of 80)~~ — fixed 2026-03-20

**Symptom:** Saleae capture (`capture_smooker.sr`, 100 kHz) showed 43 bursts;
only 6 had exactly 80 pulses. The rest ranged from 41 to 75.

**Root cause:** `Stepper_Stop()` was called unconditionally in the EXTI UP (release) ISR
handler for jog buttons — even during an in-progress JOG_STEP move. Releasing the button
before the 80-pulse move completed cut it short.

**Fix:** Added `jogStateL/R == JOG_CONT` guard in the UP ISR handler:
```c
if (snapA & BUTT_JOGL_Pin) {
    if (jogStateL == JOG_CONT) Stepper_Stop();  /* only stop continuous jog */
    lastTick_jogL = now;
    evtFlags |= EVT_JOGL_UP;
}
```
After fix: 43/43 bursts = exactly 80 pulses. ✓

**Verified by:** `captures/capture_smooker.sr` (before/after comparison)

---

## ~~BUG-05: JOG_CONT race condition — motor runs to endstop on release~~ — fixed 2026-03-20

**Symptom:** Releasing a jog button during JOG_CONT sometimes had no effect — motor
continued running until it hit the endstop.

**Root cause:** Race between ISR and main loop:
1. UP ISR fires while `jogStateL == JOG_PRESSED` → no `Stepper_Stop()` (guard from BUG-04 fix)
2. Main loop then transitions `jogStateL → JOG_CONT` and calls `Stepper_RunContinuous()`
3. Button already released — no further UP event arrives → motor runs unchecked

**Fix:** Added safety net in `ProcessEvents()` UP handler:
```c
if (flags & EVT_JOGL_UP) {
    if (jogStateL == JOG_CONT) Stepper_Stop();  /* safety net */
    jogStateL = JOG_IDLE;
}
```
If the main loop races ahead, the safety net catches it when processing the UP event.

---

## ~~BUG-06: Wrong HSE crystal frequency in CubeMX — USB clock 50 MHz instead of 48 MHz~~ — fixed 2026-03-20

**Symptom:** Random/garbled characters after USB re-enumeration (cable reconnect or
host USB reset). Firmware worked initially but became corrupted after any disconnect.

**Root cause:** `.ioc` had `RCC.HSE_VALUE=24000000` but the actual crystal on the
Black Pill board is **25 MHz**. With PLLM=12:

| Setting | Wrong (24 MHz) | Correct (25 MHz) |
|---------|---------------|-----------------|
| VCO_in | 2 MHz | 1 MHz |
| VCO_out | 192 MHz | 192 MHz |
| SYSCLK | 96 MHz | 96 MHz |
| USB (÷4) | **50 MHz ✗** | **48 MHz ✓** |

USB receiving 50 MHz instead of the required 48 MHz caused bit-timing errors,
manifesting as random symbols after re-enumeration.

**Fix:** `scstepper.ioc`: `PLLM=25`, `PLLN=192` → VCO_in=1 MHz, USB=48 MHz exactly.

---

## ~~BUG-07: FPU directive mismatch in startup assembler~~ — fixed 2026-03-20

**Symptom:** Potential ABI mismatch at startup — assembler FPU declaration inconsistent
with compiler flags. Could cause hard faults if FPU registers used before C runtime init.

**Root cause:** `startup_stm32f411xe.s` had `.fpu softvfp` while `Makefile` compiled with
`-mfpu=fpv4-sp-d16 -mfloat-abi=hard`.

**Fix:** Changed to `.fpu fpv4-sp-d16` in startup assembler to match compiler flags.

---

## ~~BUG-08: NVIC priorities set at runtime instead of in CubeMX~~ — fixed 2026-03-20

**Symptom:** `FixNVIC_Priorities()` function called in `main()` after `MX_GPIO_Init()` to
correct priorities that CubeMX generated incorrectly. Fragile — any CubeMX regeneration
would silently revert priorities.

**Root cause:** NVIC priorities were not configured in `scstepper.ioc` — CubeMX defaults
were wrong and corrected manually in C code.

**Correct priorities (PRIORITYGROUP_4, lower number = higher priority):**

| IRQ | Priority | Reason |
|-----|----------|--------|
| TIM2 | 0 | Stepper pulse ISR — highest |
| ES_L (EXTI3), ES_R (EXTI4) | 0 | Must stop motor atomically with TIM2 |
| JOG_L (EXTI9_5) | 0 | Same |
| STEP_L (EXTI0), STEP_R (EXTI1) | 2 | Lower — no timing-critical stop |
| USB OTG_FS | 3 | Lowest of interrupt group |

**Fix:** Added correct NVIC entries to `scstepper.ioc`; removed `FixNVIC_Priorities()` entirely.

---

## ~~BUG-09: USB PCD callbacks — BKPT, missing USBD_LL call, no-op SOF~~ — fixed 2026-03-20

**Sub-issue A — BKPT in suspend callback:**
`My_PCD_SuspendCallback` contained a `BKPT` instruction (debug artifact). Although
not registered in production, dead code with a breakpoint trap is dangerous. Removed.

**Sub-issue B — Disconnect callback missing USBD_LL_DevDisconnected:**
`My_PCD_DisconnectCallback` set `CDC_IsConnected = 0` but did not call
`USBD_LL_DevDisconnected()` — the USB device stack was never notified of disconnect,
leaving internal state corrupt on next enumeration.
Fix: added `USBD_LL_DevDisconnected((USBD_HandleTypeDef *)hpcd->pData)`.

**Sub-issue C — SOF callback was a no-op:**
`My_PCD_SOF` was registered as callback but skipped `USBD_LL_SOF()`. Removed
registration; default handler in `usbd_conf.c` calls `USBD_LL_SOF` correctly.

**Sub-issue D — Redundant DataOut and Connect callbacks:**
`MyPCD_DataOutStageCallback` and `My_PCD_ConnectCallback` duplicated default
HAL behavior without adding anything. Removed.

---

## ~~BUG-10: Duplicate EEPROM implementation files~~ — fixed 2026-03-20

**Symptom:** Two EEPROM emulation implementations in the tree:
- `Core/Inc/eeprom_emul.h` + `Core/Src/eeprom_emul.c` — generic STM32 emulation (unused)
- `Core/Inc/eeprom_emul_uint32_t.h` + `Core/Src/eeprom_emul_uint32_t.c` — project-specific (used)

**Fix:** Deleted unused `eeprom_emul.h` and `eeprom_emul.c`.

---

## ~~BUG-11: CubeMX usbd_conf.c — spurious -Wunused-parameter warnings~~ — fixed 2026-03-20

**Symptom:** Build warned on every compile:
```
usbd_conf.c: unused parameter 'size' in USBD_static_malloc
usbd_conf.c: unused parameter 'p' in USBD_static_free
```

**Root cause:** CubeMX-generated `USBD_static_malloc` / `USBD_static_free` use a static
buffer — the `size` and `p` parameters are intentionally ignored. No USER CODE sections
exist in those functions, so `#pragma GCC diagnostic` cannot be added without being
overwritten on next regeneration.

**Fix:** Per-file rule in `Makefile` with `-Wno-unused-parameter` for `usbd_conf.o` only.
Survives CubeMX regeneration (Makefile is not touched by CubeMX).

---

## BUG-R2 (open): `range` command does not disable buttons

**Symptom:** Physical buttons remain active during `range` measurement. Pressing a jog
button during `RunRange()` causes undefined behavior (move interrupted mid-measurement).

**Fix needed:** Set `buttonsEn = 0` at start of `RunRange()`, restore on completion.

---

## BUG-R3 (open): `moveto` accepted while motor busy

**Symptom:** Issuing `moveto <mm>` while a move is already in progress queues a second
move with no interlock — result is undefined (position tracking corrupted, potential ES hit).

**Fix needed:** Check `Stepper_IsBusy()` at entry to `moveto` handler; print error and
return if motor is not idle.

---

*Last updated: 2026-03-20*
