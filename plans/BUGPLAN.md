# BUGPLAN — scstepper firmware bugs

Analiz na bugovete i plan za fix. Vsichko e bazirano na source code review.

---

## NVIC Priority Map — STARO (CubeMX default)

| IRQ           | Priority | Kakvo e                    |
|---------------|----------|----------------------------|
| OTG_FS (USB)  | 0        | nai-visok, USB CDC         |
| TIM2          | 1        | stepper pulse ISR          |
| EXTI3 (ES_L)  | 1        | endstop lqvo               |
| EXTI4 (ES_R)  | 1        | endstop dqsno              |
| EXTI0 (STEPL) | 2        | step button lqvo           |
| EXTI1 (STEPR) | 2        | step button dqsno          |
| EXTI9_5 (JOG) | 2        | jog buttons (PA6, PA7)     |
| SysTick       | 15       | HAL_GetTick()              |

## NVIC Priority Map — NOVO (diskusiq 2026-03-18)

Princip: **movement i stop sa nai-vajni**. Device raboti standalone bez kompyutyr.
USB e s osobeno mnenie — mozhe da se vyrne na po-visok prioritet ako se nalozhi.

| IRQ           | Priority | Kakvo e                    | Promiana        |
|---------------|----------|----------------------------|-----------------|
| TIM2          | 0        | stepper pulse ISR          | **beshe 1**     |
| EXTI3 (ES_L)  | 0        | endstop lqvo              | **beshe 1**     |
| EXTI4 (ES_R)  | 0        | endstop dqsno             | **beshe 1**     |
| EXTI9_5 (JOG) | 0        | jog buttons (PA6, PA7)    | **beshe 2**     |
| EXTI0 (STEPL) | 2        | step button lqvo           | bez promiana    |
| EXTI1 (STEPR) | 2        | step button dqsno          | bez promiana    |
| OTG_FS (USB)  | 3        | USB CDC serial             | **beshe 0 !!!** |
| SysTick       | 15       | HAL_GetTick()              | bez promiana    |

**Zashto:**
- Prio 0: vsichko koeto dvizhq ili spirа motora — TIM2 pulse, endstops, jog release
- Prio 0 = ne mogat da se preemptvat vzaimno = Stepper_Stop() e atomaren ot ISR
- Prio 2: step butoni — fire-and-forget, ne vikat Stepper_Stop()
- Prio 3: USB — device raboti i bez kompyutyr, USB mozhe da pochaka
- Prio 15: SysTick — debounce/buzzer timing ne e kritichno (±1-2ms e OK)

**Osobeno mnenie za USB**: ako se nalozhi USB da e po-responsiven (naprimer
pri AI upravlenie v realno vreme), vdigame go obratno na 0-1. Zasega na 3.

---

## BUG 1: ES_R hit → keyboard (CLI) stava sluggish

### Root cause

`HAL_GPIO_EXTI_Callback()` v `main.c:1359` — kogato endstop udari:
1. ISR vika `Stepper_Stop()` (line 1382) — tova e OK
2. `ProcessEvents()` (line 1565-1569) setva `esBlocked = 1`

**Problemat e v `Stepper_Stop()` (stepper.c:357-369):**
```c
void Stepper_Stop(void)
{
    if (stepperState == STEPPER_CONST || stepperState == STEPPER_ACCEL)
    {
        decelIndex = 0;
        while (decelIndex < decelSize - 1 && decelTable[decelIndex] > currentPeriod)
            decelIndex++;
        stepsRemaining = decelIndex + 2;
        stepperState = STEPPER_DECEL;
    }
}
```

Stepper_Stop() se vika ot EXTI ISR (priority 1) — sashtat priority kato TIM2!
Na Cortex-M4 kogato dve ISR sa s ednakav priority, te NE mogat da se preemptat edna druga.
Tova znachi: dokato EXTI ISR raboti, TIM2 ISR e blokiran.

No tuk `Stepper_Stop()` e barz — tova ne e problemat.

