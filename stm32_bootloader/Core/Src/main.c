/* USER CODE BEGIN Header */
/**
 * @file    main.c
 * @brief   双 MCU OTA 系统 —— STM32 Bootloader
 *
 * 功能：
 *   1. 上电检查升级条件（按键强制 / App 请求 / 无有效 App）
 *   2. 有有效 App → IAP 跳转运行
 *   3. 无有效 App → USART2 + DMA 接收 ESP32 发来的 OTA 协议帧
 *   4. 固件写入 App B 区 → CRC 校验 → 复制到 App A → 复位
 */
/* USER CODE END Header */
#include "main.h"
#include "dma.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ota_config.h"
#include "ota_protocol.h"
#include "flash_if.h"
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* OTA 会话状态 */
typedef struct {
    uint32_t fw_total;      /* 固件总字节数        */
    uint16_t total_pkts;    /* 总包数              */
    uint32_t received;      /* 已接收字节数        */
    uint16_t expect_seq;    /* 期望的下一个序号    */
    uint32_t crc;           /* 流式累计 CRC32      */
    uint8_t  active;        /* 会话是否进行中      */
} OtaSession_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
/* 接收缓冲区 —— DMA 直接写入 OtaRxBuf；帧就绪后由主循环快照到 g_frameBuf 再解析，
 *                避免"解析期间 DMA 又覆盖"的竞态。                          */
uint8_t           OtaRxBuf[OTA_RX_BUF_SIZE];    /* DMA 目标缓冲（对外可见） */
volatile uint8_t  OtaRxReady = 0;               /* 1 = 有帧待处理           */
volatile uint16_t OtaRxLen   = 0;               /* 帧长度                   */

static uint8_t  g_frameBuf[OTA_RX_BUF_SIZE];    /* 帧快照（解析用）         */

static OtaSession_t g_ota;

static const char *s_bootVer = "boot v1.0";
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void OTA_LED_Init(void);   /* 手动初始化 LED 引脚（不依赖 CubeMX） */
/* USER CODE BEGIN PFP */
static void     JumpToApp(uint32_t appAddr);
static uint8_t  AppLooksValid(uint32_t appAddr);
static uint8_t  ShouldEnterDownload(void);
static uint8_t  WaitForKeyInWindow(uint32_t ms);   /* 上电后 N ms 内的"按 KEY0 即进下载模式"窗口 */
static void     OTA_HandleFrame(const OtaFrame_t *f);
static void     OTA_SendAck(uint16_t seq, uint8_t status);
/* USER CODE END PFP */

