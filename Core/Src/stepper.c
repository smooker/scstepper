#include "stepper.h"
#include <stdio.h>
#include <string.h>

/* replace fabsf */
#define FABS(x)     ((x) < 0.0f ? -(x) : (x))

/* replace roundf */
#define ROUND(x)    ((int32_t)((x) >= 0.0f ? (x) + 0.5f : (x) - 0.5f))

static float sqrtf_hw(float x)
{
    float result;
    __asm volatile ("vsqrt.f32 %0, %1" : "=w"(result) : "w"(x));
    return result;
}

/* ---- extern motorParams from main.c ------------------------------------ */
extern params_t motorParams;

/* ---- Timer handle ------------------------------------------------- */
static TIM_HandleTypeDef *stepTim;

/* ---- State -------------------------------------------------------- */
static volatile StepperState stepperState = STEPPER_IDLE;
static volatile int32_t  stepsRemaining   = 0;
static volatile int32_t  stepCount        = 0;
static volatile int32_t  decelSteps       = 0;
static volatile uint32_t currentPeriod    = 0;
static volatile uint32_t minPeriod        = 0;   /* ticks at mmpsmax  */
static volatile uint32_t maxPeriod        = 0;   /* ticks at mmpsmin  */

static volatile int32_t decelCount = 0;
static volatile int32_t accelIndex = 0;
static volatile int32_t decelIndex = 0;

/* ---- Absolute position tracking ----------------------------------- */
volatile int32_t  posSteps = 0;     /* current position in steps */
volatile uint8_t  posHomed = 0;     /* 1 = homed, 0 = unknown position */
static volatile int8_t moveDir = 1; /* +1 or -1, set before each move */

/* ---- Ramp tables -------------------------------------------------- */
static uint32_t accelTable[MAX_RAMP_STEPS];
static uint32_t decelTable[MAX_RAMP_STEPS];
static int32_t  accelSize = 0;
static int32_t  decelSize = 0;


/* ---- Helpers ------------------------------------------------------ */

static void BuildRampTables(void)
{
    float accel_sps2 = motorParams.dvdtacc.f  * (float)motorParams.spmm.u;
    float decel_sps2 = motorParams.dvdtdecc.f * (float)motorParams.spmm.u;

    /* accel table: slow to fast */
    float period = (float)maxPeriod;
    accelSize = 0;
    while (period > (float)minPeriod && accelSize < MAX_RAMP_STEPS)
    {
        accelTable[accelSize++] = (uint32_t)period;
        float v = (float)STEPPER_TIM_CLOCK / period;
        float v_new = sqrtf_hw(v * v + 2.0f * accel_sps2);
        period = (float)STEPPER_TIM_CLOCK / v_new;
    }

    /* decel table: slow to fast (used in reverse) */
    period = (float)maxPeriod;
    decelSize = 0;
    while (period > (float)minPeriod && decelSize < MAX_RAMP_STEPS)
    {
        decelTable[decelSize++] = (uint32_t)period;
        float v = (float)STEPPER_TIM_CLOCK / period;
        float v_new = sqrtf_hw(v * v + 2.0f * decel_sps2);
        period = (float)STEPPER_TIM_CLOCK / v_new;
    }
}

static uint32_t MmpsToTicks(float mmps)
{
    if (mmps <= 0.0f) return 0;
    float sps = mmps * (float)motorParams.spmm.u;   /* steps per second   */
    return (uint32_t)((float)STEPPER_TIM_CLOCK / sps);
}

static int32_t MmToSteps(float mm)
{
    return ROUND(mm * (float)motorParams.spmm.u);
}

/* ---- EEPROM ------------------------------------------------------- */

