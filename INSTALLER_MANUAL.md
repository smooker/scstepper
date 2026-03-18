# stepper_sc Installer Manual

## First-Time Setup

### 1. Determine Steps Per Millimeter (`spmm`)

Calculate based on your stepper driver microstepping and lead screw pitch:

```
spmm = (motor_steps_per_rev × microstepping) / lead_screw_pitch_mm
```

Example: 200-step motor, 1/8 microstepping, 4mm pitch lead screw:
```
spmm = (200 × 8) / 4 = 400
```

Set and save:
```
set spmm 400
save
```

### 2. Determine DIR Inversion (`dirinv`)

The stepper driver may invert direction due to optocoupled inputs.

1. Set `dirinv 0` (default)
2. Run `mover 5` — observe rotation
3. If CW → correct, leave `dirinv 0`
4. If CCW → set `dirinv 1`
5. `save`

**Convention**: `mover` = positive = CW, `movel` = negative = CCW.

### 3. Set Homing Speed (`homespd`)

Approach speed for the `home` command. Backoff speed is automatically `homespd / 10`.

```
set homespd 1.0
save
```

Recommended: start slow (1.0 mm/s), increase after confirming endstop reliability.

### 4. Set Homing Offset (`homeoff`)

Distance in **steps** to park away from the switch after backoff.

```
set homeoff 400
save
```

At spmm=400, 400 steps = 1mm.

### 5. Set Working Speeds

| Parameter | Description | Recommended Start |
|-----------|-------------|-------------------|
| `mmpsmax` | Max velocity for moves and jog | 10.0 mm/s |
| `mmpsmin` | Start/end velocity of ramps | 1.0 mm/s |
| `dvdtacc` | Acceleration | 50.0 mm/s² |
| `dvdtdecc` | Deceleration (can differ from accel) | 100.0 mm/s² |
| `jogmm` | Jog button travel distance | 1.0 mm |
| `stepmm` | Step button travel distance | 1.0 mm |

```
set mmpsmax 10
set mmpsmin 1
set dvdtacc 50
set dvdtdecc 100
set jogmm 1
set stepmm 1
save
```

### 6. Verify Endstops

1. Run `di` (diag_inputs mode)
2. Manually press each endstop switch
3. Confirm `ES_L hit` and `ES_R hit` appear correctly
4. Run `di` again to exit diag mode

### 7. Test Homing

1. Position the motor away from both endstops
2. Run `home`
3. Expected output:
```
home: approach ES_L @ 1.0 mm/s CCW
home: ES_L confirmed, settling...
home: backoff @ 0.10 mm/s CW
home: ES_L released, +400 steps CW
home: done
```

If you see `home: ABORT` — ES_L was not physically pressed (EMI false trigger or wrong endstop wired).

### 8. Verify with Combo

Run `combo` to test all 4 ramp profiles:
- Triangle left (1mm)
- Triangle right (1mm)
- Trapezoid left (5mm)
- Trapezoid right (5mm)

## EEPROM Parameter Map

| Address | Parameter | Type | Default | Unit |
|---------|-----------|------|---------|------|
| 1 | mmpsmax | float | 50.0 | mm/s |
| 2 | mmpsmin | float | 1.0 | mm/s |
| 3 | dvdtacc | float | 100.0 | mm/s² |
| 4 | dvdtdecc | float | 80.0 | mm/s² |
| 5 | jogmm | float | 1.0 | mm |
| 6 | stepmm | float | 1.0 | mm |
| 7 | spmm | uint32 | 80 | steps/mm |
| 8 | dirinv | uint32 | 0 | 0=normal, 1=invert |
| 9 | homespd | float | 1.0 | mm/s |
| 10 | homeoff | uint32 | 400 | steps |
| 11 | debug | uint32 | 0 | bitfield (bit0: verbose button msgs) |

All stored as raw `uint32_t` (floats via union bit-cast). Dual-page wear-leveling
in flash sectors 6 & 7 (128 KB each).

## Wiring

### Motor Driver

| Signal | MCU Pin | Notes |
|--------|---------|-------|
| PULSE | PB10 (TIM2_CH3) | PWM output, 50µs pulse width |
| DIR | PB14 | GPIO output, set before first pulse |

If driver has optocoupled inputs (common): connect MCU pin → opto+ via 3.3V,
opto common → GND. Set `dirinv 1` if direction is inverted.

### Endstops

| Signal | MCU Pin | Notes |
|--------|---------|-------|
| ES_L | PA3 | Active low, internal pull-up. Home endstop |
| ES_R | PA4 | Active low, internal pull-up |

**Recommended**: add 4.7kΩ external pull-up + 100nF cap (GND) on each endstop
line to suppress EMI. Internal 40kΩ pull-ups are insufficient for noisy environments
(solar inverters, motor drivers).

### Buttons

| Signal | MCU Pin | Notes |
|--------|---------|-------|
| JOG L | PA6 | Active low, internal pull-up |
| JOG R | PA7 | Active low, internal pull-up |
| STEP L | PB0 | Active low, internal pull-up |
| STEP R | PB1 | Active low, internal pull-up |

Same recommendation: external pull-ups + caps for EMI-heavy environments.

### Buzzer

| Signal | MCU Pin | Notes |
|--------|---------|-------|
| BUZZ | PB15 | Active low — self-oscillating buzzer |

### Debug / USB

| Signal | MCU Pin | Notes |
|--------|---------|-------|
| USB D- | PA11 | USB CDC virtual COM port |
| USB D+ | PA12 | USB CDC virtual COM port |
| SWDIO | PA13 | SWD debug (Black Magic Probe) |
| SWCLK | PA14 | SWD debug (Black Magic Probe) |
| LED | PC13 | Heartbeat (500ms toggle) |

## EMI Considerations

Solar inverters and stepper motor drivers generate switching noise that couples
into long unshielded wires. Symptoms:

- False endstop triggers (`ES_L hit` / `ES_R hit` without physical press)
- Phantom button presses (ghost jog/step moves)
- Boot self-test stuck in CQ loop

**Hardware fix (PCB)**: 4.7kΩ pull-up to 3.3V + 100nF cap to GND on each input pin.

**Software mitigations** (already implemented):
- Boot input self-test with stuck detection
- ES_L excluded from boot self-test (motor may be parked on home switch)
- `buttons off` / `endstops off` commands
- Homing uses debounced GPIO polling (not EXTI)
- 30ms debounce on buttons in normal mode
- Endstop direction blocking (jog blocked toward triggered endstop)
- Jog release: immediate ISR-level stop, no debounce

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Motor doesn't move | Power supply off | Turn on motor driver power |
| `mover` goes CCW | DIR inverted | `set dirinv 1` then `save` |
| Boot stuck in CQ loop | Input stuck LOW (EMI or wiring) | Fix wiring, add pull-ups, or wait for sunset |
| `home` ABORT | EMI triggers false ES_L | Will resolve with external pull-ups on PCB |
| Stray characters on terminal | Minicom init strings | Fixed: RX buffer flushed before main loop |
| Wrong endstop on `home` | ES_L/ES_R wires swapped | Swap wires at connector |