int main(void)
{
    /* USER CODE BEGIN 1 */

    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/
    HAL_Init();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    /* 注意：DMA 必须先于 UART 初始化（CubeMX 偶尔生成顺序颠倒会死机） */
    MX_DMA_Init();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    /* USER CODE BEGIN 2 */

    /* 手动初始化 LED 引脚（覆盖 OTA_LED_PORT/OTA_LED_PIN，不依赖 CubeMX） */
    OTA_LED_Init();

    /* ---- 1. 上电后 3 秒"等待 KEY0"窗口 ----
     * 让你不用"按住+RESET"也能进下载模式：直接按一下 RST → 松手后 3 秒内
     * 按一下 KEY0 就强制进下载模式。错过窗口（App 完整）就跳 App。
     * LED 在等待窗口里用 50ms 超快闪，跟"进下载模式后的 200ms 慢闪"区分开。
     */
    if (WaitForKeyInWindow(3000)) {
        /* 用户按了 KEY0 → 强制进下载模式（绕过 AppLooksValid 判据） */
    } else {
        /* 3 秒没按 → 走原判据 */
        if (!ShouldEnterDownload()) {
            JumpToApp(APP_A_ADDR);          /* 正常启动，不回来 */
        }
    }

    /* ---- 2. 下载模式：LED 慢闪提示 ---- */
    uint32_t lastBlink = 0;
    uint8_t  ledState = 0;

    g_ota.active = 0;
    memset(&g_ota, 0, sizeof(g_ota));

    /* 启动接收：手动 DMA + 空闲中断（不用 HAL_UARTEx_ReceiveToIdle_DMA，
     * 因为它在 F1 HAL 上会同时打开"半传输(HT)中断"，当 DMA 计数过半时
     * 也回调 RxEventCallback，产生"假帧"把真帧冲掉 —— 这正是第 4 包
     * (768B 之后)开始失败的根因）。                                    */
    OTA_RxStart();

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* ---- 3. 处理接收到的协议帧 ---- */
        if (OtaRxReady) {
            OtaFrame_t f;
            uint16_t   len = OtaRxLen;

            /* 先快照帧数据，再立刻重启 DMA 接收，
             * 之后慢慢解析（避免解析期间 DMA 覆盖数据）。 */
            memcpy(g_frameBuf, OtaRxBuf, len);
            memset(OtaRxBuf, 0, OTA_RX_BUF_SIZE);
            OtaRxReady = 0;
            OTA_RxStart();              /* 立即重启，准备收下一帧 */

            int rc = ota_parse_frame(g_frameBuf, len, &f);
            if (rc == 0) {
                OTA_HandleFrame(&f);
            } else {
                /* 帧损坏 → NACK 让 ESP32 重传 */
                ota_send(&huart2, CMD_NACK, 0, NULL, 0);
            }
        }

        /* ---- 4. LED 心跳（非阻塞） ---- */
        if (HAL_GetTick() - lastBlink > (g_ota.active ? 50 : 200)) {
            lastBlink = HAL_GetTick();
            ledState = !ledState;
            HAL_GPIO_WritePin(OTA_LED_PORT, OTA_LED_PIN,
                              ledState ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    /* USER CODE END WHILE */
    }
}

/**
 * @brief 启动 USART2 的 DMA 接收 + 打开空闲(IDLE)中断
 *        关键点：只允许 IDLE 事件把数据"交付"给上层，
 *        DMA 的 HT(半传输) / TC(传输完成) 中断一律关闭，
 *        避免半满时就产生"假帧"。
 */
void OTA_RxStart(void)
{
    /* 1) 先停掉可能残留的 DMA 传输，防止上一次的计数干扰 */
    (void)HAL_UART_DMAStop(&huart2);
    OtaRxReady = 0;
    OtaRxLen   = 0;

    /* 2) 清 UART 的 IDLE / ORE 等标志，避免刚开就误触发 */
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);

    /* 3) 启动 DMA 接收。
     *    注意：HAL 内部会默认同时打开 TC / HT / TE 三个中断
     *          （见 stm32f1xx_hal_dma.c 的 DMA_Start_IT），
     *          所以必须先启动、再关闭 HT/TC，顺序不能反。      */
    HAL_UART_Receive_DMA(&huart2, OtaRxBuf, OTA_RX_BUF_SIZE);

    /* 4) 关掉 DMA 的半传输(HT) / 传输完成(TC) 中断 —— 只留 IDLE 作为
     *    "帧结束"信号。否则缓冲区收满一半(1024B)时会误当一帧处理。
     *    USART2_RX 对应 DMA1_Channel6（CubeMX 生成）。             */
    DMA1_Channel6->CCR &= ~(DMA_CCR_HTIE | DMA_CCR_TCIE);
    DMA1->IFCR = DMA_IFCR_CGIF6;        /* 清掉通道 6 的全部标志 */

    /* 5) 打开 UART 空闲中断 —— 这是唯一的"帧结束"信号 */
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
}

/**
 * @brief UART 接收事件回调（保留空实现，防止其它路径误用）
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    (void)huart;
    (void)Size;
}

/* ============================ OTA 业务逻辑 ============================ */