void Stepper_LoadParams(void)
{
    uint32_t val;

    /* floats stored as raw uint32_t via union — read directly         */
    if (EEPROM_Read(EE_ADDR_MMPSMAX,  &val) == EEPROM_OK) motorParams.mmpsmax.u  = val;
    else motorParams.mmpsmax.f  = DEFAULT_MMPSMAX;

    if (EEPROM_Read(EE_ADDR_MMPSMIN,  &val) == EEPROM_OK) motorParams.mmpsmin.u  = val;
    else motorParams.mmpsmin.f  = DEFAULT_MMPSMIN;

    if (EEPROM_Read(EE_ADDR_DVDTACC,  &val) == EEPROM_OK) motorParams.dvdtacc.u  = val;
    else motorParams.dvdtacc.f  = DEFAULT_DVDTACC;

    if (EEPROM_Read(EE_ADDR_DVDTDECC, &val) == EEPROM_OK) motorParams.dvdtdecc.u = val;
    else motorParams.dvdtdecc.f = DEFAULT_DVDTDECC;

    if (EEPROM_Read(EE_ADDR_JOGMM,    &val) == EEPROM_OK) motorParams.jogmm.u    = val;
    else motorParams.jogmm.f    = DEFAULT_JOGMM;

    if (EEPROM_Read(EE_ADDR_STEPMM,   &val) == EEPROM_OK) motorParams.stepmm.u   = val;
    else motorParams.stepmm.f   = DEFAULT_STEPMM;

    if (EEPROM_Read(EE_ADDR_SPMM,     &val) == EEPROM_OK) motorParams.spmm.u     = val;
    else motorParams.spmm.u     = DEFAULT_SPMM;

    if (EEPROM_Read(EE_ADDR_DIRINV,   &val) == EEPROM_OK) motorParams.dirinv.u   = val;
    else motorParams.dirinv.u   = 0;

    if (EEPROM_Read(EE_ADDR_HOMESPD,  &val) == EEPROM_OK) motorParams.homespd.u  = val;
    else motorParams.homespd.f  = DEFAULT_HOMESPD;

    if (EEPROM_Read(EE_ADDR_HOMEOFF,  &val) == EEPROM_OK) motorParams.homeoff.u  = val;
    else motorParams.homeoff.u  = DEFAULT_HOMEOFF;

    if (EEPROM_Read(EE_ADDR_DEBUG,   &val) == EEPROM_OK) motorParams.debug.u   = val;
    else motorParams.debug.u   = DEFAULT_DEBUG;
}

void Stepper_SaveParams(void)
{
    EEPROM_Write(EE_ADDR_MMPSMAX,  motorParams.mmpsmax.u);
    EEPROM_Write(EE_ADDR_MMPSMIN,  motorParams.mmpsmin.u);
    EEPROM_Write(EE_ADDR_DVDTACC,  motorParams.dvdtacc.u);
    EEPROM_Write(EE_ADDR_DVDTDECC, motorParams.dvdtdecc.u);
    EEPROM_Write(EE_ADDR_JOGMM,    motorParams.jogmm.u);
    EEPROM_Write(EE_ADDR_STEPMM,   motorParams.stepmm.u);
    EEPROM_Write(EE_ADDR_SPMM,     motorParams.spmm.u);
    EEPROM_Write(EE_ADDR_DIRINV,   motorParams.dirinv.u);
    EEPROM_Write(EE_ADDR_HOMESPD,  motorParams.homespd.u);
    EEPROM_Write(EE_ADDR_HOMEOFF,  motorParams.homeoff.u);
    EEPROM_Write(EE_ADDR_DEBUG,    motorParams.debug.u);
    printf("params saved\r\n");
}

