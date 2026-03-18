# Logic Analyzer Captures

Sigrok `.sr` files captured with FX2 Saleae clone (fx2lafw driver).
Open with PulseView or `sigrok-cli`.

## Development Timeline

### Phase 1 — Bug Discovery (2026-03-13 morning)

The decel ramp bug was found: `decelIndex = 0` caused an instant 10× speed drop
at CONST→DECEL transition instead of a smooth ramp-down.

| File | Time | Rate | Content |
|------|------|------|---------|
| `capture_all.sr` | 06:49 | 1 MHz | **Pre-fix** `move 10`, 8ch — the capture that proved the bug |
| `capture_fixed.sr` | 07:30 | 1 MHz | **Post-fix** — first capture with working decel ramp |

### Phase 2 — Directional Verification (2026-03-13 ~08:26)

Verified both directions produce identical timing after the fix.

| File | Time | Rate | Content |
|------|------|------|---------|
| `capture_mover.sr` | 08:26 | 100 kHz | `mover 5` — CW direction only |
| `capture_movel.sr` | 08:26 | 100 kHz | `movel 5` — CCW direction only |
| `capture_both.sr` | 08:28 | 100 kHz | `mover 5` + `movel 5` in one capture |
| `capture_both_speed.sr` | — | 100 kHz | Same with speed analog channel |

### Phase 3 — Profile Shapes (2026-03-13 ~09:48)

Tested triangular (short move) vs trapezoidal (long move) profiles.
Symmetric ramps: dvdtacc = dvdtdecc = 50 mm/s².

| File | Time | Rate | Content |
|------|------|------|---------|
| `capture_triangle.sr` | 09:48 | 100 kHz | Short move — triangular profile |
| `capture_triangle_speed.sr` | 09:51 | 100 kHz | Same with speed analog channel |
| `capture_combo.sr` | 09:57 | 100 kHz | 2× triangular + 2× trapezoidal, symmetric |
| `capture_combo_speed.sr` | 10:11 | 100 kHz | Same with speed analog channel |

### Phase 4 — Jog Button (2026-03-13 ~11:46)

Captured jog button press/release behavior.

| File | Time | Rate | Content |
|------|------|------|---------|
| `capture_jog.sr` | 11:46 | 1 MHz | Jog button movements |
| `capture_jog_speed.sr` | 11:46 | 1 MHz | Same with speed analog channel |
| `capture_test.sr` | 11:49 | — | Quick test capture |

### Phase 5 — Asymmetric Ramps (2026-03-13 ~12:58)

Final validation with asymmetric ramps: dvdtacc=50, dvdtdecc=100 mm/s².
Decel is 2× steeper — confirmed by capture data (ratio = 2.02×).

| File | Time | Rate | Content |
|------|------|------|---------|
| `capture_combo4.sr` | 12:58 | 1 MHz | Asymmetric combo, pulse only |
| `capture_combo4_speed.sr` | 13:00 | 1 MHz | Same with speed analog channel |
| `capture_combo_final.sr` | 13:08 | 1 MHz | Final asymmetric combo |
| `capture_combo_final_speed.sr` | 13:08 | 1 MHz | **Definitive capture** — asymmetric ramps + speed |

## Viewing

```bash
# PulseView GUI (from host)
pulseview captures/capture_combo_final_speed.sr

# sigrok-cli decode
sigrok-cli -i captures/capture_combo_final_speed.sr -C D7 -P timing:data=D7
```

## Channel Mapping

- **D7** = PULSE (PB10) — step pulses to CWD556 driver
- **D6** = DIR (PB14) — direction signal
- **D0-D5** = static HIGH (unused)

## Parameters Reference

| Param | Symmetric | Asymmetric |
|-------|-----------|------------|
| spmm | 400 | 400 |
| mmpsmax | 10.0 mm/s | 10.0 mm/s |
| mmpsmin | 1.0 mm/s | 1.0 mm/s |
| dvdtacc | 50.0 mm/s² | 50.0 mm/s² |
| dvdtdecc | 50.0 mm/s² | 100.0 mm/s² |
