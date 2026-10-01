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
#include "stm32f1xx_hal.h"

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

/* ---- OTA 接收共享状态（定义在 main.c，供 stm32f1xx_it.c 的 IDLE 中断使用）---- */
#define OTA_RX_BUF_SIZE   2048U

extern uint8_t           OtaRxBuf[];    /* DMA 写入的目标缓冲区 */
extern volatile uint8_t  OtaRxReady;    /* 1 = 有一帧待主循环处理 */
extern volatile uint16_t OtaRxLen;      /* 该帧的长度            */

void OTA_RxStart(void);                 /* 启动/重启 DMA 接收     */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* KEY0 实测接在 PE4（mcuDev ZET6 原理图：KEY0 ── PE4，另一端接 GND）
   按下 = 低电平。原 CubeMX 误配成 PA0-WKUP，导致按键无反应。 */
#define KEY_Pin GPIO_PIN_4
#define KEY_GPIO_Port GPIOE
#define LED0_Pin GPIO_PIN_0
#define LED0_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