**Istinskata prichina**: sled ES_R hit, `esBlocked = 1`. Sled tova vseki EXTI event ot butoni prosto se ignorirava s "blocked" check (line 1594: `else if (esBlocked == 1)`). No `esBlocked` se clearva SAMO v jogL/jogR press handlers (line 1579/1598: `esBlocked = 0`) — t.e. samo pri uspeshen jog v obratna posoka.

AKO userat natisne step button (STEPL/STEPR) — tezi NE clearvat esBlocked!
Step buttons (line 1625-1632) vikat `Stepper_Move()` directno — no `Stepper_Move()` vika `StartMove()` koito proverqva samo `stepperState != STEPPER_IDLE`, NE proverqva `esBlocked`.

**Taka che step buttons rabotyat, no jog buttons sa blocked.**

**OBACHE** — veroqtno CLI sluggishness e ot drug problem: kogato motorat decelira sled Stop(), TIM2 ISR (priority 1) se fire-va na vseki step pulse. Ако deceleration e bavna i ima mnogo steps — TIM2 zaema mnogo CPU vreme. USB OTG e priority 0 taka che priema bytes OK, no `_write()` (printf) chaka `hcdc->TxState` v busy-wait loop (main.c:527-530) — i tova stava bavno zashtoto main loop e pre-empted ot TIM2 ISR.

**Oshte po-vajno**: sled ES_R hit, ako motorat NE e bil v dvizhenie (STEPPER_IDLE), `Stepper_Stop()` ne pravi nishto (return sled if-check). No EXTI callback (line 1364-1366) VINAGI pali buzzer — tova starta `buzzActive` flag. Ako endstopat bounce-va (mehanichen switch), poluchavame mnogo EXTI events — vseki palva buzzer i setva evtFlags. `ProcessEvents()` v main loop tryabva da obrabobi vsichki tezi flags, i vseki put printerva "ES_R hit" (ako debug=1). Printf e baven (USB CDC busy-wait) — tova blokira main loop.

### Oshte edin factor: EXTI e FALLING-only za endstops (line 1290)

Endstop switchovete sa konfigurirani kato `GPIO_MODE_IT_FALLING` s `GPIO_PULLUP`. Kogato switchat bounce-va, poluchavame mnogo falling edges — vseki triggered EXTI, no **nqma debounce za endstops v normal mode** (line 1373: `if (!diagMode) { Stepper_Stop(); evtFlags |= EVT_ES_L; }` — nqma timestamp check!).

### Predlojen fix
1. **Dobavi debounce za endstops v normal mode** — syshtata `now - lastTick >= DEBOUNCE_MS` proverka kakto v diagMode
2. **Namali printf-ovete v ProcessEvents** za endstop events — ili gi mahni, ili gi ogranichiь do edin put
3. **Clear esBlocked pri vsqka uspeshna komanda** — ne samo pri jog, ami i pri `move`, `steps` CLI komandi (veche e taka na line 790-802, no ne za step buttons)

---

## BUG 2: Debounce issues — false triggers, multiple fires

### Root cause

Nyakolko problemi:

**A) Endstops nqmat debounce v normal mode**
`main.c:1371-1387` — v normal mode (`!diagMode`), endstop callbacks vikat `Stepper_Stop()` i setvat flag **bez nikakav debounce check**. Samo diagMode ima `now - lastTick >= DEBOUNCE_MS` proverka.

**B) Jog release nqma debounce**
`main.c:1393-1395` — jog button release e `/* release — immediate, no debounce, stop NOW */`. Tova e namereno (za barz stop), no mehanichen switch bounce na release dava nyakolko falling+rising edge-a. Rising edge = release = `Stepper_Stop()`. Falling edge sled tova = nov press = nov jog start.

Znachi: release → bounce → false press → neochakvan jog!

**C) 30ms debounce mozhe da e malko**
`DEBOUNCE_MS = 30` (line 1331). Za mehanichni butoni obiknoveno 50-100ms e po-sigurno. Zavisi ot konkretnite butoni.

**D) Debounce variables sa `volatile` no race condition**
ISR chete i pishe `lastTick_jogL` (line 1398) — main loop ne gi pipa, taka che tuk nqma race. OK.

