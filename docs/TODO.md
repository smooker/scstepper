# TODO

## Cross-parameter validation in Stepper_SetParam()

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
