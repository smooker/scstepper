# Credits

Technologies, tools, and libraries used in the **scstepper** project.

---

## Hardware

| Component | Description |
|-----------|-------------|
| **STM32F411CEU6** | ARM Cortex-M4 microcontroller, 96 MHz, 512 KB Flash, 128 KB RAM |
| **WeAct Black Pill v3.1** | STM32F411CEU6 development board |
| **Black Magic Probe** | JTAG/SWD debugger — GDB server + UART, no OpenOCD needed |
| **Saleae Logic (FX2)** | 8-channel logic analyzer (fx2lafw firmware) |

---

## Firmware

| Component | Source | License |
|-----------|--------|---------|
| **STM32CubeF4 HAL** | [STMicroelectronics](https://github.com/STMicroelectronics/STM32CubeF4) | BSD-3-Clause |
| **STM32 USB Device Library (CDC)** | STMicroelectronics | BSD-3-Clause |
| **CMSIS** | ARM / STMicroelectronics | Apache-2.0 |
| **EEPROM emulation** | ST AN3969 adapted | BSD-3-Clause |

---

## Build Toolchain

| Tool | Description |
|------|-------------|
| **arm-none-eabi-gcc** | GCC cross-compiler for ARM bare-metal |
| **arm-none-eabi-gdb** | GDB for ARM bare-metal debugging |
| **GNU Make** | Build system |
| **STM32CubeMX** | Peripheral configuration and code generation (STMicroelectronics) |

---

## Debugging

| Tool | Source | License |
|------|--------|---------|
| **GDB Dashboard** | [cyrus-and/gdb-dashboard](https://github.com/cyrus-and/gdb-dashboard) — Andrea Cardaci | MIT |
| **PyCortexMDebug** | [bnahill/PyCortexMDebug](https://github.com/bnahill/PyCortexMDebug) | GPL-3.0 |
| **STM32F411.svd** | ARM CMSIS SVD — STMicroelectronics | — |

---

## Logic Analysis

| Tool | Description |
|------|-------------|
| **sigrok / sigrok-cli** | Open-source signal analysis framework |
| **fx2lafw** | Open-source firmware for FX2-based logic analyzers |
| **PulseView** | sigrok GUI frontend |
| **stepper_motor decoder** | sigrok protocol decoder — step/direction |
| **counter decoder** | sigrok protocol decoder — edge counter |

---

## Scripts & Utilities

| Tool | Description |
|------|-------------|
| **Python 3** | Speed channel generator (`go.sh speed`), sigrok `.sr` file manipulation |
| **minicom** | Serial terminal for CDC USB communication |
| **Perl** | Makefile include path extractor (`scripts/mf.pl`) |

---

## Development Environment

| Tool | Description |
|------|-------------|
| **Gentoo Linux** | Host OS |
| **Qt Creator** | IDE (project files via `scripts/makefiles.sh`) |
| **udev** | Device rules for deterministic `/dev/ttyBmpGdb`, `/dev/ttyBmpUart`, `/dev/ttyACMTarg` symlinks |

---

*Project: scstepper — stepper motor controller firmware*
*Hardware: smooker (LZ1CCM)*
*Firmware & tooling: smooker + Claude Sonnet 4.6*
