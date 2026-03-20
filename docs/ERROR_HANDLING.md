# Error Handling & Fault Handlers

## Overview

On any crash or unhandled exception, the firmware calls `SafeState_And_Blink()` which:

1. Disables all interrupts (`__disable_irq()`)
2. Sets all functional GPIO pins to **Hi-Z** (input, no pull-up/down) — stops motor, silences buzzer
3. Fast-blinks the LED (~16 Hz) as a visual crash indicator
4. Loops forever — reset required to recover

## SafeState_And_Blink() — Pin Actions

| Pin | Label   | Normal State  | Safe State (after fault) |
|-----|---------|---------------|--------------------------|
| PB10 | PULSE  | TIM2 CH3 PWM output | Input, no pull — Hi-Z |
| PB14 | DIR    | GPIO output   | Input, no pull — Hi-Z |
| PB15 | BUZZ   | GPIO output (active low) | Input, no pull — silent |
| PA3  | ES_L   | Input + internal pull-up | Input, no pull — Hi-Z |
| PA4  | ES_R   | Input + internal pull-up | Input, no pull — Hi-Z |
| PA6  | BUTT_JOGL | Input + internal pull-up | Input, no pull — Hi-Z |
| PA7  | BUTT_JOGR | Input + internal pull-up | Input, no pull — Hi-Z |
| PB0  | BUTT_STEPL | Input + internal pull-up | Input, no pull — Hi-Z |
| PB1  | BUTT_STEPR | Input + internal pull-up | Hi-Z |
| PC13 | LED_USER | GPIO output  | GPIO output — fast blink |

> External 10 kΩ pull-ups on PCB keep ES_L/R and button lines at 3.3 V (inactive/released state) when MCU releases internal pull-ups. No spurious triggers.

## Fault Handler Table

| Handler | Source | Action |
|---------|--------|--------|
| `Error_Handler` | HAL assert failures, peripheral init errors | `SafeState_And_Blink()` |
| `HardFault_Handler` | Invalid memory access, bad instruction fetch, divide by zero | `SafeState_And_Blink()` |
| `MemManage_Handler` | MPU access violation | `SafeState_And_Blink()` |
| `BusFault_Handler` | Prefetch/data bus fault | `SafeState_And_Blink()` |
| `UsageFault_Handler` | Undefined instruction, unaligned access | `SafeState_And_Blink()` |
| `NMI_Handler` | Non-maskable interrupt (e.g. clock failure) | `SafeState_And_Blink()` |

## LED Behavior

| State | LED PC13 | Description |
|-------|----------|-------------|
| Normal | 1 Hz toggle (500 ms) | Main loop heartbeat |
| Homing / combo | 50 ms toggle (10 Hz) | Blocking operation in progress |
| **Fault (any handler above)** | **~16 Hz fast blink** | **Crash indicator — reset required** |
| GDB attached | Frozen | CPU halted by debugger |

## Implementation Notes

- `SafeState_And_Blink()` is defined in `Core/Src/main.c` (USER CODE END 4)
- `extern void SafeState_And_Blink(void);` declared in `Core/Src/stm32f4xx_it.c` (USER CODE PFP)
- Uses **direct GPIO register writes** (`GPIOX->MODER`, `GPIOX->PUPDR`, `GPIOX->ODR`) — no HAL_Delay, no SysTick dependency. Safe to call even if HAL is in an inconsistent state.
- Delay loop: `for (volatile uint32_t i = 0; i < 300000UL; i++)` ≈ ~10 ms at 96 MHz → ~16 Hz blink rate
- All USER CODE sections — survives CubeMX regeneration

## CubeMX Regeneration Safety

The safe state hooks are in USER CODE sections and survive CubeMX regeneration:

```
Core/Src/main.c         — USER CODE END 4  : SafeState_And_Blink() definition
                        — USER CODE BEGIN Error_Handler_Debug : call
Core/Src/stm32f4xx_it.c — USER CODE BEGIN PFP                : extern declaration
                        — USER CODE BEGIN HardFault_IRQn 0   : call
                        — USER CODE BEGIN MemoryManagement_IRQn 0 : call
                        — USER CODE BEGIN BusFault_IRQn 0    : call
                        — USER CODE BEGIN UsageFault_IRQn 0  : call
                        — USER CODE BEGIN NonMaskableInt_IRQn 0 : call
```