void Stepper_DumpParams(void)
{
    printf("\r\n-----------------------------------------------\r\n");
    printf("  mmpsmax........: %7.3f mm/s\r\n",   motorParams.mmpsmax.f);
    printf("  mmpsmin........: %7.3f mm/s\r\n",   motorParams.mmpsmin.f);
    printf("  dvdtacc........: %7.3f mm/s2\r\n",  motorParams.dvdtacc.f);
    printf("  dvdtdecc.......: %7.3f mm/s2\r\n",  motorParams.dvdtdecc.f);
    printf("  jogmm..........: %7.3f mm\r\n",     motorParams.jogmm.f);
    printf("  stepmm.........: %7.3f mm\r\n",     motorParams.stepmm.f);
    printf("  spmm...........: %7lu steps/mm\r\n", motorParams.spmm.u);
    printf("  dirinv.........: %7lu %s\r\n", motorParams.dirinv.u,
              motorParams.dirinv.u ? "(inverted)" : "(normal)");
    printf("  homespd........: %7.3f mm/s\r\n", motorParams.homespd.f);
    printf("  homeoff........: %7lu steps\r\n", motorParams.homeoff.u);
    printf("  debug..........: 0x%04lX\r\n", motorParams.debug.u);
    printf("-----------------------------------------------\r\n");
    printf("  pulse_ticks....: %lu\r\n", (uint32_t)PULSE_TICKS);
    printf("  min_period.....: %lu ticks (%.1f mm/s)\r\n",
              MmpsToTicks(motorParams.mmpsmax.f), motorParams.mmpsmax.f);
    printf("  max_period.....: %lu ticks (%.1f mm/s)\r\n",
              MmpsToTicks(motorParams.mmpsmin.f), motorParams.mmpsmin.f);
    printf("-----------------------------------------------\r\n");
}

void Stepper_SetParam(const char *name, float value)
{
    if      (strcmp(name, "mmpsmax")  == 0) motorParams.mmpsmax.f  = value;
    else if (strcmp(name, "mmpsmin")  == 0) motorParams.mmpsmin.f  = value;
    else if (strcmp(name, "dvdtacc")  == 0) motorParams.dvdtacc.f  = value;
    else if (strcmp(name, "dvdtdecc") == 0) motorParams.dvdtdecc.f = value;
    else if (strcmp(name, "jogmm")    == 0) motorParams.jogmm.f    = value;
    else if (strcmp(name, "stepmm")   == 0) motorParams.stepmm.f   = value;
    else if (strcmp(name, "spmm")     == 0) motorParams.spmm.u     = (uint32_t)value;
    else if (strcmp(name, "dirinv")   == 0) motorParams.dirinv.u   = (uint32_t)value;
    else if (strcmp(name, "homespd")  == 0) motorParams.homespd.f  = value;
    else if (strcmp(name, "homeoff")  == 0) motorParams.homeoff.u  = (uint32_t)value;
    else if (strcmp(name, "debug")   == 0) motorParams.debug.u    = (uint32_t)value;
    else { printf("unknown param: %s\r\n", name); return; }
    printf("%s = %.3f\r\n", name, value);
}

/* ---- Init --------------------------------------------------------- */

void Stepper_Init(TIM_HandleTypeDef *htim)
{
    stepTim = htim;

    /* ensure DIR and PULSE pins are low at start */
    HAL_GPIO_WritePin(PULSE_GPIO_Port, PULSE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DIR_GPIO_Port,   DIR_Pin,   GPIO_PIN_RESET);

    /* fixed pulse width */
    __HAL_TIM_SET_COMPARE(stepTim, TIM_CHANNEL_3, PULSE_TICKS);

    printf("  stepper init ok\r\n");
}

/* ---- Move --------------------------------------------------------- */

