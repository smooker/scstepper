# Ръководство за билд и debug

## Предварителни изисквания

### ARM тулчейн
Проектът изисква `arm-none-eabi-gcc` (тестван с GCC 14.x).

На Gentoo тулчейнът се намира на:
```
/usr/x86_64-pc-linux-gnu/arm-none-eabi/gcc-bin/14/
```

Уверете се, че е в PATH:
```bash
export PATH="/usr/x86_64-pc-linux-gnu/arm-none-eabi/gcc-bin/14:$PATH"
```

Проверка:
```bash
arm-none-eabi-gcc --version
```

### Настройка на chroot
При билд вътре в chroot (напр. `/chroot/claude`), копирайте тулчейна от хоста:

```bash
# бинарни файлове
rsync -av /usr/bin/arm-none-eabi-* /chroot/claude/usr/bin/
rsync -av /usr/x86_64-pc-linux-gnu/ /chroot/claude/usr/x86_64-pc-linux-gnu/
rsync -av /usr/lib/gcc/arm-none-eabi/ /chroot/claude/usr/lib/gcc/arm-none-eabi/

# compiler плъгини (cc1 и др.)
rsync -av /usr/libexec/gcc/arm-none-eabi/ /chroot/claude/usr/libexec/gcc/arm-none-eabi/

# споделени библиотеки (binutils)
rsync -av /usr/lib64/binutils/arm-none-eabi/ /chroot/claude/usr/lib64/binutils/arm-none-eabi/
```

---

## Билд

```bash
# пълен чист билд
make clean && make

# инкрементален билд
make
```

### Изходни файлове
```
build/stepper_sc.elf   — ELF с debug символи (за GDB)
build/stepper_sc.hex   — Intel HEX (за флашване)
build/stepper_sc.bin   — суров бинарен файл
build/stepper_sc.map   — linker map
```

### Очакван размер (билд с lookup таблица, -O0)
```
text: ~79KB   data: ~900B   bss: ~14KB
```

---

## Флашване и Debug

### Хардуер
- **Програматор**: Black Magic Probe (BMP) свързан чрез `/dev/ttyBmpGdb`
- **Интерфейс**: SWD (PA13=SWDIO, PA14=SWCLK)

### Настройка на GDB
`.gdbinit` в корена на проекта се зарежда автоматично. Съдържа:
- Пълен **gdb-dashboard** (визуален TUI с регистри, сорс, disassembly)
- **SVD** преглед на регистри чрез PyCortexMDebug (`STM32F411.svd`)
- Custom команди `ag` и `ld`

> **Забележка**: PyCortexMDebug е git submodule в `PyCortexMDebug/` и се зарежда чрез относителен път — не е нужна ръчна настройка.

Вашият `~/.gdbinit` трябва да разрешава локални init файлове:
```
set auto-load local-gdbinit on
add-auto-load-safe-path .
```

### Стартиране на GDB
```bash
./go_gdb.sh
```
Изпълнява:
```bash
arm-none-eabi-gdb -x ./script2.gdb ./build/stepper_sc.elf
```

### Custom GDB команди

**`ag`** — свързване с Black Magic Probe и прикачване към target:
```
target extended-remote /dev/ttyBmpGdb
monitor swdp_scan
attach 1
monitor traceswo
```

**`ld`** — флашване на фърмуера и проверка:
```
file ./build/stepper_sc.elf
load ./build/stepper_sc.hex
set remote exec-file ./build/stepper_sc.elf
compare-sections
```

### Типична сесия
```
(gdb) ag       ← свързване с BMP, сканиране на SWD, прикачване към STM32
(gdb) ld       ← флашване на stepper_sc.hex, проверка на секциите
(gdb) continue ← стартиране на фърмуера
```

За задаване на breakpoint и спиране при main:
```
(gdb) ag
(gdb) ld
(gdb) hbreak main
(gdb) run
```

### Инспекция на SVD регистри
След `ag` периферните регистри на STM32F411 са достъпни чрез:
```
(gdb) svd TIM2
(gdb) svd GPIOB
(gdb) svd RCC
```

---

## Black Pill v3.1 Pinout (WeAct STM32F411CEU6)

USB-C конекторът е вляво. DIP-стил номерация: пинове 1–20 на предния ред (към вас), пинове 21–40 на задния ред отдясно-наляво (обвива като DIP IC).

