---
name: feedback_home_cdc_block
description: home command blocks CDC output for ~2min — _write timeout fills TX ring during blocking RunHomeEx loop
type: feedback
---

`home` command blocks CDC TX for the duration of RunHomeEx (~2 minutes). printf() output from inside the homing loop never arrives until home completes. The prompt `0.00 >` comes only after RunHomeEx returns.

**Why:** `_write()` pushes to TX ring, calls `TxDrain()` → `CDC_Transmit_FS()`. During blocking loops with `HAL_Delay(5)`, USB OTG ISR processes DataIn callbacks but CDC link may not drain properly. TX ring fills up → `_write` timeout (100ms per byte) → all printf output is lost.

**How to apply:** After sending `home`, do NOT wait for prompt. Wait a fixed time (5-10s for approach + backoff), then probe with CR/uptime. Same pattern for any blocking firmware command (range works because it's faster). This is a firmware CDC issue to investigate separately.