### Predlojen fix
1. **Dobavi debounce za endstops v normal mode** — syshtoto kato bug 1
2. **Dobavi debounce na jog release** — zapishi `lastTick_jogL = now` pri release, proverqvai `now - lastTick >= DEBOUNCE_MS` predi da priznaesh release. No vnimanie: tova dobavq latency na stop. Alternativa: ignore re-press za 100ms sled release
3. **DEBOUNCE_MS = 30ms (bez promiana)** na 50ms — po-sigurno za mehanichni switches
4. **Dobavi "lockout" period** sled release — 100ms v koito press events se ignoririat

---

## BUG 3: Step button → buzzer gets stuck ON

### Root cause

`HAL_GPIO_EXTI_Callback()` line 1364-1366:
```c
HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_RESET);  // buzzer ON
buzzTick = now;
buzzActive = 1;
```

Buzzer se gasne v main loop (line 1087-1090):
```c
if (buzzActive && (HAL_GetTick() - buzzTick >= 50)) {
    HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_SET);  // buzzer OFF
    buzzActive = 0;
}
```

**Problemat**: Step button e FALLING-only (line 1302). Kogato button bounce-va, poluchavame 3-5 falling edges za ~5ms. Vseki setva `buzzTick = now`. Taka buzzer-off uslovie (`HAL_GetTick() - buzzTick >= 50`) se restartira na vseki bounce event.

No tova bi sazdalo samo malko po-dalgo buzzirane (50ms ot posledniq bounce), ne "stuck".

**ISTINSKATA PRICHINA**: Ako step button EXTI idva DOKATO Stepper_ISR() (TIM2) raboti — EXTI priority e 2, TIM2 e 1. EXTI **NE mozhe da preemptne TIM2**! Taka EXTI se izpalnqva SLED TIM2, no `HAL_GetTick()` NE se updeitva vytre v ISR (SysTick e priority 15 — nai-nisak, blokiran ot vsichki ISR).

**Scenariq za stuck buzzer**:
1. TIM2 ISR se izpalnqva (stepper pulse)
2. Mezhduvremenno step button generira EXTI
3. EXTI se izpalnqva sled TIM2 (queued, ne preempted)
4. EXTI setva buzzActive=1, buzzTick=now
5. Main loop proverqva — buzzer e ON, no ako EXTI i TIM2 sa zaeli mnogo vreme, `HAL_GetTick()` mozhe da e bil "frozen" (SysTick ne se izpalnqva pri chains of ISRs s priority < 15)
6. V nai-loshiq sluchai — `buzzTick` se update-va s "stale" stoinost ot `now`, sled koeto main loop vidi razlika < 50 i ne gasne buzzer

**No po-vazhno**: Ako buzzer-ON (GPIO_PIN_RESET = active low!) se setva v EXTI, i posle MorseUpdate() v main loop SYSHTO manipulira buzzer pin — te se biqt! MorseUpdate() setva pin HIGH (off) v svoia state machine, a EXTI go setva LOW (on). Ako morse e aktiven po vreme na button press — buzzer mozhe da ostane v nedefinirano sustoyanie.

### Predlojen fix
1. **NE palvai buzzer v EXTI ISR** — vmesto tova, samo setvai flag `buzzRequest = 1`
2. **Palvai buzzer v main loop** (ProcessEvents) — `if (buzzRequest) { buzzOn(); buzzRequest = 0; }`
3. **Proveri che MorseUpdate i buzzer ne se conflict-vat** — dobavi `if (!MorseIsBusy())` predi buzzer-on
4. **Alternativa**: izpolzvai TIM za buzzer vmesto GPIO toggle — no tova e overkill

---

## BUG 4: Stacking beeps → jerk v movement

### Root cause (preformuliran sled diskusiq po BUG 1-3)

Dva otdelni problema koito zaedno davat jerk:

**Problema A: Buzzer v ISR (veche resheno v BUG 3)**
EXTI callback palvashe buzzer GPIO direktno — tova veche go mahame (samo flag).

**Problema B: Race condition — Stepper_Stop() ot greshen priority**
V starata shema: jog butoni sa na priority 2, TIM2 e na priority 1.
Stepper_Stop() se vika ot jog release EXTI (prio 2) i manipulira
`stepsRemaining`, `decelIndex`, `stepperState` — syshtite variables koito
Stepper_ISR() (TIM2, prio 1) chete/pishe.

