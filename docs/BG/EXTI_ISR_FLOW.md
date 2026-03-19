# EXTI ISR Поток

```mermaid
flowchart TD
    ENTRY[EXTI IRQ Вход] --> SNAP["snapA = GPIOA->IDR\nsnapB = GPIOB->IDR"]
    SNAP --> BEEP[buzzRequest = 1]
    BEEP --> SW{GPIO_Pin?}

    %% ---- Крайни изключватели ----
    SW -->|ES_L / ES_R| ES_EN{endstopsEn?}
    ES_EN -->|Не| EXIT([return])
    ES_EN -->|Да| ES_DIAG{diagMode?}

    ES_DIAG -->|Не = нормален| ES_LOCK{"now - lastTick >= 30ms?\n(edge lockout)"}
    ES_LOCK -->|Не| EXIT
    ES_LOCK -->|Да| ES_ACT["lastTick = now\nStepper_Stop()\nevtFlags |= EVT_ES_x"]
    ES_ACT --> EXIT

    ES_DIAG -->|Да = диагностика| ES_DEB{"now - lastTick >= 30ms?\n(дебаунс)"}
    ES_DEB -->|Не| EXIT
    ES_DEB -->|Да| ES_DACT["lastTick = now\nevtFlags |= EVT_ES_x\n(БЕЗ спиране в диагностика)"]
    ES_DACT --> EXIT

    %% ---- Jog бутони ----
    SW -->|JOGL / JOGR| JOG_EN{buttonsEn?}
    JOG_EN -->|Не| EXIT
    JOG_EN -->|Да| JOG_EDGE{"snapA & JOG_Pin?\n(HIGH = пуснат)"}

    JOG_EDGE -->|HIGH = Пускане| JOG_REL["Stepper_Stop()\nlastTick = now\nevtFlags |= EVT_JOGx_UP"]
    JOG_REL --> EXIT

    JOG_EDGE -->|LOW = Натискане| JOG_DEB{"now - lastTick >= 50ms?\n(DEBOUNCE_REL_MS)"}
    JOG_DEB -->|Не| EXIT
    JOG_DEB -->|Да| JOG_ES{"snapA & ES_x_Pin?\n(HIGH = ES свободен)"}
    JOG_ES -->|"LOW = ES активен\n(блокиран)"| EXIT
    JOG_ES -->|"HIGH = ES свободен"| JOG_ACT["lastTick = now\njogPressTick = now\nevtFlags |= EVT_JOGx_DN"]
    JOG_ACT --> EXIT

    %% ---- Step бутони ----
    SW -->|STEPL / STEPR| STEP_EN{buttonsEn?}
    STEP_EN -->|Не| EXIT
    STEP_EN -->|Да| STEP_ES{"snapA & ES_x_Pin?\n(HIGH = ES свободен)"}
    STEP_ES -->|"LOW = ES активен\n(блокиран)"| EXIT
    STEP_ES -->|"HIGH = ES свободен"| STEP_DEB{"now - lastTick >= 30ms?\n(DEBOUNCE_MS)"}
    STEP_DEB -->|Не| EXIT
    STEP_DEB -->|Да| STEP_ACT["lastTick = now\nevtFlags |= EVT_STEPx"]
    STEP_ACT --> EXIT

    %% Стилове
    style ENTRY fill:#2c3e50,color:#fff
    style EXIT fill:#555,color:#fff
    style SNAP fill:#3498db,color:#fff
    style ES_ACT fill:#e74c3c,color:#fff
    style ES_DACT fill:#e67e22,color:#fff
    style JOG_REL fill:#e74c3c,color:#fff
    style JOG_ACT fill:#27ae60,color:#fff
    style STEP_ACT fill:#27ae60,color:#fff
```

## Времеви константи

| Константа | Стойност | Използва се за |
|-----------|----------|----------------|
| `DEBOUNCE_MS` | 30 ms | Edge lockout на крайни изключватели, натискане на step бутон |
| `DEBOUNCE_REL_MS` | 50 ms | Jog дебаунс след пускане (бутоните отскачат повече при пускане) |
| `JOG_HOLD_MS` | 300 ms | Праг на задържане: кратко натискане → стъпка, дълго задържане → непрекъснато |

## Ключови дизайнерски решения

### snapA/snapB — GPIO snapshot при влизане в ISR
`GPIOA->IDR` се чете **веднъж** в началото на ISR в `snapA`.
Всички следващи проверки на пинове използват този snapshot — не live четения.
Гарантира консистентен изглед на GPIO дори ако пиновете продължат да отскачат по време на изпълнение на ISR.

### Edge lockout на крайни изключватели (30ms) в нормален режим
Крайните изключватели НЕ са "без дебаунс" — имат 30ms edge lockout.
Предотвратява множество събития за спиране от едно механично удряне (EMI / вибрации).
`Stepper_Stop()` се задейства при **първия** фронт, следващите фронтове в рамките на 30ms се игнорират.

### Jog пускане: незабавно спиране + release lockout
Пускането е безусловно: `Stepper_Stop()` се задейства незабавно.
След това `lastTick = now` нулира дебаунс прозореца на **50ms**.
Всяко bounce-натискане в рамките на 50ms след пускане се потиска.
Предотвратява фалшиви второ jog движения от механичен bounce на бутона.

### Проверка на посока на крайните изключватели в ISR (snapA)
Jog и step бутоните проверяват съответния крайен изключвател чрез `snapA` директно в ISR.
Блокирана посока се отхвърля на ISR ниво — не се поставя събитие в опашката, не е нужна обработка в main loop. Бързо и чисто.

### NVIC приоритети — безопасност на Stepper_Stop()
TIM2 (ISR за stepper импулси) и ES/JOG EXTI обработчиците са всички с **приоритет 0**.
ISR с еднакъв приоритет не могат да се прекъсват взаимно на Cortex-M4.
Следователно `Stepper_Stop()`, викан от EXTI ISR, е атомарен спрямо TIM2 ISR —
не е нужна критична секция вътре в `Stepper_Stop()`.

### buzzRequest — отложен звуков сигнал
Зумерът НЕ се превключва директно в ISR (би предизвикал race condition с MorseUpdate в main loop).
Вместо това се задава `buzzRequest = 1`, а main loop обработва звуковия сигнал когато морзето не е активно.