static void StartMove(int32_t steps)
{
    if (stepperState != STEPPER_IDLE)
    {
        printf("stepper busy\r\n");
        return;
    }
    if (steps == 0) return;

    /* set direction (apply dirinv from EEPROM) */
    {
        uint8_t hw = (steps > 0) ^ (motorParams.dirinv.u != 0);
        HAL_GPIO_WritePin(DIR_GPIO_Port, DIR_Pin,
                          hw ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    /* wait for DIR to settle */
    uint32_t t = HAL_GetTick();
    while (HAL_GetTick() == t);

    moveDir        = (steps > 0) ? 1 : -1;
    stepsRemaining = ABS(steps);
    stepCount      = 0;
    decelCount     = 0;
    minPeriod      = MmpsToTicks(motorParams.mmpsmax.f);
    maxPeriod      = MmpsToTicks(motorParams.mmpsmin.f);
    currentPeriod  = maxPeriod;

    /* in StartMove — replace decelSteps calculation: */
    BuildRampTables();
    decelSteps = decelSize;
    if (decelSteps > stepsRemaining / 2)
        decelSteps = stepsRemaining / 2;
    accelIndex = 0;
    decelIndex = decelSize - 1;  /* start from fast end */


    /* calculate peak speed achievable in available steps */
    float accel_sps2 = motorParams.dvdtacc.f  * (float)motorParams.spmm.u;
    float decel_sps2 = motorParams.dvdtdecc.f * (float)motorParams.spmm.u;
    float v_max_sps  = (float)STEPPER_TIM_CLOCK / (float)minPeriod;
    float v_min_sps  = (float)STEPPER_TIM_CLOCK / (float)maxPeriod;

    /* peak speed for triangle profile */
    float v_peak_sps = sqrtf_hw(2.0f * accel_sps2 * decel_sps2 * (float)stepsRemaining
                             / (accel_sps2 + decel_sps2));

    /* clamp to mmpsmax */
    if (v_peak_sps > v_max_sps) v_peak_sps = v_max_sps;

    /* recalculate minPeriod and decelSteps from actual peak speed */
    if (v_peak_sps > v_min_sps)
    {
        minPeriod  = (uint32_t)((float)STEPPER_TIM_CLOCK / v_peak_sps);
        decelSteps = (int32_t)((v_peak_sps * v_peak_sps - v_min_sps * v_min_sps)
                                / (2.0f * decel_sps2));
    }

    stepperState = STEPPER_ACCEL;

    /* 1. reconfigure channel to PWM Mode 1 */
    TIM_OC_InitTypeDef sConfig = {0};
    sConfig.OCMode     = TIM_OCMODE_PWM1;
    sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfig.OCFastMode = TIM_OCFAST_DISABLE;
    sConfig.Pulse      = PULSE_TICKS;
    HAL_TIM_PWM_ConfigChannel(stepTim, &sConfig, TIM_CHANNEL_3);

    /* 2. set period and counter */
    __HAL_TIM_SET_AUTORELOAD(stepTim, currentPeriod - 1);
    __HAL_TIM_SET_COMPARE(stepTim, TIM_CHANNEL_3, PULSE_TICKS);
    __HAL_TIM_SET_COUNTER(stepTim, 0);

    /* 3. start */
    HAL_TIM_PWM_Start_IT(stepTim, TIM_CHANNEL_3);

    printf("move %ld steps\r\n", (long)ABS(steps));
}

void Stepper_Move(float mm)
{
    int32_t steps = MmToSteps(mm);
    printf("move %.3f mm -> %ld steps\r\n", mm, steps);
    StartMove(steps);
}

void Stepper_MoveSteps(int32_t steps)
{
    printf("move %ld steps\r\n", steps);
    StartMove(steps);
}

void Stepper_Jog(float mm)
{
    /* jog uses jogmm as unit — positive = right, negative = left      */
    float dist = (mm >= 0.0f ? 1.0f : -1.0f) * motorParams.jogmm.f;
    int32_t steps = MmToSteps(dist);
    printf("jog %.3f mm -> %ld steps\r\n", dist, steps);
    StartMove(steps);
}

void Stepper_RunContinuous(int8_t dir)
{
    if (stepperState != STEPPER_IDLE)
    {
        printf("stepper busy\r\n");
        return;
    }

    /* set direction (apply dirinv from EEPROM) */
    {
        uint8_t hw = (dir > 0) ^ (motorParams.dirinv.u != 0);
        HAL_GPIO_WritePin(DIR_GPIO_Port, DIR_Pin,
                          hw ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    uint32_t t = HAL_GetTick();
    while (HAL_GetTick() == t);

    moveDir        = (dir > 0) ? 1 : -1;
    stepsRemaining = 0x7FFFFFFF;   /* run "forever" */
    stepCount      = 0;
    decelCount     = 0;
    minPeriod      = MmpsToTicks(motorParams.mmpsmax.f);
    maxPeriod      = MmpsToTicks(motorParams.mmpsmin.f);
    currentPeriod  = maxPeriod;

    BuildRampTables();
    decelSteps = decelSize;        /* only used when Stop() triggers decel */
    accelIndex = 0;
    decelIndex = decelSize - 1;

    stepperState = STEPPER_ACCEL;

    TIM_OC_InitTypeDef sConfig = {0};
    sConfig.OCMode     = TIM_OCMODE_PWM1;
    sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfig.OCFastMode = TIM_OCFAST_DISABLE;
    sConfig.Pulse      = PULSE_TICKS;
    HAL_TIM_PWM_ConfigChannel(stepTim, &sConfig, TIM_CHANNEL_3);

    __HAL_TIM_SET_AUTORELOAD(stepTim, currentPeriod - 1);
    __HAL_TIM_SET_COMPARE(stepTim, TIM_CHANNEL_3, PULSE_TICKS);
    __HAL_TIM_SET_COUNTER(stepTim, 0);

    HAL_TIM_PWM_Start_IT(stepTim, TIM_CHANNEL_3);

    printf("continuous %s\r\n", dir > 0 ? "R" : "L");
}

void Stepper_Stop(void)
{
    if (stepperState == STEPPER_CONST || stepperState == STEPPER_ACCEL)
    {
        /* find matching decel index for current speed */
        decelIndex = 0;
        while (decelIndex < decelSize - 1 && decelTable[decelIndex] > currentPeriod)
            decelIndex++;
        /* ensure enough steps to decelerate */
        stepsRemaining = decelIndex + 2;
        stepperState = STEPPER_DECEL;
    }
}

uint8_t Stepper_IsBusy(void)
{
    return stepperState != STEPPER_IDLE;
}

/* ---- ISR ---------------------------------------------------------- */
/* Call from HAL_TIM_PWM_PulseFinishedCallback                         */

void Stepper_ISR(void)
{

    stepsRemaining--;
    stepCount++;
    posSteps += moveDir;  /* track absolute position */

    if (stepsRemaining == 0)
    {
        uint32_t ccr = __HAL_TIM_GET_COMPARE(stepTim, TIM_CHANNEL_3);
        while (__HAL_TIM_GET_COUNTER(stepTim) <= ccr + 10);

        /* set idle state LOW before disabling channel */
        stepTim->Instance->CR2 &= ~TIM_CR2_OIS3;  /* OIS3 = output idle state CH3 = 0 = LOW */

        HAL_TIM_PWM_Stop_IT(stepTim, TIM_CHANNEL_3);
        stepperState = STEPPER_IDLE;
        return;
    }

    switch (stepperState)
    {
    /* switch: */
    case STEPPER_ACCEL:
        accelIndex++;
        if (accelIndex >= accelSize)
        {
            currentPeriod = minPeriod;
            stepperState  = STEPPER_CONST;
        }
        else
        {
            currentPeriod = accelTable[accelIndex];
            if (stepsRemaining <= decelSteps)
            {
                /* find matching decel index for current speed */
                decelIndex = 0;
                while (decelIndex < decelSize - 1 && decelTable[decelIndex] > currentPeriod)
                    decelIndex++;
                stepperState = STEPPER_DECEL;
            }
        }
        break;

    case STEPPER_CONST:
        if (stepsRemaining <= decelSteps)
        {
            decelIndex   = decelSize - 1;  /* start from fast end of decel table */
            stepperState = STEPPER_DECEL;
        }
        break;

    case STEPPER_DECEL:
        currentPeriod = decelTable[decelIndex];
        decelIndex--;
        if (decelIndex < 0)
            currentPeriod = maxPeriod;
        break;
    case STEPPER_IDLE:
    default:
        break;
    }

    __HAL_TIM_SET_AUTORELOAD(stepTim, currentPeriod - 1);
    __HAL_TIM_SET_COMPARE(stepTim, TIM_CHANNEL_3, PULSE_TICKS);
}