TIM2 MOZHE da preemptne EXTI prio 2 → Stepper_Stop() se prekysva po sredata
→ inconsistent state → jerk ili crash.

### Reshenie (diskusiq 2026-03-18)

**Ne workaround-vame s critical sections — opraqvame prioritetite!**

S novata NVIC shema (vizh tablicata gore):
- Jog butoni (EXTI9_5) → **priority 0** (kato TIM2)
- Na syshtiq priority ne mogat da se preemptvat vzaimno
- Stepper_Stop() ot jog release ISR e **atomaren po dizain**
- Race condition **izchezva bez critical sections**

**Ostavqme critical section v Stepper_Stop() samo za main loop kontekst**
(CLI `stop` komanda) — tam TIM2 prio 0 mozhe da preemptne main loop.

### Obobshtenie na vsichki fixove koito premahvat jerk-a:
1. ~~Buzzer v ISR~~ → flag v ISR, handle v main loop (BUG 3)
2. ~~Race condition~~ → NVIC priorities fix (BUG 4)
3. ~~EXTI flood ot bounce~~ → edge lockout za endstops (BUG 1), debounce za butoni (BUG 2)

---

## ~~BUG 5: Lipsvat jogstop i jog (continuous) CLI komandi~~

**OTPADA.** Jog e operatorska rabota — fizicheski butoni. Nqma smisyl ot CLI
komandi za jog. Ako AI/kompyutyr upravlqva — polzva `move` s konkretna distanciq.

---

## RESHENIA (diskusiq smooker + claude, 2026-03-18)

Sled diskusiq, opredelqme TRI razlichni podkhoda za TRI razlichni tipa vhodove:

### 1. ENDSTOPS (ES_L, ES_R) → EDGE LOCKOUT

Endstop-ite tryabva da reagirat **MIGNOVENO**. Nula latency. Mashina udari
endstop → motor sprq V SYSHTIQ MOMENT.

- **Purvi edge → instant reaction** (Stepper_Stop, set flag)
- **Sled tova → lockout 50-100ms** (ignorirai vsichki sledvashti edges)
- Tova NE E debounce! Debounce izchakva i posle deistva. Edge lockout
  deistva VEDNAGA i posle ignorirava bounce-a.
- Switch bounce sled purviq edge e bez znachenie — motorat veche e sprql.

### 2. JOG BUTONI (JOGL, JOGR) → DEBOUNCE na PRESS, INSTANT RELEASE

Jog butonite imat DVA razlichni rezhima:

**Press (falling edge):**
- Debounce 50ms — izchakaj stabilizaciq, posle trgvai
- Chovek natiska buton — 50ms latency e nezabelezhima
- Tova premahva false press ot bounce

**Release (rising edge):**
- MIGNOVENEN STOP — nula latency, kato endstop
- Motorat tryabva da spira VEDNAGA kogato chovek puskne butona
- Bounce na release ne e problem — povtoren stop na sprql motor = nishto
- Sled release → lockout 100ms za press (za da ne hvane false re-press ot bounce)

### 3. STEP BUTONI (STEPL, STEPR) → DEBOUNCE samo na PRESS

- **Press**: debounce 30ms → move fixed distance
- **Release**: ignorira se — step e fire-and-forget komanda
- Ne hvashchame release za step butoni vobshte

### Obshtobstvo

| Vhod | Press | Release |
|------|-------|---------|
| Endstop | edge lockout (instant + lockout) | n/a (falling only) |
| Jog buton | debounce 30ms | instant stop + lockout 100ms |
| Step buton | debounce 30ms | ignorirai |

---

## ACTION PLAN — Red na fixovete

### Faza 1: Kritichni bugove (bez tqh motorat mozhe da se povredi)

**1.1 — Fix race condition: NVIC priorities + critical section [BUG 4]**

