---
name: feedback_no_hal_delay
description: Never use HAL_Delay in project — use tim9Ms (TIM9 1ms tick) for all timing
type: feedback
---

Never use `HAL_Delay()` in this project. Use `tim9Ms` (TIM9 ISR, 1ms tick) for all delays and timing.

**Why:** `HAL_Delay` uses SysTick. Project uses TIM9 as its own time source. Mixing SysTick and TIM9 can cause timing conflicts. Also `HAL_Delay` may interfere with USB OTG ISR processing, blocking CDC TX drain — this is the suspected cause of `home` blocking CDC output for ~2 minutes.

**How to apply:** Replace `HAL_Delay(N)` with `{ uint32_t t = tim9Ms; while (tim9Ms - t < N); }` everywhere. Search for `HAL_Delay` in user code and replace.
