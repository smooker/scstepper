---
name: reference_dwt_cyccnt
description: DWT CYCCNT — hardware cycle counter on Cortex-M4, used for precise delays without ISR/timer dependency
type: reference
---

DWT CYCCNT on STM32F411 (Cortex-M4, 96 MHz):

- Enable: `CoreDebug->DEMCR |= TRCENA` + `DWT->CYCCNT = 0` + `DWT->CTRL |= CYCCNTENA`
- 32-bit counter, overflow at ~44.7 seconds
- Counts CPU cycles even during ISR — no jitter
- Does NOT count during WFI/sleep (not used in this project)
- Debugger may reset TRCENA on attach — re-enable in init
- Used for `delay_us()` replacing HAL_Delay in blocking morse and boot code
- Init must be before CRC check (morse("E") is called there)
