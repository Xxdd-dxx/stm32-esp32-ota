/* ============================================================================
 * ex03_frame_build.c —— 练习 3：把一包数据"包"成一帧
 *
 * 对应项目代码：ESP32 端 ota_downloader.c 的 send_frame()
 *   （项目里那个函数直接对着 static out[] 写，这里改成"客户端提供缓冲区"的写法，
 *     更好测，也更像正经库函数）
 *
 * 帧的完整布局（共 13 + plen 字节）：
 *
 *   下标     长度   内容            说明
 *   ----    ----   ------------    ------------------------------------
 *   [0]      1     0xAA            帧头高字节
 *   [1]      1     0x55            帧头低字节
 *   [2..3]   2     flen (小端)     = 3 + plen  ← 只数 cmd+seq+payload
 *   [4]      1     cmd             命令字
 *   [5..6]   2     seq (小端)      包序号
 *   [7..]    plen  payload         数据
 *   [7+p]    4     CRC32 (小端)    覆盖 [4 .. 4+flen-1]，即 cmd+seq+payload
 *   [11+p]   1     0x0D            帧尾 CR
 *   [12+p]   1     0x0A            帧尾 LF
 *
 * 记住两个数字：
 *   flen = 3 + plen     （3 = cmd 1 + seq 2）
 *   整帧 = 13 + plen    （13 = 帧头2 + 长度2 + cmd1 + seq2 + CRC4 + 帧尾2）
 *
 * 编译运行：
 *   gcc -Wall -Wextra -O2 -o ex03 ex03_frame_build.c && ./ex03
 * ==========================================================================*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "crc32_table.h"        /* 现成的查表 CRC，别自己再写一遍 */
#include "test_vectors.h"

#define MAX_PAYLOAD   256
#define FRAME_OVERHEAD 13       /* 2+2+1+2+4+2 */

static int g_fail = 0;

#define CHECK(cond, msg) do {                                        \
        if (cond) { printf("  [ ok ] %s\n", (msg)); }                \
        else      { printf("  [FAIL] %s\n", (msg)); g_fail++; }      \
    } while (0)

/* ==========================================================================
 *  TODO 区
 * ========================================================================*/

/* --------------------------------------------------------------------------
 * TODO: 组帧
 *
 *   out      : 输出缓冲区（调用者提供）
 *   out_cap  : 这块缓冲一共多大（必须自己检查，不能假定够用！）
 *   cmd      : 命令字
 *   seq      : 包序号
 *   payload  : 数据（payload_len == 0 时允许为 NULL）
 *   plen     : 数据长度
 *
 *   返回值   : 成功 -> 整帧长度（13 + plen）
 *              失败 -> 0
 *
 *   失败的情况（三种，都要处理）：
 *     ① out == NULL
 *     ② plen > MAX_PAYLOAD
 *     ③ out_cap < 13 + plen     ← 越界写是嵌入式里最难查的 bug，必须挡住
 *
 *   实现步骤：
 *     1) 前置检查，不通过直接 return 0
 *     2) 算 flen = 3 + plen
 *     3) 写帧头 AA 55
 *     4) 写 flen（小端，低字节在前）—— 想想练习 1 里怎么写的
 *     5) 写 cmd
 *     6) 写 seq（小端）
 *     7) memcpy payload 到 &out[7]
 *     8) 算 CRC 并写进 &out[7+plen]（4 字节小端）
 *     9) 写帧尾 0D 0A
 *    10) return 13 + plen
 * ------------------------------------------------------------------------*/
size_t frame_build(uint8_t *out, size_t out_cap,
                   uint8_t cmd, uint16_t seq,
                   const uint8_t *payload, uint16_t plen)
{
    /* 你的代码 */
    (void)out; (void)out_cap; (void)cmd; (void)seq; (void)payload; (void)plen;
    return 0;
}

/* ==========================================================================
 *  测试区 —— 不要改
 * ========================================================================*/

static int bytes_eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void first_diff(const uint8_t *got, const uint8_t *want, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            printf("          第 %zu 字节不同：你写的 %02X，应该是 %02X\n",
                   i, got[i], want[i]);
            return;
        }
    }
    printf("          长度可能就是错的\n");
}

static void dump(const uint8_t *p, size_t n)
{
    printf("          ");
    for (size_t i = 0; i < n; i++) printf("%02X ", p[i]);
    printf("\n");
}

