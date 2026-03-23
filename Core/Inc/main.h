/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

typedef struct
{
    union { float f; uint32_t u; } mmpsmax;
    union { float f; uint32_t u; } mmpsmin;
    union { float f; uint32_t u; } dvdtacc;
    union { float f; uint32_t u; } dvdtdecc;
    union { float f; uint32_t u; } jogmm;
    union { float f; uint32_t u; } stepmm;
    union { float f; uint32_t u; } spmm;
    union { float f; uint32_t u; } dirinv;   /* 0=normal, 1=invert DIR */
    union { float f; uint32_t u; } homespd;  /* homing approach speed mm/s */
    union { float f; uint32_t u; } homeoff;  /* homing offset from switch in steps */
    union { float f; uint32_t u; } debug;    /* bit0: verbose button msgs */
} params_t;

extern union {
    float f;
    uint32_t u;
} float2uint;

extern params_t motorParams;

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

// uint8_t cdcprintf(const char *format, ... );

/* RX ring buffer — defined in main.c, written by CDC_Receive_FS (usbd_cdc_if.c) */
#define RX_RING_SIZE  512
#define RX_RING_MASK  (RX_RING_SIZE - 1)
extern uint8_t           rxRing[RX_RING_SIZE];
extern volatile uint16_t rxHead;
extern volatile uint16_t rxTail;

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_USER_Pin GPIO_PIN_13
#define LED_USER_GPIO_Port GPIOC
#define ES_L_Pin GPIO_PIN_3
#define ES_L_GPIO_Port GPIOA
#define ES_L_EXTI_IRQn EXTI3_IRQn
#define ES_R_Pin GPIO_PIN_4
#define ES_R_GPIO_Port GPIOA
#define ES_R_EXTI_IRQn EXTI4_IRQn
#define BUTT_JOGL_Pin GPIO_PIN_6
#define BUTT_JOGL_GPIO_Port GPIOA
#define BUTT_JOGL_EXTI_IRQn EXTI9_5_IRQn
#define BUTT_JOGR_Pin GPIO_PIN_7
#define BUTT_JOGR_GPIO_Port GPIOA
#define BUTT_JOGR_EXTI_IRQn EXTI9_5_IRQn
#define BUTT_STEPL_Pin GPIO_PIN_0
#define BUTT_STEPL_GPIO_Port GPIOB
#define BUTT_STEPL_EXTI_IRQn EXTI0_IRQn
#define BUTT_STEPR_Pin GPIO_PIN_1
#define BUTT_STEPR_GPIO_Port GPIOB
#define BUTT_STEPR_EXTI_IRQn EXTI1_IRQn
#define PULSE_Pin GPIO_PIN_10
#define PULSE_GPIO_Port GPIOB
#define DIR_Pin GPIO_PIN_14
#define DIR_GPIO_Port GPIOB
#define BUZZ_Pin GPIO_PIN_15
#define BUZZ_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
