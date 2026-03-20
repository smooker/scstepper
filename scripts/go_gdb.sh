#!/bin/bash
cd "$(dirname "$0")/.."
arm-none-eabi-gdb -x ./initcfg/gdbinit -x ./scripts/script2.gdb