/**
 * @brief 上电后 N ms 内的"按 KEY0 即进下载模式"窗口
 *        让用户不用"按住 KEY + RESET"也能进下载模式：
 *          1. 按一下 RST 复位
 *          2. 松手后 N ms 内按一下 KEY0 → 强制进下载模式
 *          3. LED 在等待期间用 50ms 超快闪作提示（区别于下载模式的 200ms 慢闪）
 *        错过窗口（KEY0 没按 / 按了但太短）→ 走原 ShouldEnterDownload 判据
 * @return 1 = 用户在窗口内按下了 KEY0（强制进下载模式）
 *         0 = 超时（让主流程继续按原逻辑走）
 */
static uint8_t WaitForKeyInWindow(uint32_t ms)
{
    uint32_t t0      = HAL_GetTick();
    uint32_t lastLed = 0;
    uint8_t  ledSt   = 0;

    while ((uint32_t)(HAL_GetTick() - t0) < ms) {
        /* KEY0 按下（持续 30ms 消抖）→ 立即返回 1 */
        if (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == KEY_ACTIVE_LEVEL) {
            HAL_Delay(30);
            if (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == KEY_ACTIVE_LEVEL) {
                return 1;
            }
        }

        /* 50ms 心跳：与"下载模式 200ms 慢闪"明显不同，告诉用户"窗口还开着" */
        if (HAL_GetTick() - lastLed > 50) {
            lastLed = HAL_GetTick();
            ledSt   = !ledSt;
            HAL_GPIO_WritePin(OTA_LED_PORT, OTA_LED_PIN,
                              ledSt ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
    return 0;
}

/**
 * @brief 判断是否进入 Bootloader 下载模式
 *        任一条件成立即进入：
 *        ① 按住 KEY 上电（物理强制）
 *        ② Config 区有升级请求标志（App 触发，预留）
 *        ③ App A 区无有效固件（首次烧录/固件损坏）
 */
static uint8_t ShouldEnterDownload(void)
{
    SystemConfig_t *cfg = (SystemConfig_t *)CONFIG_ADDR;

    /* ① 按键：按住 KEY 再复位 → 强制下载模式（延时消抖） */
    if (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == KEY_ACTIVE_LEVEL) {
        HAL_Delay(30);
        if (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == KEY_ACTIVE_LEVEL) {
            return 1;
        }
    }

    /* ② App 请求升级（写入 ota_request 后复位） */
    if (cfg->magic == OTA_MAGIC && cfg->ota_request == 1) {
        return 1;
    }

    /* ②b 上次"App B → App A 复制"没走完（中途掉电）
     *     → 此时 App A 可能是"擦了一半"或"拷了一半"的半成品，
     *       AppLooksValid() 只看头 8 字节，完全可能误判为有效。
     *       所以这条判据必须放在 AppLooksValid() 之前。 */
    if (cfg->magic == OTA_MAGIC && cfg->reserved[0] == CONFIG_FLAG_COPYING) {
        return 1;
    }

    /* ③ App A 是否有效 */
    if (!AppLooksValid(APP_A_ADDR)) {
        return 1;
    }

    return 0;
}

/**
 * @brief 粗判 App 有效性：栈顶指针指向 RAM 且复位向量指向 Flash
 */
static uint8_t AppLooksValid(uint32_t appAddr)
{
    uint32_t sp = *(volatile uint32_t *)appAddr;
    uint32_t pc = *(volatile uint32_t *)(appAddr + 4);

    if ((sp & 0x2FFE0000UL) != 0x20000000UL) {   /* 栈顶须在 64KB SRAM 区 */
        return 0;
    }
    if ((pc & 0xFF000000UL) != 0x08000000UL) {   /* 入口须在 Flash 区    */
        return 0;
    }
    return 1;
}

/**
 * @brief 处理一帧合法的 OTA 协议命令
 */
static void OTA_HandleFrame(const OtaFrame_t *f)
{
    switch (f->cmd) {

    /* ---------- 开始升级 ---------- */
    case CMD_START: {
        if (f->payload_len != 6) {
            OTA_SendAck(f->seq, ACK_ERR);
            return;
        }
        uint32_t total = (uint32_t)f->payload[0]
                       | ((uint32_t)f->payload[1] << 8)
                       | ((uint32_t)f->payload[2] << 16)
                       | ((uint32_t)f->payload[3] << 24);
        uint16_t pkts  = (uint16_t)(f->payload[4] | ((uint16_t)f->payload[5] << 8));

        if (total == 0 || total > APP_B_SIZE || pkts == 0) {
            OTA_SendAck(f->seq, ACK_ERR);
            return;
        }

        /* 擦除 App B 暂存区 */
        if (flash_erase_region(APP_B_ADDR, APP_B_SIZE) != 0) {
            OTA_SendAck(f->seq, ACK_ERR);
            return;
        }

        memset(&g_ota, 0, sizeof(g_ota));
        g_ota.fw_total   = total;
        g_ota.total_pkts = pkts;
        g_ota.crc        = 0;
        g_ota.active     = 1;

        OTA_SendAck(f->seq, ACK_OK);
        break;
    }

    /* ---------- 固件数据包 ---------- */
    case CMD_DATA: {
        if (!g_ota.active || f->payload_len == 0) {
            OTA_SendAck(f->seq, ACK_ERR);
            return;
        }

        if (f->seq == g_ota.expect_seq) {
            /* 顺序新包 → 写入 App B */
            uint32_t dst = APP_B_ADDR + g_ota.received;
            if (flash_write_bytes(dst, f->payload, f->payload_len) != 0) {
                OTA_SendAck(f->seq, ACK_ERR);
                return;
            }
            g_ota.received += f->payload_len;
            g_ota.crc = ota_crc32_update(g_ota.crc, f->payload, f->payload_len);
            g_ota.expect_seq++;

        } else if (f->seq == (uint16_t)(g_ota.expect_seq - 1)) {
            /* 重复包（ACK 丢失后 ESP32 重传）→ 不重写，只补 ACK */
        } else {
            /* 乱序 → 拒绝 */
            OTA_SendAck(f->seq, ACK_ERR);
            return;
        }

        OTA_SendAck(f->seq, ACK_OK);
        break;
    }

    /* ---------- 传输结束 ---------- */
    case CMD_END: {
        uint8_t done_status = ACK_ERR;

        if (g_ota.active && f->payload_len == 4) {
            uint32_t crc_sent = (uint32_t)f->payload[0]
                              | ((uint32_t)f->payload[1] << 8)
                              | ((uint32_t)f->payload[2] << 16)
                              | ((uint32_t)f->payload[3] << 24);

            if (g_ota.received == g_ota.fw_total && g_ota.crc == crc_sent) {
                /* ★ 复制前先立"复制中"标志（掉电保护）
                 *   复制的第一步是把 App A 整片擦成 0xFF，中途掉电会留下半个固件，
                 *   而 AppLooksValid() 只看头 8 个字节就判"有效" → 下次上电会跳进残片。
                 *   标志写在 Config 区（0x08006000，与 App A 不同页，擦 A 碰不到它），
                 *   下次上电 ShouldEnterDownload() 见到它就强制重进下载模式重来。 */
                SystemConfig_t busy = {0};
                busy.magic       = OTA_MAGIC;
                busy.ota_request = 1;                    /* 保证任何情况都能重进下载模式 */
                busy.reserved[0] = CONFIG_FLAG_COPYING;
                config_write(&busy);

                /* 复制 App B → App A */
                if (flash_copy_appb_to_appa(g_ota.fw_total) == 0) {
                    /* 回读 App A 做 CRC 终检 */
                    uint32_t verify = ota_crc32_update(0,
                        (const uint8_t *)APP_A_ADDR, g_ota.fw_total);
                    if (verify == crc_sent) {
                        SystemConfig_t cfg = {
                            .magic       = OTA_MAGIC,
                            .ota_request = 0,
                            .appA_valid  = 1,
                            .appA_size   = g_ota.fw_total,
                            .appA_crc    = crc_sent,
                            .appA_version = 0,
                            /* reserved[] 未指定 → C 规则清 0，
                               即 CONFIG_FLAG_COPYING 自动解除 */
                        };
                        config_write(&cfg);
                        done_status = ACK_OK;
                    }
                }
            }
        }

        uint8_t pl = done_status;
        ota_send(&huart2, CMD_DONE, 0, &pl, 1);

        if (done_status == ACK_OK) {
            HAL_Delay(100);          /* 等 ACK 发完 */
            NVIC_SystemReset();       /* 复位 → 启动新固件 */
        }
        g_ota.active = 0;
        break;
    }

    /* ---------- 查询信息 ---------- */
    case CMD_GET_INFO: {
        ota_send(&huart2, CMD_INFO, 0,
                 (const uint8_t *)s_bootVer, (uint16_t)strlen(s_bootVer));
        break;
    }

    default:
        break;
    }
}

/**
 * @brief 发送 ACK（序号 + 状态）
 */
static void OTA_SendAck(uint16_t seq, uint8_t status)
{
    uint8_t pl[3] = { (uint8_t)(seq & 0xFF), (uint8_t)(seq >> 8), status };
    ota_send(&huart2, CMD_ACK, 0, pl, 3);
}

/**
 * @brief IAP 跳转到 App
 * @note  关键顺序：检查栈顶 → 关中断 → 失能外设 → 重映射向量表 → 设 MSP → 跳转
 */
static void JumpToApp(uint32_t appAddr)
{
    uint32_t jumpAddr;
    void (*jumpToApp)(void);

    /* 1. 合法性检查 */
    if (!AppLooksValid(appAddr)) {
        return;
    }

    /* 2. 取出复位向量 */
    jumpAddr   = *(volatile uint32_t *)(appAddr + 4);
    jumpToApp  = (void (*)(void))jumpAddr;

    /* 3. 关闭全局中断 */
    __disable_irq();

    /* 4. 停 SysTick（防止跳过去立刻触发中断） */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 5. 清所有 NVIC 使能/挂起位（防残留中断） */
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /* 6. 反初始化本工程用过的外设（UART/DMA/GPIO 回到复位态） */
    HAL_DMA_DeInit(huart2.hdmarx);
    HAL_UART_DeInit(&huart2);

    /* 7. 重映射中断向量表到 App 起点 */
    SCB->VTOR = appAddr;

    /* 8. 设置主栈指针为 App 栈顶 */
    __set_MSP(*(volatile uint32_t *)appAddr);

    /* 9. 跳转（开启全局中断由 App 的启动文件完成） */
    jumpToApp();
}

/**
 * @brief System Clock Configuration（HSE 8MHz → PLL×9 → 72MHz）
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Initializes the RCC Oscillators according to the specified parameters
    */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState    = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource   = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
    */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;    /* APB1=36MHz (USART2) */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;    /* APB2=72MHz (USART1)  */

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}

/* USER CODE BEGIN 4 */

/**
 * @brief 手动初始化 LED 引脚（不依赖 CubeMX 生成的 gpio.c）
 *        这样改 OTA_LED_PORT/OTA_LED_PIN 后不用回 CubeMX 重新生成
 */
static void OTA_LED_Init(void)
{
    /* 开所有常用 GPIO 端口时钟（避免根据 PORT 选时钟的麻烦） */
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

    /* 初始熄灭（默认高电平） */
    HAL_GPIO_WritePin(OTA_LED_PORT, OTA_LED_PIN, GPIO_PIN_SET);
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
