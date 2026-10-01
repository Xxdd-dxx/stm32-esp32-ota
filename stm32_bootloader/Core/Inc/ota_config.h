/**
 * @file ota_config.h
 * @brief OTA 系统全局配置：Flash 分区 + 协议常量（STM32F103ZET6, 512KB）
 */
#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

#include "stm32f1xx_hal.h"

/* ============ Flash 分区表 (512KB) ============ */
#define BOOTLOADER_ADDR   0x08000000UL          /* Bootloader 起点         */
#define BOOTLOADER_SIZE   (24UL  * 1024UL)      /* 24KB                   */

#define CONFIG_ADDR       0x08006000UL          /* System Config 起点     */
#define CONFIG_SIZE       (8UL   * 1024UL)       /* 8KB                    */

#define APP_A_ADDR        0x08008000UL          /* App 运行区             */
#define APP_A_SIZE        (240UL * 1024UL)      /* 240KB                  */

#define APP_B_ADDR        0x08044000UL          /* OTA 下载暂存区         */
#define APP_B_SIZE        (240UL * 1024UL)      /* 240KB                  */

/* ============ LED 状态指示灯引脚（可改） ============ */
/* mcuDev ZET6 板"自定义LED指示灯"区里 LED0 的实际接脚
   常见候选：PB0 / PB5 / PE5 / PE6
   没亮就改下面两行换一个试。看你原理图 LED0 → R20 → MCU 哪只脚最准      */
#define OTA_LED_PORT    GPIOB
#define OTA_LED_PIN     GPIO_PIN_5

/* ============ 标志魔术字 ============ */
#define OTA_MAGIC         0xAA55AA55UL

/* ============ 协议帧定义 ============ */
#define FRAME_HEAD_0      0xAA
#define FRAME_HEAD_1      0x55
#define FRAME_TAIL_0      0x0D
#define FRAME_TAIL_1      0x0A

/* 帧最小长度: 帧头2 + 长度2 + 指令1 + 序号2 + CRC4 + 帧尾2 = 13 字节 */
#define FRAME_MIN_LEN     13

/* 指令集 */
#define CMD_START         0x01   /* payload: 固件大小(4B) + 总包数(2B)          */
#define CMD_DATA          0x02   /* payload: 固件数据 (最长 1024B)               */
#define CMD_ACK           0x03   /* payload: 序号(2B) + 状态(1B)                 */
#define CMD_NACK          0x04   /* payload: 序号(2B)                            */
#define CMD_END           0x05   /* payload: 发送端累计 CRC32(4B)                */
#define CMD_DONE          0x06   /* payload: 状态(1B)                            */
#define CMD_GET_INFO      0x07   /* payload: 空                                   */
#define CMD_INFO          0x08   /* payload: 版本字符串                           */

/* ACK 状态 */
#define ACK_OK            0x00
#define ACK_ERR           0x01

/* OTA 数据包大小 (字节)
 * 必须与 ESP32 端 ota_downloader.c 中的 PACKET_SIZE 保持一致。
 * 用 256 而不是 1024 的原因：STM32 用 DMA+IDLE 接收整帧，
 * 1024B 净荷(总 1037B)在空闲窗口内易被拆帧导致 CRC 失败；
 * 256B 净荷帧总长 269B，一次攒齐概率高，且总耗时仍可接受。 */
#define OTA_PACKET_SIZE   256

/* 注意：接收缓冲区大小已统一用 main.h 里的 OTA_RX_BUF_SIZE (2048)，
 *      此处保留旧名仅为兼容，新代码请勿使用。 */
#define RX_BUF_SIZE       2048

/* 按键有效电平（mcuDev ZET6 板 KEY0 = PE4，内部上拉，按下接地 = 低电平触发）
   如果你的板子 KEY 按下是高电平，改成 GPIO_PIN_SET 即可                        */
#define KEY_ACTIVE_LEVEL  GPIO_PIN_RESET

/* Config 区 reserved[0] 的语义：复制进行中标志（掉电保护）
 *   Bootloader 在"擦除 App A → 拷贝 App B"之前先把它置 1，
 *   复制成功、写好最终 config 时自动清 0（结构体未指定的成员会被 C 清 0）。
 *   下次上电 ShouldEnterDownload() 见到 1 → 直接进下载模式，绝不跳半个固件。 */
#define CONFIG_FLAG_COPYING   1UL

/* ============ System Config 区结构 ============ */
typedef struct {
    uint32_t magic;        /* OTA_MAGIC 表示配置有效                */
    uint32_t ota_request;  /* App 置 1 请求重启进 Bootloader 下载   */
    uint32_t appA_valid;   /* App A 是否有效                        */
    uint32_t appA_size;    /* App A 固件大小                        */
    uint32_t appA_crc;     /* App A 整体 CRC32                      */
    uint32_t appA_version; /* App 固件版本号（可选）                */
    uint32_t reserved[2];  /* 保留对齐到 32 字节                    */
                           /*   reserved[0] = CONFIG_FLAG_COPYING   */
                           /*   → "上次 B→A 复制未完成"（掉电保护） */
} SystemConfig_t;

#endif /* OTA_CONFIG_H */
