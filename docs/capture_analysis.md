# Capture Analysis — Ramp Bug (2026-03-13)

## Setup

- **Logic analyzer**: FX2 Saleae clone, sigrok/fx2lafw driver
- **Capture**: 5M samples @ 1 MHz, 8 channels (D0-D7)
- **Command**: `move 10` (10mm travel)
- **Channel mapping**: D7 = PULSE (PB10), D0-D6 = static HIGH (DIR not visible on FX2)

## Results

| Phase | Pulses | Period | Frequency | Steps |
|-------|--------|--------|-----------|-------|
| Accel | 0–393 | 2147 → 240 us | 466 → 4167 Hz | ~394 |
| Const | 394–3603 | 240 us (steady) | 4167 Hz | ~3210 |
| Decel | 3604–3999 | 2400 us (flat!) | 417 Hz | ~396 |

- **Total pulses**: 4000 (= 400 spm × 10mm)
- **Total duration**: 1.890s
- **Max frequency**: 4167 Hz (240 us period)
- **Min frequency**: 416–466 Hz (2147–2401 us)

## Bug: No Deceleration Ramp

At pulse 3604 (CONST → DECEL transition), the period **jumps instantly from 240 us to 2400 us** — a 10× speed drop with zero ramp. The decel phase then stays flat at 2400 us for ~396 pulses.

### Root Cause (`stepper.c`)

The CONST → DECEL transition sets `decelIndex = 0`. But `decelTable` is built slow-to-fast:
- Index 0 = `maxPeriod` (slowest)
- Index `decelSize-1` = `minPeriod` (fastest)

So `decelIndex = 0` starts at the **slowest** entry instead of the fastest, then decrements — which immediately wraps/clips, keeping the period stuck at `maxPeriod`.

### Expected Behavior

```
Accel: 2147us ──smooth ramp──▶ 240us    (394 steps)
Const: 240us ──────────────── 240us      (3210 steps)
Decel: 240us ──smooth ramp──▶ ~2400us   (396 steps)  ← SHOULD mirror accel
```

### Actual Behavior

```
Accel: 2147us ──smooth ramp──▶ 240us    (394 steps)
Const: 240us ──────────────── 240us      (3210 steps)
Decel: 240us ──INSTANT JUMP──▶ 2400us   (flat for 396 steps)  ← BUG
```

### Fix

```c
// WRONG:
decelIndex = 0;

// CORRECT:
decelIndex = decelSize - 1;
```

Start decel from the fast end of the table, then decrement toward 0 (slow end). This produces a symmetric decel ramp matching the accel phase.

### Status

- [ ] Apply fix in `stepper.c`
- [ ] Re-capture to verify symmetric accel/decel ramps
