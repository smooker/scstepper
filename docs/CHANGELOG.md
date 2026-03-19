# CHANGELOG — STM32F411CEU6 Stepper Controller

## [Unreleased] — 2026-03-19 (v1.0 milestone)

### Added
- **Button combo homing gesture**: at ES_L + hold JOGL + press JOGR → triggers RunHomeEx(1)
  - EVT_HOME flag: ISR → main loop → RunHomeEx(fromButtons=1)
  - 300ms debounced abort on non-simultaneous button release
  - Abort check embedded in all polling loops (approach, settle, backoff, park)
  - 1s grace period + 150ms beep + prompt after successful homing
  - Homing print messages only when debug&1; ABORT always prints
  - CDC `home` command unchanged
- **`range` command**: measures travel distance between endstops
  - Drives to ES_R, computes raw and usable (raw − homeoff) mm
  - Stores `rangeUsableMm`, returns to home (endstops disabled during return)
- **`moveto <mm>` command**: absolute positioning
  - Requires posHomed AND rangeUsableMm > 0
  - target < 0 or > range → 100ms beep + error message
  - delta = target − currentMm → Stepper_Move(delta)

### Fixed / Improved
- On motor stop (wasBusy→idle): prints `\r\n\r\n` + prompt (double blank line before prompt)
- Blocking commands (home, range, combo): end with `\r\n\r\n` + prompt
- Prompt format changed to `%7.2f` from `%8.2f` — aligns with `XXXX.XX >`
- Jog bounce re-press after release: `lastTick_jog` now reset on release,
  suppressing bounce-press events for `DEBOUNCE_REL_MS` (50ms) after release.
  Affected both short-press (JOG_STEP) and long-hold (JOG_CONT) modes.

### Changed
- `DEBOUNCE_REL_MS = 50ms` introduced (separate from `DEBOUNCE_MS = 30ms`).
  Jog press debounce after release is 50ms — switches bounce more on release.
- Jog press endstop check moved into ISR via `snapA` — blocked directions
  rejected immediately, no event queued to main loop.
- Removed redundant `Stepper_Stop()` from `ProcessEvents` EVT_JOGx_UP handler —
  stop already guaranteed by ISR.
- `snapA = GPIOA->IDR` snapshot at ISR entry — all pin checks use consistent state.
- `buzzRequest` flag: buzzer deferred to main loop (avoids race with MorseUpdate).
- NVIC priorities fixed via `FixNVIC_Priorities()`:
  TIM2 + ES endstops at priority 0, jog at priority 0, step at priority 2, USB at 3.
  Makes `Stepper_Stop()` from EXTI ISR safe without `__disable_irq()`.
- Endstop edge lockout (30ms) in normal mode — EMI/vibration protection.

## [e243560] — 2026-03-18 (Input snapshot, ISR endstop blocking, debug guards)

## [Unreleased] — 2026-03-04 (lookup table branch, not committed)
**Active firmware — stepper.c mtime 2026-03-04 09:32**

### Changed
- Replaced Lindstedt online formula in ISR with precomputed ramp tables
  - `BuildRampTables()` called once at move start, builds `accelTable[]` and `decelTable[]`
  - ISR reduced to pure table lookup: `currentPeriod = accelTable[accelIndex++]`
  - FPU `vsqrt.f32` used inside `BuildRampTables()` (not in ISR)
  - `MAX_RAMP_STEPS = 512` entries per table
- Removed `STEPPER_DIRSETUP` and `STEPPER_STOP` states (repo additions not carried over)
- Kept `movel <mm>` / `mover <mm>` CLI aliases for directional moves
- Known issue: `CONST → DECEL` transition sets `decelIndex = 0` (slow end) — work in progress

---

## [96ec342] — 2026-03-04 09:52
### Changed
- Improved motion profile for short moves (triangle profile)
- Calculate peak speed from available steps using `v_peak = sqrt(2*a*d*steps/(a+d))`
- Removed old `stepsRemaining/2` decel clamp — replaced by proper physics formula
- Hardware `VSQRT.F32` via inline ASM (`sqrtf_approx`) — no libm dependency
- `PeriodToIndex()` and `Stepper_StartDecel()` added for external decel trigger

## [5bf1d37] — 2026-03-04 08:48
### Fixed
- Minor fixes in defines and code

## [0f5c05f] — 2026-03-04 08:35
### Added
- `stepper.c` / `stepper.h` — full stepper driver module
  - TIM2 CH3 on PB10 — hardware PWM pulse generation, 50µs pulse width
  - Trapezoidal motion profile: ACCEL → CONST → DECEL states
  - Lindstedt formula for online period stepping in ISR
  - Natural units: mm/s, mm/s², steps/mm
  - EEPROM persistence for all 7 motion parameters
  - `params_t` struct with float/uint32 union for lossless EEPROM storage
- CLI commands: `move <mm>`, `steps <n>`, `set <param> <f>`, `params`, `save`, `stop`
- Verified with oscilloscope and Saleae logic analyzer
- SVD helper from PyCortexMDebug added to GDB config

