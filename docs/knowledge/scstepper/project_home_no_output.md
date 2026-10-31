---
name: project_home_no_output
description: BUG - home command completes (motor stops) but CDC output and prompt never arrive — needs investigation
type: project
---

`home` command completes successfully (motor homes, stops), but NO output arrives via CDC after the echo. No intermediate printf, no final `0.00 >` prompt.

**Observed:** Motor moves, finds ES_L, backs off, parks, stops. But serial output from RunHomeEx printf() calls never arrives. Only the echo `home\r\n` comes through.

**Why:** Unknown. Possibly `_write()` TX ring fills up during blocking RunHomeEx while loop, `TxDrain` → `CDC_Transmit_FS` fails or `txBusy` stays stuck. Happens consistently after reset+boot. Does NOT happen when minicom was already connected before boot.

**How to apply:** This is a firmware CDC TX issue during blocking functions. Needs investigation: check txHead/txTail/txBusy state via GDB after home completes, or add diagnostic output to `_write` timeout path.