A) Nova funkcia `FixNVIC_Priorities()` v main.c — vikva se ot main() sled HAL_Init:
   - Jog butoni (EXTI9_5) → priority 1 (kato TIM2 i endstops)
   - Step butoni ostava priority 2 (ne vikat Stepper_Stop)
   - Hubav komentar nad funkciata ZASHTO i KAKVO reshava
   - Kogato doidem do finalna chistva (cubemx regen) — mahame funkciata
   - File: `main.c`

B) Critical section v `Stepper_Stop()` — `__disable_irq()`/`__enable_irq()`
   - Nuzhno zashtoto CLI `stop` komanda vika Stepper_Stop() ot main loop
   - Ot main loop TIM2 (prio 1) mozhe da preemptne → race
   - Ot ISR prio 1 ne e nuzhno (atomic), no ne prechqi — extra safety
   - File: `stepper.c:357`

**1.2 — Endstop EDGE LOCKOUT [BUG 1 + BUG 2]**
- PURVIQ edge → instant Stepper_Stop() + set flag + record timestamp
- Sledvashti edges v ramkite na 50-100ms → ignorirai
- NE E debounce — reagirame VEDNAGA, posle lockout
- File: `main.c:1371-1387`

### Faza 2: Buzzer / interrupt cleanup

**2.1 — Premesti buzzer-ON ot ISR v main loop [BUG 3 + BUG 4]**
- V EXTI callback: `buzzRequest = 1;` (vmesto GPIO write)
- V main loop (predi ProcessEvents): `if (buzzRequest && !MorseIsBusy()) { buzzOn(); buzzRequest = 0; }`
- Tova premahva GPIO conflict s MorseUpdate i premahva izlishna rabota ot ISR
- File: `main.c:1364-1366` i `main.c:1087-1090`

**2.2 — Jog release: INSTANT STOP, no NE v ISR [BUG 4]**
- V EXTI callback za jog release: samo `evtFlags |= EVT_JOGX_UP;`
- PREMAHNI direktniq Stepper_Stop() ot ISR (line 1394/1408)
- V ProcessEvents() EVT_JOGX_UP handler → Stepper_Stop() (veche go ima na line 1585/1604)
- Release e mignovenen (main loop cycle ~1ms) no bez race condition
- File: `main.c:1394, 1408`

### Faza 3: Debounce/lockout po tipa na vhoda

**3.1 — Jog butoni: debounce 30ms na PRESS, instant RELEASE [BUG 2]**
- Press: izchakaj 50ms stabilizaciq → posle deistvai
- Release: vednaga (prez flag → main loop → Stepper_Stop)
- Sled release: lockout 100ms za press (protiv false re-press ot bounce)
- File: `main.c:1390-1416`

**3.2 — Step butoni: debounce 30ms samo na PRESS [BUG 2]**
- Press: debounce 30ms → move fixed distance
- Release: ne ni interesava — step e fire-and-forget
- File: `main.c` step button handler

**3.3 — DEBOUNCE_MS ostava 30ms** (provereno s osciloskop, dostatychno za tezi butoni)

### ~~Faza 4: Novi komandi~~ OTPADA
Jog e operatorska rabota, ne CLI.

### Zavisimosti

```
1.1 (critical section) → nqma zavisimosti, fix PURVO
1.2 (endstop edge lockout) → nqma zavisimosti
2.1 (buzzer to main loop) → nqma zavisimosti
2.2 (jog release ot ISR) → zavisi ot 1.1
3.x (debounce/lockout) → sled 1.2 i 2.2
4.1 → OTPADA (jog e operatorska rabota)
```

---

## NOTES

- `_write()` (printf prez USB CDC) e blocking s 100ms timeout (main.c:527-530). Vseki printf v ISR path zabavq vsichko. Nito edin printf ne tryabva da e v ISR — vsichko prez flags v main loop.
- Buzzer e active-low (GPIO_PIN_RESET = ON, GPIO_PIN_SET = OFF). Lесно se burka.
- `CDC_RxRead()` (main.c:718) izpolzvashe `APP_RX_DATA_SIZE` (2048) za modulo vmesto `RX_BUF_SIZE` (512). Typo — **FIXNATO** (2026-03-18). rxTail mozhe da stigne 513+ dokato rxHead nikoga ne nadvishavashe 511 → phantom data → corruption pri intenziven trafik.