## [d409dd6] — 2026-03-03 20:11
### Added
- Forgotten files committed: `stepper.c`, `stepper.h`, cleaned `claude_pv.csv`

## [1733001] — 2026-03-03 18:14
### Added
- Ramp testing session
- `claude_pv.csv` — PulseView capture data from logic analyzer
- TIM2 and pin assignments added to `.ioc`

## [e2b05dd] — 2026-03-03 15:06
### Added
- `SEQ_O` parser state for keypad Enter and F1–F4 keys
- Built-in commands: `cls`, `help`, `uptime`, `reset`
- sscanf float parsing + printf float output via `-u _printf_float`
### Fixed
- RX drain loop no longer blocks on TX busy-wait
- Garbage on key-hold: RX and TX phases separated

## [3121724] — 2026-03-03 14:37
### Added
- EEPROM emulation using Flash sectors 6 & 7 (wear leveling, claude.ai assisted)
- USB CDC RX via ring buffer in `CDC_Receive_FS` callback
- VT102 key parser: arrow keys, backspace, Ctrl+L clear screen
- `printf` routed through `_write()` with `TxState` busy-wait + 100ms timeout
- Line buffer command parsing via `sscanf`

## [ad97faf] — 2026-03-03 13:15
### Added
- Key parser foundation, `echo` + `printf` via `_write` low-level

## [2d44c8f] — 2026-03-03 11:01
### Added
- RX ring buffer (work in progress — not fully working yet)

## [2837276] — 2026-03-03 07:20
### Added
- EEPROM variables and params working
- `float ↔ uint32_t` union technique for lossless EEPROM storage

## [a97432a] — 2026-03-03 06:16
### Added
- `initParams()` test, float rounding verified in printf output

## [b902c33] — 2026-03-03 05:18
### Added
- `uint32_t` EEPROM write and read working

## [e953ba2] — 2026-03-02 19:39
### Notes
- Credits to Tisho for introducing claude.ai

## [8445658] — 2026-03-02 19:37
### Changed
- Replaced AN3969 EEPROM emulation with claude.ai generated implementation

## [a45ffb7] — 2026-03-02 18:03
### Fixed
- DTR was the culprit for USB CDC disconnect issue
- Solved with `hupcl` in `stty` listen scripts

## [c392e0f] — 2026-03-02 17:19
### Added
- `stty.sh` with correct options for CDC endpoint detection

## [81166e3] — 2026-03-02 07:06
### Changed
- Removed intermediate TX buffer (`buffx`) — using native USB CDC buffers directly
- Custom `MyPCD_DataInStageCallback` / `MyPCD_DataOutStageCallback` registered

## [9482d23] — 2026-03-01 20:17
### Added
- `dodefines.sh` — script to generate `defines.h` via gcc `-dM` preprocessor dump
- Clang/clangd compatibility for Qt Creator

## [01beeb1] — 2026-02-28 13:23
### Added
- USB physical connect/disconnect/suspend/SOF callbacks registered via HAL PCD callback API

## [5638714] — 2026-02-28 10:15
### Added
- clangd integration with Qt Creator (`.cflags`, `.cxxflags`, `.includes`, `.files`)

## [678f918] — 2026-02-28 07:57
### Added
- Morse code for digits (0–9)
- `dotTime`, `dashTime`, `spaceTime`, `interTime` defines
- LED + buzzer driven together in `dot()` / `dash()`

## [11f7be4] — 2026-02-27 14:10
### Added
- `minicom.sh` — serial terminal helper script

## [6475ce8] — 2026-02-27 13:47
### Changed
- Silenced clang and gcc warnings
- Moved to GNU C23 syntax

## [fc43ba4] — 2026-02-27 13:38
### Changed
- `-Werror -Wall -Wextra` enabled — warnings are now errors

## [1460116] — 2026-02-27 13:06
### Changed
- `makefiles.sh` cleanup and Qt Creator project file regeneration

## [d9348ce] — 2026-02-27 11:15
### Changed
- Morse transmitter simplified — no additional buffer needed

## [ea4b2d8] — 2026-02-27 09:13
### Changed
- GNU99 flags for C and C++ in Qt Creator project files

## [2de900a] — 2026-02-27 09:05
### Changed
- Moved from C17 to C99 standard in clang/Qt Creator flags

## [cafa413] — 2026-02-26 15:34
### Changed
- README updated

## [b57732d] — 2026-02-26 15:32
### Changed
- System clock confirmed working at 96MHz (HSE 12MHz, PLL ×8)

## [3e23d3c] — 2026-02-26 15:23
### Added
- USB OTG FS device + system clock configuration in CubeMX

## [1fe0e96] — 2026-02-26 13:53
### Added
- Second commit — project structure established

## [7ec1732] — 2026-02-26 13:52
### Added
- First commit — STM32F411CEU6 HAL bootstrap with Qt Creator project files generator
