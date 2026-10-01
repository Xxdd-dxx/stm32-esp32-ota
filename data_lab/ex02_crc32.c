/* ============================================================================
 * ex02_crc32.c —— 练习 2：CRC32（按位版 + 查表版，两者必须互证）
 *
 * 对应项目代码：ota_protocol.c 的 ota_crc32_update()，多项式 0xEDB88320。
 *
 * 为什么要有"两个版本"：
 *   按位版 —— 慢，但逻辑直白，是【基准】。任何实现都要先跟它对齐。
 *   查表版 —— 快 8 倍左右，是【量产版】。表本身由按位版的"单字节内核"生成。
 *   两者对同一段数据必须给出完全相同的值，否则一定有一个错了。
 *
 * 三个关键约定（项目就是这么定的，别改）：
 *   1) crc 是"可续算"的：先传 0，之后把上一次的返回值接着传 ——
 *      分批算和一次算完结果完全相等。项目靠这个性质做"边收边算"。
 *   2) 进入时 crc = ~crc，返回前再 ~ 回来。
 *      第一次传 0，~0 == 0xFFFFFFFF，正好是标准 CRC32 的初值。
 *   3) 多项式用【反射形式】0xEDB88320（即正向 0x04C11DB7 的位反转）。
 *
 * 编译运行：
 *   gcc -Wall -Wextra -O2 -o ex02 ex02_crc32.c && ./ex02
 * ==========================================================================*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "crc32_table.h"
#include "test_vectors.h"

#define POLY 0xEDB88320u        /* 反射多项式 */

static int g_fail = 0;

#define CHECK(cond, msg) do {                                        \
        if (cond) { printf("  [ ok ] %s\n", (msg)); }                \
        else      { printf("  [FAIL] %s\n", (msg)); g_fail++; }      \
    } while (0)

/* ==========================================================================
 *  TODO 区
 * ========================================================================*/

/* --------------------------------------------------------------------------
 * TODO 1: 按位实现（基准版）
 *
 *   crc = ~crc;
 *   对每个字节 b：
 *       crc ^= b;
 *       重复 8 次：
 *           crc = (crc >> 1) ^ (POLY & -(crc & 1));
 *   返回 ~crc;
 *
 *   提示：`-(crc & 1)` 是"不用 if 的条件取负"——
 *         crc&1 == 0 时结果是 0，& 上 POLY 得 0（不异或）；
 *         crc&1 == 1 时结果是 0xFFFFFFFF，& 上 POLY 还是 POLY（异或）。
 *   注意 crc 必须是 uint32_t，右移是【逻辑右移】才不会把符号位带进来。
 * ------------------------------------------------------------------------*/
uint32_t crc32_bitwise(uint32_t crc, const uint8_t *d, size_t n)
{
    /* 你的代码 */
    (void)crc; (void)d; (void)n;
    return 0;
}

/* --------------------------------------------------------------------------
 * TODO 2: 生成 256 项的查表
 *
 *   表项 tbl[i] 的含义：以字节 i 为"当前 crc 低 8 位"时，走完 8 位的结果。
 *   生成办法：把上面那个"8 次循环的内核"单独拿 i 跑一遍 ——
 *       uint32_t c = i;
 *       for (int k = 0; k < 8; k++) c = (c >> 1) ^ (POLY & -(c & 1));
 *       tbl[i] = c;
 * ------------------------------------------------------------------------*/
void crc32_build_table(uint32_t *tbl)
{
    /* 你的代码 */
    (void)tbl;
}

/* --------------------------------------------------------------------------
 * TODO 3: 查表实现（量产版）
 *
 *   crc = ~crc;
 *   while (n--) crc = (crc >> 8) ^ tbl[(crc ^ *d++) & 0xFF];
 *   return ~crc;
 *
 *   这里的 tbl 用 crc32_build_table() 现算出来的那份，不要直接抄
 *   crc32_table.h 里的 CRC32_TABLE —— 那样就没意义了。
 * ------------------------------------------------------------------------*/
