#ifndef STEPPER_H
#define STEPPER_H

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_tim.h"
#include "main.h"
#include "defines.h"
#include "eeprom_emul_uint32_t.h"
#include <stdint.h>

/* ---- EEPROM virtual addresses (continuing from params in main.c) --- */
#define EE_ADDR_MMPSMAX     1   /* matches writeParams() index order    */
#define EE_ADDR_MMPSMIN     2
#define EE_ADDR_DVDTACC     3
#define EE_ADDR_DVDTDECC    4
#define EE_ADDR_JOGMM       5
#define EE_ADDR_STEPMM      6
#define EE_ADDR_SPMM        7
#define EE_ADDR_DIRINV      8
#define EE_ADDR_HOMESPD     9
#define EE_ADDR_HOMEOFF     10
#define EE_ADDR_DEBUG       11

#define DEFAULT_HOMESPD     1.0f    /* mm/s     */
#define DEFAULT_HOMEOFF     400UL   /* steps    */
#define DEFAULT_DEBUG       0UL     /* bit0: verbose button msgs */

/* ---- Timer clock --------------------------------------------------- */
#define STEPPER_TIM_CLOCK   96000000UL          /* 96MHz                */
#define DIRSETUP_TICKS      ((STEPPER_TIM_CLOCK / 1000000UL) * delayafterdir)

#define ABS(x)  ((x) < 0 ? -(x) : (x))

/* ---- Default params ------------------------------------------------ */
#define DEFAULT_MMPSMAX     50.0f   /* mm/s     */
#define DEFAULT_MMPSMIN     1.0f    /* mm/s     */
#define DEFAULT_DVDTACC     100.0f  /* mm/s²    */
#define DEFAULT_DVDTDECC    80.0f   /* mm/s²    */
#define DEFAULT_JOGMM       1.0f    /* mm       */
#define DEFAULT_STEPMM      1.0f    /* mm       */
#define DEFAULT_SPMM        80UL    /* steps/mm */

#define MAX_RAMP_STEPS      512

/* ---- State machine ------------------------------------------------- */
typedef enum {
    STEPPER_IDLE,
    STEPPER_ACCEL,
    STEPPER_CONST,
    STEPPER_DECEL,
} StepperState;

/* ---- Public API ---------------------------------------------------- */
void     Stepper_Init(TIM_HandleTypeDef *htim);
void     Stepper_Move(float mm);
void     Stepper_MoveSteps(int32_t steps);
void     Stepper_Jog(float mm);
void     Stepper_RunContinuous(int8_t dir);
void     Stepper_Stop(void);
uint8_t  Stepper_IsBusy(void);
void     Stepper_ISR(void);
void     Stepper_DumpParams(void);
void     Stepper_SaveParams(void);
void     Stepper_LoadParams(void);
void     Stepper_SetParam(const char *name, float value);

#endif /* STEPPER_H */
