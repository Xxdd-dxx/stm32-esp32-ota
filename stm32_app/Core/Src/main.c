/* USER CODE BEGIN Header */
/**
 * @file    main.c
 * @brief   OTA 测试 App —— 验证 Bootloader 跳转与升级结果
 *
 * 现象：
 *   - LED0 周期闪烁（v1.0 慢闪 500ms / v1.1 快闪 150ms）
 *   - USART1（板载 CH340）打印版本与运行信息
 *
 * ⚠️ 编译前必须设置 IROM1 = 0x08008000（见 CUBE_MX_CONFIG.md）
 */
/* USER CODE END Header */
#include "main.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_VERSION      "1.0.0"        /* 升级测试时改成 1.1.0 重新编译   */
#define APP_FLASH_BASE   0x08008000UL   /* 必须与 IROM1 一致               */
#define BLINK_PERIOD_MS  1000            /* v1.1 改成 150                   */

/* LED 引脚 —— 与 Bootloader 的 ota_config.h 保持一致
   没亮就改下面两行换一个试（常见 PB0 / PB5 / PE5）                  */
#define OTA_LED_PORT     GPIOB
#define OTA_LED_PIN      GPIO_PIN_5
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
void SystemClock_Config(void);
static void OTA_LED_Init(void);
/* USER CODE END PFP */

int main(void)
{
    /* USER CODE BEGIN 1 */

    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/
    HAL_Init();

    /* USER CODE BEGIN SysInit */
    /* 中断向量表重定位到 App 区（跳转后中断必须指向这里）
       注：Bootloader 跳转前已设置 VTOR，这里再设一次做双保险，
       同时兼容直接 ST-Link 烧录调试的场景 */
    SCB->VTOR = APP_FLASH_BASE;

    /* ★ 关键修复：解除 Bootloader 遗留的全局中断屏蔽 ★
       Bootloader 的 JumpToApp() 里执行了 __disable_irq()（PRIMASK=1），
       这个状态会跟着跳转带进 App，启动文件和库函数都不会自动清除。
       不清零的话：SysTick 中断永不触发 → HAL_GetTick() 冻结 →
       主循环的 blink 判断永远不成立 → LED 不闪、无 alive 输出。
       顺序要求：必须放在 SCB->VTOR 之后（先定位向量表再开中断）。 */
    __set_PRIMASK(0);
    /* USER CODE END SysInit */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    /* USER CODE BEGIN 2 */
    OTA_LED_Init();   /* 手动初始化 LED 引脚（不依赖 CubeMX） */

    printf("\r\n");
    printf("========================================\r\n");
    printf("[APP]  STM32F103 OTA Demo\r\n");
    printf("[APP]  version : %s\r\n", APP_VERSION);
    printf("[APP]  base    : 0x%08lX\r\n", APP_FLASH_BASE);
    printf("[APP]  VTOR    : 0x%08lX\r\n", SCB->VTOR);
    printf("[APP]  stack   : 0x%08lX\r\n", *(volatile uint32_t *)APP_FLASH_BASE);
    printf("[APP]  reset   : 0x%08lX\r\n", *(volatile uint32_t *)(APP_FLASH_BASE + 4));
    printf("========================================\r\n");
    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    uint32_t lastBlink = 0;
    uint8_t  ledState  = 0;
    uint32_t sec = 0;

    while (1)
    {
        uint32_t now = HAL_GetTick();
        if (now - lastBlink > BLINK_PERIOD_MS) {
            lastBlink = now;
            ledState = !ledState;
            HAL_GPIO_WritePin(OTA_LED_PORT, OTA_LED_PIN,
                              ledState ? GPIO_PIN_SET : GPIO_PIN_RESET);
            printf("[APP] alive %lus (v%s)\r\n", sec++, APP_VERSION);
        }
    /* USER CODE END WHILE */
    }
}

/**
 * @brief System Clock Configuration（72MHz，与 Bootloader 相同）
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState    = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource   = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}

/* USER CODE BEGIN 4 */
/**
 * @brief printf 重定向到 USART1（Keil MDK）
 */
int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 10);
    return ch;
}

/**
 * @brief 手动初始化 LED 引脚（不依赖 CubeMX 生成的 gpio.c）
 */
static void OTA_LED_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Pin   = OTA_LED_PIN;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(OTA_LED_PORT, &gi);

    HAL_GPIO_WritePin(OTA_LED_PORT, OTA_LED_PIN, GPIO_PIN_SET);   /* 初始熄灭 */
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
