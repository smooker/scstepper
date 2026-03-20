#!/bin/bash
# post_cubemx.sh — run after every CubeMX regeneration
# Restores project customizations that CubeMX overwrites.
set -e
cd "$(dirname "$0")/.."

echo ""
echo "=== post_cubemx ==="
echo ""

# ── 1. Restore Makefile from template ────────────────────────────────────────
echo "--- [1/3] Restore Makefile from Makefile.template ---"
cp -v Makefile.template Makefile
echo ""

# ── 2. Fix GPIO mode: RISING → RISING_FALLING in MX_GPIO_Init ───────────────
echo "--- [2/3] Fix GPIO_MODE_IT_RISING → GPIO_MODE_IT_RISING_FALLING ---"
FILE=Core/Src/main.c
BEFORE=$(grep -c "GPIO_MODE_IT_RISING[^_]" "$FILE" || true)
sed -i 's/GPIO_MODE_IT_RISING\b/GPIO_MODE_IT_RISING_FALLING/g' "$FILE"
AFTER=$(grep -c "GPIO_MODE_IT_RISING_FALLING" "$FILE" || true)
echo "  $FILE: fixed $BEFORE occurrences → $AFTER GPIO_MODE_IT_RISING_FALLING"
echo ""

# ── 3. Verify Makefile matches template ──────────────────────────────────────
echo "--- [3/3] make check ---"
make check
