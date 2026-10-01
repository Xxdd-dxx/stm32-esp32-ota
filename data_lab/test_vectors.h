/* 本文件由 _gen_tables.py 自动生成，不要手改。
 * 预先算好的"合法帧"字节流 —— 练习 3/4/5/6 的目标与输入。
 * 每一帧都满足：AA 55 | flen(LE) | cmd | seq(LE) | payload | CRC32(LE) | 0D 0A
 */
#ifndef TEST_VECTORS_H
#define TEST_VECTORS_H

#include <stdint.h>
#include <string.h>

#include "crc32_table.h"

#define TV_OVERHEAD    13

static const uint8_t TV_ACK[16] = {
    0xAA, 0x55, 0x06, 0x00, 0x03, 0x01, 0x00, 0x01, 0x00, 0x00, 0x8A, 0x90,
    0xF4, 0x0B, 0x0D, 0x0A,
};

static const uint8_t TV_START[21] = {
    0xAA, 0x55, 0x0B, 0x00, 0x01, 0x00, 0x00, 0xE4, 0x15, 0x00, 0x00, 0x44,
    0x33, 0x22, 0x11, 0xAF, 0x04, 0x2C, 0x67, 0x0D, 0x0A,
};

static const uint8_t TV_INFO[22] = {
    0xAA, 0x55, 0x0C, 0x00, 0x08, 0x00, 0x00, 0x62, 0x6F, 0x6F, 0x74, 0x20,
    0x76, 0x31, 0x2E, 0x30, 0x1C, 0xBE, 0x28, 0x6C, 0x0D, 0x0A,
};

static const uint8_t TV_END[21] = {
    0xAA, 0x55, 0x0B, 0x00, 0x05, 0x16, 0x00, 0xE4, 0x15, 0x00, 0x00, 0x73,
    0x8C, 0x05, 0x29, 0x4C, 0xF5, 0x7C, 0xAC, 0x0D, 0x0A,
};

/* DATA 帧：payload[i] = (uint8_t)i，共 256 字节 */
#define TV_DATA_PLEN      256
#define TV_DATA_FLEN      259
#define TV_DATA_FRAME_LEN 269
/* TV_DATA_CRC32 = 纯 payload 的 CRC（CMD_END 里传的就是这个）*/
#define TV_DATA_CRC32        0x29058C73u
#define TV_END_FW_CRC32      0x29058C73u
/* TV_DATA_FRAME_CRC32 = 帧内那 4 字节，覆盖 cmd+seq+payload（共 259B）*/
#define TV_DATA_FRAME_CRC32  0xCEF954DFu

static inline void tv_fill_data_payload(uint8_t *p)
{
    for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;
}

/* 测试用的最小组帧器 —— 练习 3 的参考答案，先自己写一版再来对。
 * 练习 4/5/6 需要它来"造"输入，所以放在这里给你直接用。
 * 返回整帧长度（13 + plen）。调用者自己保证 out 够大。 */
static inline size_t tv_build(uint8_t *out, uint8_t cmd, uint16_t seq,
                              const uint8_t *payload, uint16_t plen)
{
    uint16_t flen = (uint16_t)(3 + plen);
    out[0] = 0xAA; out[1] = 0x55;
    out[2] = (uint8_t)(flen & 0xFF);
    out[3] = (uint8_t)(flen >> 8);
    out[4] = cmd;
    out[5] = (uint8_t)(seq & 0xFF);
    out[6] = (uint8_t)(seq >> 8);
    if (plen && payload) memcpy(&out[7], payload, plen);
    uint32_t crc = crc32_fast(0, &out[4], flen);
    out[7 + plen]  = (uint8_t)(crc & 0xFF);
    out[8 + plen]  = (uint8_t)((crc >> 8) & 0xFF);
    out[9 + plen]  = (uint8_t)((crc >> 16) & 0xFF);
    out[10 + plen] = (uint8_t)((crc >> 24) & 0xFF);
    out[11 + plen] = 0x0D;
    out[12 + plen] = 0x0A;
    return (size_t)(13 + plen);
}

#endif /* TEST_VECTORS_H */
