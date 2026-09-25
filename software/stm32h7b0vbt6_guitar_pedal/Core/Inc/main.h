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
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define GPIO_INP_SW1_DOWN_Pin GPIO_PIN_3
#define GPIO_INP_SW1_DOWN_GPIO_Port GPIOA
#define ADC1_INP19_Expression_Pin GPIO_PIN_5
#define ADC1_INP19_Expression_GPIO_Port GPIOA
#define ADC1_INP3_POT6_Pin GPIO_PIN_6
#define ADC1_INP3_POT6_GPIO_Port GPIOA
#define ADC1_INP7_POT5_Pin GPIO_PIN_7
#define ADC1_INP7_POT5_GPIO_Port GPIOA
#define ADC1_INP4_POT4_Pin GPIO_PIN_4
#define ADC1_INP4_POT4_GPIO_Port GPIOC
#define ADC1_INP8_POT3_Pin GPIO_PIN_5
#define ADC1_INP8_POT3_GPIO_Port GPIOC
#define ADC1_INP9_POT2_Pin GPIO_PIN_0
#define ADC1_INP9_POT2_GPIO_Port GPIOB
#define ADC1_INP5_POT1_Pin GPIO_PIN_1
#define ADC1_INP5_POT1_GPIO_Port GPIOB
#define GPIO_INP_SW1_UP_Pin GPIO_PIN_9
#define GPIO_INP_SW1_UP_GPIO_Port GPIOE
#define GPIO_INP_SW2_DOWN_Pin GPIO_PIN_11
#define GPIO_INP_SW2_DOWN_GPIO_Port GPIOE
#define GPIO_INP_SW2_UP_Pin GPIO_PIN_13
#define GPIO_INP_SW2_UP_GPIO_Port GPIOE
#define GPIO_input_Footswitch2_or_ledR_Pin GPIO_PIN_8
#define GPIO_input_Footswitch2_or_ledR_GPIO_Port GPIOA
#define GPIO_In_Footsw_Pin GPIO_PIN_11
#define GPIO_In_Footsw_GPIO_Port GPIOC
#define GPIO_Out_Bypass_Pin GPIO_PIN_0
#define GPIO_Out_Bypass_GPIO_Port GPIOD
#define GPIO_Out_Mute_Pin GPIO_PIN_2
#define GPIO_Out_Mute_GPIO_Port GPIOD
#define GPIO_Output_CODEC_RESET_Pin GPIO_PIN_5
#define GPIO_Output_CODEC_RESET_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
