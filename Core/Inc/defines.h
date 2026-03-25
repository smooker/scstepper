#ifndef DEFINES_H
#define DEFINES_H

#define BKPT asm("bkpt 255")

// define bitwises for semaphore
#define JOGL        0
#define JOGR        1
#define JOGSTEPL    2
#define JOGSTEPR    3
#define STEPL       4
#define STEPR       5
#define EL          6
#define ER          7
#define EE          8
#define JOGL_DB     9
#define JOGR_DB     10
#define JOGSTEPL_DB 11
#define JOGSTEPR_DB 12
#define STEPL_DB    13
#define STEPR_DB    14
#define EL_DB       15
#define ER_DB       16
#define EE_DB       17
#define IN_HOMING   18

//PIN JOGS
#define PIN_JOGL_RESET  (HAL_GPIO_ReadPin(BUTT_JOGL_GPIO_Port, BUTT_JOGL_Pin) == GPIO_PIN_RESET)
#define PIN_JOGL_SET    (HAL_GPIO_ReadPin(BUTT_JOGL_GPIO_Port, BUTT_JOGL_Pin) == GPIO_PIN_SET)
#define PIN_JOGR_RESET  (HAL_GPIO_ReadPin(BUTT_JOGR_GPIO_Port, BUTT_JOGR_Pin) == GPIO_PIN_RESET)
#define PIN_JOGR_SET    (HAL_GPIO_ReadPin(BUTT_JOGR_GPIO_Port, BUTT_JOGR_Pin) == GPIO_PIN_SET)

//PIN STEPS
#define PIN_STEPL_RESET (HAL_GPIO_ReadPin(BUTT_STEPL_GPIO_Port, BUTT_STEPL_Pin) == GPIO_PIN_RESET)
#define PIN_STEPL_SET   (HAL_GPIO_ReadPin(BUTT_STEPL_GPIO_Port, BUTT_STEPL_Pin) == GPIO_PIN_SET)
#define PIN_STEPR_RESET (HAL_GPIO_ReadPin(BUTT_STEPR_GPIO_Port, BUTT_STEPR_Pin) == GPIO_PIN_RESET)
#define PIN_STEPR_SET   (HAL_GPIO_ReadPin(BUTT_STEPR_GPIO_Port, BUTT_STEPR_Pin) == GPIO_PIN_SET)

//PIN E
#define PIN_EL_RESET    (HAL_GPIO_ReadPin(ES_L_GPIO_Port, ES_L_Pin) == GPIO_PIN_RESET)
#define PIN_EL_SET      (HAL_GPIO_ReadPin(ES_L_GPIO_Port, ES_L_Pin) == GPIO_PIN_SET)
#define PIN_ER_RESET    (HAL_GPIO_ReadPin(ES_R_GPIO_Port, ES_R_Pin) == GPIO_PIN_RESET)
#define PIN_ER_SET      (HAL_GPIO_ReadPin(ES_R_GPIO_Port, ES_R_Pin) == GPIO_PIN_SET)

/* Endstop active state (active LOW) — single source of truth */
#define ES_L_ACTIVE()   PIN_EL_RESET   /* ES_L triggered = block CCW */
#define ES_R_ACTIVE()   PIN_ER_RESET   /* ES_R triggered = block CW  */

//SEMAPHORES
#define SEM_EL          ((semaphore & (1 << EL)) > 0)
#define SEM_ER          ((semaphore & (1 << ER)) > 0)
#define SEM_JOGL        ((semaphore & (1 << JOGL)) > 0)
#define SEM_JOGR        ((semaphore & (1 << JOGR)) > 0)
#define SEM_JOGSTEPL    ((semaphore & (1 << JOGSTEPL)) > 0)
#define SEM_JOGSTEPR    ((semaphore & (1 << JOGSTEPR)) > 0)
#define SEM_STEPL       ((semaphore & (1 << STEPL)) > 0)
#define SEM_STEPR       ((semaphore & (1 << STEPR)) > 0)
#define SEM_IN_HOMING   ((semaphore & (1 << IN_HOMING)) > 0)

//DEBOUNCES
#define DB_JOGL         ((semaphore & (1 << JOGL_DB)) > 0)
#define DB_JOGR         ((semaphore & (1 << JOGR_DB)) > 0)
#define DB_STEPL        ((semaphore & (1 << STEPL_DB)) > 0)
#define DB_STEPR        ((semaphore & (1 << STEPR_DB)) > 0)
#define DB_EL           ((semaphore & (1 << EL_DB)) > 0)
#define DB_ER           ((semaphore & (1 << ER_DB)) > 0)
#define DB_EE           ((semaphore & (1 << EE_DB)) > 0)

//
#define pulsedur        50                  //puse duration in us
#define delayafterdir   50                  //delay after set direction pin and before first pulse to come

#define PULSE_TICKS     5000UL   /* ~50us at 96MHz, +100 to compensate ISR latency */

/* tim9Ms-based delay — requires tim9Ms (volatile uint32_t from TIM9 ISR) to be in scope */
#define TIM9_DELAY(ms) { uint32_t _t = tim9Ms; while (tim9Ms - _t < (ms)); }


#endif // DEFINES_H
