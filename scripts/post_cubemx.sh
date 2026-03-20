#!/bin/bash
# post_cubemx.sh — run after every CubeMX regeneration
# Restores project customizations that CubeMX overwrites.
set -e
cd "$(dirname "$0")/.."

echo ""
echo "=== post_cubemx ==="
echo ""

# ── 1. Check Makefile vs template ────────────────────────────────────────────
echo "--- [1/3] Makefile vs Makefile.template ---"
if diff -q Makefile Makefile.template > /dev/null 2>&1; then
    echo "  Makefile matches template — no change needed"
else
    echo ""
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "!!  MAKEFILE DIFFERS FROM TEMPLATE — CubeMX overwrote it            !!"
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    diff Makefile Makefile.template || true
    echo ""
    echo "  Restoring Makefile from template..."
    cp Makefile.template Makefile
    echo "  Done."
fi
echo ""

# ── 2. Fix GPIO mode: RISING / FALLING → RISING_FALLING in MX_GPIO_Init ─────
echo "--- [2/3] Fix GPIO_MODE_IT_RISING/FALLING → GPIO_MODE_IT_RISING_FALLING ---"
FILE=Core/Src/main.c
BEFORE_R=$(grep -c "GPIO_MODE_IT_RISING[^_]" "$FILE" || true)
BEFORE_F=$(grep -c "GPIO_MODE_IT_FALLING[^_]" "$FILE" || true)
sed -i 's/GPIO_MODE_IT_RISING\b/GPIO_MODE_IT_RISING_FALLING/g' "$FILE"
sed -i 's/GPIO_MODE_IT_FALLING\b/GPIO_MODE_IT_RISING_FALLING/g' "$FILE"
AFTER=$(grep -c "GPIO_MODE_IT_RISING_FALLING" "$FILE" || true)
echo "  $FILE: fixed RISING=$BEFORE_R + FALLING=$BEFORE_F → $AFTER GPIO_MODE_IT_RISING_FALLING"
echo ""

# ── 3. Verify Makefile matches template ──────────────────────────────────────
echo "--- [3/3] make check ---"
make check
