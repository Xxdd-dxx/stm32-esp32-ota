/**
 * @file ota_protocol.h
 * @brief 自定义二进制协议：帧解析 / 组帧发送 / CRC32
 */
#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

#include "ota_config.h"

/* 解析后的帧 */
typedef struct {
    uint8_t         cmd;          /* 指令                        */
    uint16_t        seq;          /* 包序号                      */
    const uint8_t  *payload;      /* 指向接收缓冲区内的 payload  */
    uint16_t        payload_len;  /* payload 字节数              */
} OtaFrame_t;

/* 流式 CRC32 更新（初始传 0，结果与标准 zlib CRC-32 一致） */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);

/**
 * @brief  从缓冲区解析一帧并校验 CRC / 帧尾
 * @return 0 成功; -1 没找到合法帧头或长度不足; -2 CRC 错误; -3 帧尾错误
 */
int ota_parse_frame(const uint8_t *buf, uint32_t len, OtaFrame_t *frame);

/**
 * @brief  组帧并通过指定 UART 阻塞发送
 * @note   CMD_ACK / CMD_NACK / CMD_DONE 的便捷包装在 main.c 中
 */
void ota_send(UART_HandleTypeDef *huart, uint8_t cmd, uint16_t seq,
              const uint8_t *payload, uint16_t plen);

#endif /* OTA_PROTOCOL_H */