```
    USB край                             не-USB край
      ↓                                      ↓
  ┌───┴──────────────────────────────────────┴───┐
  │  [USB-C]   STM32F411CEU6   WeAct v3.1        │
  └──┬───────────────────────────────────────┬───┘
     │                                       │
     │  ПРЕДЕН РЕД  (пинове 1–20, ляво→дясно)│
     ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●
     1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20
    B12 B13 B14 B15 A8 A9 A10 A11 A12 A15 B3 B4 B5 B6 B7 B8 B9 5V GND 3V3

     ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●
    40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21
    5V GND 3V3 B10 B2 B1 B0 A7 A6 A5 A4 A3 A2 A1 A0 RST C15 C14 C13 VBAT
     │  ЗАДЕН РЕД  (пинове 21–40, дясно→ляво = пин 21 при не-USB край)  │
```

**Ключови сигнали за проекта:**

| BP пин | Сигнал | GPIO | Роля |
|--------|--------|------|------|
| 3  | PB14   | PB14 | DIR изход → NPN буфер → CWD556 DIR+ |
| 4  | PB15   | PB15 | BUZZ изход |
| 18 | 5V     | —    | Захранване (от 78M05) |
| 19 | GND    | —    | Маса |
| 20 | 3V3    | —    | 3.3V референция |
| 29 | PA3    | PA3  | ES_L — краен изключвател ляво, активен LOW |
| 30 | PA4    | PA4  | ES_R — краен изключвател дясно, активен LOW |
| 32 | PA6    | PA6  | JOGL — jog ляво бутон, активен LOW |
| 33 | PA7    | PA7  | JOGR — jog дясно бутон, активен LOW |
| 34 | PB0    | PB0  | STEPL — step ляво бутон, активен LOW |
| 35 | PB1    | PB1  | STEPR — step дясно бутон, активен LOW |
| 37 | PB10   | PB10 | PULSE изход (TIM2 CH3 PWM → CWD556 PUL+) |
| 38 | 3V3    | —    | 3.3V референция |
| 39 | GND    | —    | Маса |
| 40 | 5V     | —    | Захранване (от 78M05) |

---

## Справочник на пиновете

| Сигнал     | Пин  | Посока | Описание                              |
|------------|------|--------|---------------------------------------|
| PULSE      | PB10 | Изход  | STEP импулс (TIM2 CH3 PWM)            |
| DIR        | PB14 | Изход  | Посока                                |
| BUZZ       | PB15 | Изход  | Зумер                                 |
| LED_USER   | PC13 | Изход  | Потребителски LED                     |
| ES_L       | PA3  | Вход   | Краен изключвател ляво — активен LOW, двата фронта, вътрешен pull-up |
| ES_R       | PA4  | Вход   | Краен изключвател дясно — активен LOW, двата фронта, вътрешен pull-up |
| BUTT_JOGL  | PA6  | Вход   | Jog ляво — активен LOW, двата фронта, вътрешен pull-up |
| BUTT_JOGR  | PA7  | Вход   | Jog дясно — активен LOW, двата фронта, вътрешен pull-up |
| BUTT_STEPL | PB0  | Вход   | Step ляво — активен LOW, двата фронта, вътрешен pull-up |
| BUTT_STEPR | PB1  | Вход   | Step дясно — активен LOW, двата фронта, вътрешен pull-up |
| USB DM/DP  | PA11/PA12 | USB | USB CDC (виртуален COM)          |
| SWDIO      | PA13 | Debug  | SWD данни                             |
| SWCLK      | PA14 | Debug  | SWD часовник                          |

---

## USB Сериен терминал

Фърмуерът излага USB CDC виртуален COM порт. Свържете се с произволен терминал при произволна скорост.

Препоръчително:
```bash
./minicom.sh
# или
./listen.sh
```

### CLI команди
```
move <mm>          движение с mm (положително = дясно, отрицателно = ляво)
movel <mm>         движение ляво с mm
mover <mm>         движение дясно с mm
steps <n>          движение с N стъпки
set mmpsmax  <f>   максимална скорост mm/s      (по подразбиране: 50.0)
set mmpsmin  <f>   минимална скорост mm/s       (по подразбиране: 1.0)
set dvdtacc  <f>   ускорение mm/s²              (по подразбиране: 100.0)
set dvdtdecc <f>   забавяне mm/s²               (по подразбиране: 80.0)
set jogmm    <f>   jog разстояние mm            (по подразбиране: 1.0)
set stepmm   <f>   step разстояние mm           (по подразбиране: 1.0)
set spmm     <n>   стъпки на mm                 (по подразбиране: 80)
params             извежда текущите параметри
save               запис на параметрите в EEPROM
dump               dump на EEPROM промеливи и семафорно състояние
stop               забавяне до спиране
cls                изчистване на екрана
uptime             показва uptime в ms
reset              системен рестарт
help               показва списък с команди
```
