# stepper_sc — Firmware

STM32F411CEU6 (WeAct Black Pill v3.1) stepper motor controller firmware.

## Build

Requires ST HAL — not included in repo (57MB). Generate via STM32CubeMX using `stepper_sc.ioc`, or download from [st.com](https://www.st.com).

```bash
make clean && make
# output: build/stepper.{elf,hex,bin}
```

Toolchain: `arm-none-eabi-gcc 14.x`

## Flash & Debug

Via **Black Magic Probe** on `/dev/ttyBmpGdb`:

```bash
./go_gdb.sh
# inside GDB:
#   ag   → connect + attach
#   ld   → flash + verify
```

SVD register viewer: `firmware/PyCortexMDebug/` (git submodule) + `STM32F411.svd`

See [docs/build-and-debug.md](docs/build-and-debug.md) for full instructions.

## Key Signals

| Pin | Signal | Direction | Connector |
|-----|--------|-----------|-----------|
| PB10 | PULSE | out → NPN buffer → J2 | J11B pin 17 |
| PB14 | DIR   | out → NPN buffer → J3 | J11A pin 3  |
| PB15 | BUZZ  | out → J10             | J11A pin 4  |
| PA3  | ES_L  | in ← J4              | J11B pin 9  |
| PA4  | ES_R  | in ← J5              | J11B pin 10 |
| PA6  | JOGL  | in ← J6              | J11B pin 12 |
| PA7  | JOGR  | in ← J7              | J11B pin 13 |
| PB0  | STEPL | in ← J8              | J11B pin 14 |
| PB1  | STEPR | in ← J9              | J11B pin 15 |

Pin assignments are the source of truth for the PCB netlist — see `fw_to_net.py`.

## Motion

- Trapezoidal ramp: `BuildRampTables()` precomputes accel/decel lookup tables (512 entries)
- TIM2 CH3 → PB10 STEP pulse (50µs PWM)
- USB CDC virtual COM — `printf` via `_write()`, RX ring buffer

## EEPROM

Flash sectors 6 & 7, 7 parameters: `mmpsmax`, `mmpsmin`, `dvdtacc`, `dvdtdecc`, `jogmm`, `stepmm`, `spmm`

## Docs

- [Build and Debug](docs/build-and-debug.md) — toolchain setup, GDB, flashing, pinout, CLI commands
- [Capture Analysis](docs/capture_analysis.md) — FX2/sigrok logic capture of `move 10`, ramp bug analysis
- [Credits](CREDITS.md) — technologies, tools, and libraries used
