# EXTI ISR Flow

```mermaid
flowchart TD
    ENTRY[EXTI IRQ Entry] --> SNAP["snapA = GPIOA->IDR\nsnapB = GPIOB->IDR"]
    SNAP --> BEEP[buzzRequest = 1]
    BEEP --> SW{GPIO_Pin?}

    %% ---- Endstops ----
    SW -->|ES_L / ES_R| ES_EN{endstopsEn?}
    ES_EN -->|No| EXIT([return])
    ES_EN -->|Yes| ES_DIAG{diagMode?}

    ES_DIAG -->|No = normal| ES_LOCK{"now - lastTick >= 30ms?\n(edge lockout)"}
    ES_LOCK -->|No| EXIT
    ES_LOCK -->|Yes| ES_ACT["lastTick = now\nStepper_Stop()\nevtFlags |= EVT_ES_x"]
    ES_ACT --> EXIT

    ES_DIAG -->|Yes = diag| ES_DEB{"now - lastTick >= 30ms?\n(debounce)"}
    ES_DEB -->|No| EXIT
    ES_DEB -->|Yes| ES_DACT["lastTick = now\nevtFlags |= EVT_ES_x\n(NO stop in diag)"]
    ES_DACT --> EXIT

    %% ---- Jog buttons ----
    SW -->|JOGL / JOGR| JOG_EN{buttonsEn?}
    JOG_EN -->|No| EXIT
    JOG_EN -->|Yes| JOG_EDGE{"snapA & JOG_Pin?\n(HIGH = released)"}

    JOG_EDGE -->|HIGH = Release| JOG_REL["Stepper_Stop()\nlastTick = now\nevtFlags |= EVT_JOGx_UP"]
    JOG_REL --> EXIT

    JOG_EDGE -->|LOW = Press| JOG_DEB{"now - lastTick >= 50ms?\n(DEBOUNCE_REL_MS)"}
    JOG_DEB -->|No| EXIT
    JOG_DEB -->|Yes| JOG_ES{"snapA & ES_x_Pin?\n(HIGH = ES clear)"}
    JOG_ES -->|"LOW = ES active\n(blocked)"| EXIT
    JOG_ES -->|"HIGH = ES clear"| JOG_ACT["lastTick = now\njogPressTick = now\nevtFlags |= EVT_JOGx_DN"]
    JOG_ACT --> EXIT

    %% ---- Step buttons ----
    SW -->|STEPL / STEPR| STEP_EN{buttonsEn?}
    STEP_EN -->|No| EXIT
    STEP_EN -->|Yes| STEP_ES{"snapA & ES_x_Pin?\n(HIGH = ES clear)"}
    STEP_ES -->|"LOW = ES active\n(blocked)"| EXIT
    STEP_ES -->|"HIGH = ES clear"| STEP_DEB{"now - lastTick >= 30ms?\n(DEBOUNCE_MS)"}
    STEP_DEB -->|No| EXIT
    STEP_DEB -->|Yes| STEP_ACT["lastTick = now\nevtFlags |= EVT_STEPx"]
    STEP_ACT --> EXIT

    %% Styling
    style ENTRY fill:#2c3e50,color:#fff
    style EXIT fill:#555,color:#fff
    style SNAP fill:#3498db,color:#fff
    style ES_ACT fill:#e74c3c,color:#fff
    style ES_DACT fill:#e67e22,color:#fff
    style JOG_REL fill:#e74c3c,color:#fff
    style JOG_ACT fill:#27ae60,color:#fff
    style STEP_ACT fill:#27ae60,color:#fff
```

## Timing Constants

| Constant | Value | Used for |
|----------|-------|----------|
| `DEBOUNCE_MS` | 30 ms | Endstop edge lockout, step button press |
| `DEBOUNCE_REL_MS` | 50 ms | Jog press debounce after release (switches bounce more on release) |
| `JOG_HOLD_MS` | 300 ms | Hold duration threshold: short press → step, long hold → continuous |

## Key Design Decisions

### snapA/snapB — GPIO snapshot at ISR entry
`GPIOA->IDR` is read **once** at the top of the ISR into `snapA`.
All subsequent pin checks use this snapshot — not live reads.
This ensures a consistent view of GPIO state even if pins continue to bounce during ISR execution.

### Endstop edge lockout (30ms) in normal mode
Endstops are NOT "no debounce" — they have a 30ms edge lockout.
This prevents multiple stop events from a single mechanical hit (EMI / vibration).
`Stepper_Stop()` fires on the **first** edge, subsequent edges within 30ms are ignored.

### Jog release: instant stop + release lockout
Release is unconditional: `Stepper_Stop()` fires immediately.
Then `lastTick = now` resets the debounce window to **50ms**.
Any bounce-press within 50ms after release is suppressed.
This prevents spurious second jog moves from mechanical button bounce.

### Endstop direction check in ISR (snapA)
Jog and step buttons check the relevant endstop pin via `snapA` directly in the ISR.
This means a blocked direction is rejected at ISR level — no event is queued,
no main loop processing needed. Fast and clean.

### NVIC priorities — Stepper_Stop() safety
TIM2 (stepper pulse ISR) and ES/JOG EXTI handlers are all at **priority 0**.
Same-priority ISRs cannot preempt each other on Cortex-M4.
Therefore `Stepper_Stop()` called from EXTI ISR is atomic with respect to TIM2 ISR —
no critical section needed inside `Stepper_Stop()`.

### buzzRequest — deferred beep
Buzzer is NOT toggled directly in the ISR (would race with MorseUpdate in main loop).
Instead `buzzRequest = 1` is set, and the main loop handles the beep
when morse is not active.
