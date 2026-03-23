# scstepper — Code Audit Log

## 2026-03-23: CDC TX/RX Ring Buffer + snapA Boot Seed

Audited commits (newest first):
- `60c5ae5` fix: seed snapA/snapB at boot — prevents false endstop blocking
- `3c56983` fix: TxDrain race — advance txTail before CDC_Transmit_FS
- `4ba1f67` cdc: TX ring buffer + RX extern — eliminate MyCDC_Receive_FS bridge

### Findings

| Severity | Finding | Location |
|----------|---------|----------|
| **CRITICAL** | `TxDrain` test-and-set of `txBusy` is not atomic — ISR can preempt between the check and the set, causing double-transmit and double-advance of txTail | main.c TxDrain() |
| **WARNING** | `txTail` advanced before `CDC_Transmit_FS` — on `USBD_BUSY` (not just broken link) data is silently lost from the ring | main.c TxDrain() |
| **WARNING** | `tim9Ms` has two `static` definitions in one TU (lines ~199 and ~1302) — GCC accepts it but technically a C constraint violation | main.c |
| **INFO** | `txChunk` and `rxRing` lack `volatile`; no memory barriers around shared-buffer handoff — architecturally incorrect, safe on Cortex-M4 without cache | main.c |
| **INFO** | `_write` timeout resets on every byte enqueued, not per blocked-spin — total blocking time is unbounded if the ring slowly drains | main.c _write() |
| **INFO** | `settleEnd` comparison in `RunHomeEx` is not overflow-safe — correct idiom is `tim9Ms - start < 500`, not `tim9Ms < settleEnd` | main.c RunHomeEx() |
| **OK** | RX SPSC ring buffer — correctly structured (ISR owns head, main owns tail, overflow guard present) | main.c, usbd_cdc_if.c |
| **OK** | snapA/snapB seed at boot — correct and sufficient; refreshed by every subsequent EXTI and TIM9 debounce | main.c |
| **OK** | ZLP path — handled correctly, txBusy stays 1 until ZLP DataIn callback, then TxDrain fires | main.c MyPCD_DataInStageCallback() |
| **OK** | epnum == 0x01 check — correct (HAL passes physical EP number without direction bit) | main.c MyPCD_DataInStageCallback() |

### CRITICAL fix: TxDrain atomicity

`TxDrain` is called from both main context (`_write`, `TxWrite`) and ISR context (`MyPCD_DataInStageCallback`). The `txBusy` guard is not atomic — ISR can preempt between the check and the set, causing a second `CDC_Transmit_FS` call while the first transfer is in flight.

Proposed fix — disable IRQ around the test-and-set:

```c
static void TxDrain(void)
{
    __disable_irq();
    if (txBusy) { __enable_irq(); return; }
    uint16_t head = txHead;
    uint16_t tail = txTail;
    if (head == tail) { __enable_irq(); return; }
    txBusy = 1;
    txTail = tail;       /* advance before transmit (race fix from 3c56983) */
    __enable_irq();

    /* copy chunk — safe now, txBusy is claimed */
    uint16_t n = 0;
    while (tail != head && n < sizeof(txChunk)) {
        txChunk[n++] = txRing[tail];
        tail = (tail + 1) & TX_RING_MASK;
    }

    txTail = tail;       /* final advance after copy */
    if (CDC_Transmit_FS(txChunk, n) != USBD_OK) {
        txBusy = 0;
    }
}
```

Note: `__disable_irq()` window is ~10 cycles (snapshot 3 variables + set flag). No impact on ISR latency.

### WARNING fix: tim9Ms double definition

Remove the forward declaration at line ~199 — keep only the single definition near the debounce code (line ~1302).

### INFO: settleEnd overflow

Replace `while (tim9Ms < settleEnd)` with `while (tim9Ms - start < 500)` in `RunHomeEx` to handle 32-bit wrap at ~49 days uptime.
