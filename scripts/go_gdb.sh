#!/bin/bash
cd "$(dirname "$0")/.."
arm-none-eabi-gdb -x ./initcfg/gdbinit -x ./script2.gdb