static void test_against_vectors(void)
{
    printf("\n[1] 和预先算好的合法帧逐字节比对\n");
    uint8_t buf[512];

    /* --- ACK：cmd=0x03 seq=1 payload={01,00,00} --- */
    uint8_t ack_pl[3] = {0x01, 0x00, 0x00};
    memset(buf, 0, sizeof buf);
    size_t n = frame_build(buf, sizeof buf, 0x03, 1, ack_pl, 3);
    CHECK(n == sizeof TV_ACK, "ACK 帧长度 == 16");
    if (n == sizeof TV_ACK && !bytes_eq(buf, TV_ACK, n)) first_diff(buf, TV_ACK, n);
    CHECK(bytes_eq(buf, TV_ACK, sizeof TV_ACK), "ACK 帧 16 字节完全一致");

    /* --- START：cmd=0x01 seq=0 payload={size=5604, crc=0x11223344} --- */
    uint8_t st_pl[8];
    st_pl[0] = 0xE4; st_pl[1] = 0x15; st_pl[2] = 0x00; st_pl[3] = 0x00;  /* 5604 = 0x15E4 */
    st_pl[4] = 0x44; st_pl[5] = 0x33; st_pl[6] = 0x22; st_pl[7] = 0x11;
    memset(buf, 0, sizeof buf);
    n = frame_build(buf, sizeof buf, 0x01, 0, st_pl, 8);
    CHECK(n == sizeof TV_START, "START 帧长度 == 21");
    if (n == sizeof TV_START && !bytes_eq(buf, TV_START, n)) first_diff(buf, TV_START, n);
    printf("          你写的  : "); dump(buf, 21);
    printf("          标准向量: "); dump(TV_START, 21);
    CHECK(bytes_eq(buf, TV_START, sizeof TV_START), "START 帧 21 字节完全一致");

    /* --- INFO：cmd=0x08 payload="boot v1.0" --- */
    memset(buf, 0, sizeof buf);
    n = frame_build(buf, sizeof buf, 0x08, 0, (const uint8_t *)"boot v1.0", 9);
    CHECK(n == sizeof TV_INFO, "INFO 帧长度 == 22");
    if (n == sizeof TV_INFO && !bytes_eq(buf, TV_INFO, n)) first_diff(buf, TV_INFO, n);
    CHECK(bytes_eq(buf, TV_INFO, sizeof TV_INFO), "INFO 帧 22 字节完全一致");
}

static void test_big_data_frame(void)
{
    printf("\n[2] 256 字节的 DATA 帧（项目里的主力帧）\n");
    static uint8_t buf[512];
    uint8_t pl[TV_DATA_PLEN];
    tv_fill_data_payload(pl);

    size_t n = frame_build(buf, sizeof buf, 0x02, 0, pl, TV_DATA_PLEN);
    CHECK(n == TV_DATA_FRAME_LEN, "长度 == 269");
    if (n != TV_DATA_FRAME_LEN) return;

    CHECK(buf[0] == 0xAA && buf[1] == 0x55, "帧头 == AA 55");
    CHECK(buf[2] == 0x03 && buf[3] == 0x01, "flen == 259 (03 01 小端)");
    CHECK(buf[4] == 0x02, "cmd == 0x02");
    CHECK(buf[5] == 0x00 && buf[6] == 0x00, "seq == 0");
    CHECK(bytes_eq(&buf[7], pl, 256), "payload 256 字节原样搬进去");
    CHECK(buf[269 - 2] == 0x0D && buf[269 - 1] == 0x0A, "帧尾 == 0D 0A");

    uint32_t crc = (uint32_t)buf[263] | ((uint32_t)buf[264] << 8)
                 | ((uint32_t)buf[265] << 16) | ((uint32_t)buf[266] << 24);
    printf("          帧内 CRC = 0x%08X（应 0x%08X）\n", (unsigned)crc, TV_DATA_FRAME_CRC32);
    CHECK(crc == TV_DATA_FRAME_CRC32, "帧内 CRC 正确");

    /* 自己再用独立算一遍，确认 CRC 覆盖的是 [cmd..payload] 这 259 字节 */
    CHECK(crc32_fast(0, &buf[4], 259) == crc,
          "CRC 覆盖范围 == &out[4] 起 259 字节");

    printf("          前 12 字节："); dump(buf, 12);
}

