# SCstepper Pinout — STM32F411CE Black Pill

## GPIO Pin Map

| Pin | Port | Funkciq | Aktivno nivo |
|-----|------|---------|--------------|
| PC13 | GPIOC | LED_USER | Active LOW |
| PA3 | GPIOA | ES_L (endstop left) | Active LOW |
| PA4 | GPIOA | ES_R (endstop right) | Active LOW |
| PA6 | GPIOA | BUTT_JOGL (jog left) | Active LOW |
| PA7 | GPIOA | BUTT_JOGR (jog right) | Active LOW |
| PB0 | GPIOB | BUTT_STEPL (step left) | Active LOW |
| PB1 | GPIOB | BUTT_STEPR (step right) | Active LOW |
| PB10 | GPIOB | PULSE (stepper pulse) | Active LOW |
| PB14 | GPIOB | DIR (stepper direction) | Active LOW |
| PB15 | GPIOB | BUZZ (buzzer) | Active LOW |

## Definicii

- Pin definitions: `Core/Inc/main.h` (CubeMX generated)
- Active level macros: `Core/Inc/defines.h`
- Endstop active state: `ES_L_ACTIVE()` / `ES_R_ACTIVE()` = `GPIO_PIN_RESET`

## Black Pill Board

- MCU: STM32F411CEU6
- Crystal: 25MHz HSE
- USB: USB-C (CDC Virtual COM)
- Onboard LED: PC13 (active LOW)
- Docs: `docs/other/black-pill-pinout.png`
