#!/bin/bash
# Run after CubeMX regenerates Makefile
# Patches CFLAGS to allow unused-parameter in HAL code

sed -i 's/-Werror -Wall -Wextra/-Werror -Wall -Wextra -Wno-error=unused-parameter/g' Makefile
echo "Makefile patched: -Wno-error=unused-parameter added"
