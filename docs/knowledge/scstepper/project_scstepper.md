---
name: project_scstepper
description: scstepper - STM32F411 stepper motor controller firmware with USB CDC CLI, trapezoidal ramp, EEPROM params, button/endstop debounce
type: project
---

STM32F411CEU6 (Black Pill v3.1) stepper motor controller firmware.

Key architecture:
- USB CDC virtual COM port for CLI commands
- Trapezoidal ramp via precomputed accel/decel tables (512 entries max)
- TIM2 CH3 PWM for step pulses, TIM9 for 1ms debounce tick
- EEPROM emulation in flash sectors 6&7 for persistent params
- Two-phase button debounce: EXTI arms, TIM9 confirms after window
- CRC self-check at boot (patched by scripts/crc_patch.py)
- Morse code buzzer feedback (non-blocking state machine)

**Why:** Building a physical CNC/positioning machine with stepper motor, endstops, jog/step buttons.

**How to apply:** All changes must consider ISR safety, motor safety (never leave motor running without feedback), and CubeMX regeneration compatibility (keep user code in USER CODE sections).
