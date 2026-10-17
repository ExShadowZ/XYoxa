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
#define LED_FAULT_Pin GPIO_PIN_0
#define LED_FAULT_GPIO_Port GPIOA
#define LED_PROCESS_Pin GPIO_PIN_1
#define LED_PROCESS_GPIO_Port GPIOA
#define ESP07_TX_Pin GPIO_PIN_2
#define ESP07_TX_GPIO_Port GPIOA
#define ESP07_RX_Pin GPIO_PIN_3
#define ESP07_RX_GPIO_Port GPIOA
#define RE_2x485_Pin GPIO_PIN_0
#define RE_2x485_GPIO_Port GPIOB
#define DE_2x485_Pin GPIO_PIN_1
#define DE_2x485_GPIO_Port GPIOB
#define RE_4x485_Pin GPIO_PIN_2
#define RE_4x485_GPIO_Port GPIOB
#define DE_4x485_Pin GPIO_PIN_10
#define DE_4x485_GPIO_Port GPIOB
#define A1_Pin GPIO_PIN_12
#define A1_GPIO_Port GPIOB
#define A0_Pin GPIO_PIN_13
#define A0_GPIO_Port GPIOB
#define E0_Pin GPIO_PIN_8
#define E0_GPIO_Port GPIOA
#define RS232_422_485_TX_Pin GPIO_PIN_9
#define RS232_422_485_TX_GPIO_Port GPIOA
#define RS232_422_485_RX_Pin GPIO_PIN_10
#define RS232_422_485_RX_GPIO_Port GPIOA
#define W5500_CS_Pin GPIO_PIN_15
#define W5500_CS_GPIO_Port GPIOA
#define W5500_SCK_Pin GPIO_PIN_3
#define W5500_SCK_GPIO_Port GPIOB
#define W5500_MISO_Pin GPIO_PIN_4
#define W5500_MISO_GPIO_Port GPIOB
#define W5500_MOSI_Pin GPIO_PIN_5
#define W5500_MOSI_GPIO_Port GPIOB
#define W5500_INT_Pin GPIO_PIN_6
#define W5500_INT_GPIO_Port GPIOB
#define W5500_RST_Pin GPIO_PIN_7
#define W5500_RST_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define CMD_MAGIC_STR     "69EF13A"
#define CMD_MAGIC_LEN     7          /* длина без запятой */
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