static void test_empty_payload(void)
{
    printf("\n[3] 空 payload（协议规定最小帧 13 字节）\n");
    uint8_t buf[32];
    memset(buf, 0xCC, sizeof buf);
    size_t n = frame_build(buf, sizeof buf, 0x07, 0x1234, NULL, 0);
    CHECK(n == 13, "长度 == 13");
    if (n != 13) return;
    CHECK(buf[0]==0xAA && buf[1]==0x55, "帧头正确");
    CHECK(buf[2]==0x03 && buf[3]==0x00, "flen == 3");
    CHECK(buf[4]==0x07, "cmd == 0x07 (CMD_GET_INFO)");
    CHECK(buf[5]==0x34 && buf[6]==0x12, "seq == 0x1234 小端");
    CHECK(buf[11]==0x0D && buf[12]==0x0A, "帧尾正确");
    printf("          完整帧："); dump(buf, 13);
}

static void test_capacity_guard(void)
{
    printf("\n[4] 缓冲区不够时必须拒绝，而且一个字节都不能越界写\n");
    static uint8_t buf[64];
    uint8_t pl[TV_DATA_PLEN];
    tv_fill_data_payload(pl);

    /* 先铺满哨兵值，写完之后看哨兵还在不在 */
    memset(buf, 0xCC, sizeof buf);

    size_t n = frame_build(buf, 32, 0x02, 0, pl, TV_DATA_PLEN);
    CHECK(n == 0, "out_cap=32 装不下 269 -> 返回 0");

    int intact = 1;
    for (size_t i = 32; i < sizeof buf; i++) if (buf[i] != 0xCC) intact = 0;
    CHECK(intact, "32 之后的哨兵字节没被改动（没有越界写）");

    /* 刚好装下：269 字节的缓冲应该成功 */
    memset(buf, 0xCC, sizeof buf);
    n = frame_build(buf, 64, 0x02, 0, pl, TV_DATA_PLEN);
    CHECK(n == 0, "out_cap=64 仍然不够 -> 返回 0");

    /* plen 超上限 */
    memset(buf, 0xCC, sizeof buf);
    n = frame_build(buf, sizeof buf, 0x02, 0, pl, 257);
    CHECK(n == 0, "plen=257 > MAX_PAYLOAD(256) -> 返回 0");

    /* out == NULL */
    n = frame_build(NULL, 512, 0x02, 0, pl, 256);
    CHECK(n == 0, "out == NULL -> 返回 0");
}

static void test_last_short_packet(void)
{
    printf("\n[5] 最后一包往往不足 256 字节（5604 = 21*256 + 228）\n");
    uint8_t buf[512];
    uint8_t pl[228];
    for (int i = 0; i < 228; i++) pl[i] = (uint8_t)(i + 7);

    size_t n = frame_build(buf, sizeof buf, 0x02, 21, pl, 228);
    CHECK(n == 13 + 228, "长度 == 241");
    CHECK(buf[2] == 0xE7 && buf[3] == 0x00, "flen == 231 (0x00E7 小端)");
    CHECK(buf[5] == 0x15 && buf[6] == 0x00, "seq == 21");

    /* 241 字节的帧里，CRC 落在 [7+228 .. 238]，帧尾落在 [239][240] */
    uint32_t crc = (uint32_t)buf[235] | ((uint32_t)buf[236] << 8)
                 | ((uint32_t)buf[237] << 16) | ((uint32_t)buf[238] << 24);
    CHECK(crc == crc32_fast(0, &buf[4], 231), "短帧 CRC 也对");
    CHECK(buf[239] == 0x0D && buf[240] == 0x0A, "短帧帧尾仍在最后两字节");
    printf("          长度 241 的短帧尾部：");
    dump(&buf[235], 6);
}

int main(void)
{
    printf("=============================================================\n");
    printf(" 练习 3：组帧 frame_build()\n");
    printf("=============================================================\n");

    test_against_vectors();
    test_big_data_frame();
    test_empty_payload();
    test_capacity_guard();
    test_last_short_packet();

    printf("\n-------------------------------------------------------------\n");
    if (g_fail == 0)
        printf("  ALL PASS —— 你现在能从零写出一帧了。\n");
    else
        printf("  失败 %d 项。\n", g_fail);
    printf("-------------------------------------------------------------\n");
    return g_fail ? 1 : 0;
}