uint32_t crc32_table(uint32_t crc, const uint8_t *d, size_t n)
{
    static uint32_t tbl[256];
    static int inited = 0;
    if (!inited) { crc32_build_table(tbl); inited = 1; }

    /* 你的代码（用上面那个 tbl） */
    (void)crc; (void)d; (void)n;
    return 0;
}

/* ==========================================================================
 *  测试区 —— 不要改
 * ========================================================================*/

static void test_standard_values(void)
{
    printf("\n[1] 标准检验值（这三个背下来，以后任何平台都能验）\n");
    /* CRC32 的"标准答案"，所有实现都必须给出同样的值 */
    CHECK(crc32_bitwise(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u,
          "CRC32(\"123456789\") == 0xCBF43926");
    CHECK(crc32_bitwise(0, NULL, 0) == 0x00000000u,
          "CRC32(\"\")           == 0x00000000");
    CHECK(crc32_bitwise(0, (const uint8_t *)"The quick brown fox jumps over the lazy dog", 43)
          == 0x414FA339u,
          "CRC32(\"quick brown fox...\") == 0x414FA339");
}

static void test_streaming(void)
{
    printf("\n[2] 流式可续算（项目靠这个性质边收边算）\n");
    const char *s = "123456789";

    uint32_t one_shot = crc32_bitwise(0, (const uint8_t *)s, 9);

    uint32_t c = 0;
    c = crc32_bitwise(c, (const uint8_t *)s, 3);          /* "123" */
    c = crc32_bitwise(c, (const uint8_t *)(s + 3), 3);    /* "456" */
    c = crc32_bitwise(c, (const uint8_t *)(s + 6), 3);    /* "789" */
    CHECK(c == one_shot, "分 3 批算 == 一次算完");

    /* 逐字节喂，最极端的分批方式 */
    uint32_t c2 = 0;
    for (int i = 0; i < 9; i++) c2 = crc32_bitwise(c2, (const uint8_t *)(s + i), 1);
    CHECK(c2 == one_shot, "逐字节算 == 一次算完");

    /* 空批次不能改变结果（项目里最后一包可能长度为 0） */
    uint32_t c3 = crc32_bitwise(one_shot, NULL, 0);
    CHECK(c3 == one_shot, "中间插入一个长度 0 的批次，结果不变");
}

static void test_table_matches_bitwise(void)
{
    printf("\n[3] 查表版 vs 按位版（两者必须完全一致）\n");

    uint32_t got[256];
    crc32_build_table(got);
    int bad = 0, first = -1;
    for (int i = 0; i < 256; i++) {
        if (got[i] != CRC32_TABLE[i]) { if (first < 0) first = i; bad++; }
    }
    if (bad) printf("        第一个不同的表项 tbl[%d]: 你算出 0x%08X，应为 0x%08X\n",
                    first, (unsigned)got[first], (unsigned)CRC32_TABLE[first]);
    CHECK(bad == 0, "你生成的 256 项表 == crc32_table.h 里的基准表");

    /* 一段 4096 字节的伪随机数据，两种实现必须同值 */
    static uint8_t buf[4096];
    uint32_t x = 0xC0FFEEu;
    for (size_t i = 0; i < sizeof buf; i++) {
        x = x * 1664525u + 1013904223u;
        buf[i] = (uint8_t)(x >> 16);
    }
    CHECK(crc32_bitwise(0, buf, sizeof buf) == crc32_table(0, buf, sizeof buf),
          "4096 随机字节：按位版 == 查表版");

    /* 边界长度 */
    CHECK(crc32_bitwise(0, buf, 0) == crc32_table(0, buf, 0),   "长度 0 一致");
    CHECK(crc32_bitwise(0, buf, 1) == crc32_table(0, buf, 1),   "长度 1 一致");
    CHECK(crc32_bitwise(0, buf, 4095) == crc32_table(0, buf, 4095), "长度 4095 一致");
}

static void test_project_bodies(void)
{
    printf("\n[4] 对着项目里真实的字节去算\n");

    /* 4.1 CMD_DATA 帧的 CRC 覆盖范围 = cmd(1) + seq(2) + payload(256) = 259 字节
     *     这就是 send_frame() 里 crc32_update(0, &out[4], flen) 算的东西 */
    uint8_t body[TV_DATA_FLEN];
    body[0] = 0x02;                 /* cmd = CMD_DATA */
    body[1] = 0x00; body[2] = 0x00; /* seq = 0，小端 */
    tv_fill_data_payload(&body[3]); /* payload[i] = i */

    uint32_t c = crc32_bitwise(0, body, TV_DATA_FLEN);
    printf("        你算出 0x%08X（应 0x%08X）\n", (unsigned)c, TV_DATA_FRAME_CRC32);
    CHECK(c == TV_DATA_FRAME_CRC32,
          "CMD_DATA 帧内 CRC == 0xCEF954DF");

    /* 4.2 只算 payload 部分 —— 这是 CMD_END 里传的"整个固件的 CRC" */
    CHECK(crc32_bitwise(0, &body[3], TV_DATA_PLEN) == TV_DATA_CRC32,
          "整个固件 payload 的 CRC == 0x29058C73（CMD_END 用）");

    /* 4.3 CRC 覆盖范围必须是 [cmd..payload]，多一字节少一字节都不对
     *     用"少覆盖 1 字节"来证明范围是敏感的 */
    uint32_t wrong = crc32_bitwise(0, body, TV_DATA_FLEN - 1);
    CHECK(wrong != TV_DATA_FRAME_CRC32,
          "少覆盖 1 个字节 -> CRC 立刻不同（证明范围不能差）");

    /* 4.4 从真实帧里读出那 4 个字节，看看是不是小端 */
    printf("        帧内 4 字节的存放形式（LE）：%02X %02X %02X %02X\n",
           (unsigned)(TV_DATA_FRAME_CRC32 & 0xFF),
           (unsigned)((TV_DATA_FRAME_CRC32 >> 8) & 0xFF),
           (unsigned)((TV_DATA_FRAME_CRC32 >> 16) & 0xFF),
           (unsigned)((TV_DATA_FRAME_CRC32 >> 24) & 0xFF));
}

static void test_error_detection(void)
{
    printf("\n[5] 检错能力：改 1 个 bit 能查出来吗\n");

    static uint8_t buf[512];
    tv_fill_data_payload(buf);
    for (int i = 256; i < 512; i++) buf[i] = (uint8_t)(i * 7);

    uint32_t base = crc32_bitwise(0, buf, sizeof buf);
    CHECK(base != 0, "基准 CRC 已算出");

    /* 单 bit 翻转：512 字节 × 8 位 = 4096 个位置，全部必须检出 */
    int missed = 0;
    for (size_t byte_i = 0; byte_i < sizeof buf; byte_i++) {
        for (int bit = 0; bit < 8; bit++) {
            buf[byte_i] ^= (uint8_t)(1u << bit);
            if (crc32_bitwise(0, buf, sizeof buf) == base) missed++;
            buf[byte_i] ^= (uint8_t)(1u << bit);     /* 还原 */
        }
    }
    CHECK(missed == 0, "4096 种单 bit 错误，全部被检出");

    /* 两个字节互换：CRC 通常也能查出来（但不是 100% 保证，
     * 这正是"CRC 不是密码学哈希"的体现） */
    uint8_t a = buf[10], b = buf[11];
    buf[10] = b; buf[11] = a;
    int swapped_detected = (crc32_bitwise(0, buf, sizeof buf) != base);
    buf[10] = a; buf[11] = b;
    printf("        相邻两字节互换：%s\n",
           swapped_detected ? "检出了" : "没检出（CRC 的已知盲区，正常）");
}

int main(void)
{
    printf("=============================================================\n");
    printf(" 练习 2：CRC32 按位版 + 查表版\n");
    printf("=============================================================\n");

    test_standard_values();
    test_streaming();
    test_table_matches_bitwise();
    test_project_bodies();
    test_error_detection();

    printf("\n-------------------------------------------------------------\n");
    if (g_fail == 0)
        printf("  ALL PASS —— 你的 CRC 和 zlib 是同一个东西了。\n");
    else
        printf("  失败 %d 项。\n", g_fail);
    printf("-------------------------------------------------------------\n");
    return g_fail ? 1 : 0;
}
