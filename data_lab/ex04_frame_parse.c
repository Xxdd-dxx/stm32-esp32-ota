/* ============================================================================
 * ex04_frame_parse.c —— 练习 4：把一帧解回结构体（含 6 种损坏样本）
 *
 * 对应项目代码：ota_protocol.c 的 ota_parse_frame()
 *
 * 组帧是"写出去"，解帧是"读进来"。解帧比组帧难，因为要【假设对方可能给垃圾】——
 * 每一个字节都可能是错的，你必须按固定顺序逐层挡住。
 *
 * 检查顺序（必须按这个顺序，否则错误码会不一样）：
 *   ① 空指针           -> ERR_NULL
 *   ② 长度 < 13        -> ERR_SHORT      （最小帧都不到）
 *   ③ 帧头不是 AA 55   -> ERR_HEADER
 *   ④ flen 不合理      -> ERR_LEN        （flen < 3，或 10+flen > 传入的 len）
 *   ⑤ CRC 不匹配       -> ERR_CRC
 *   ⑥ 帧尾不是 0D 0A   -> ERR_TAIL
 *   ⑦ 全通过           -> FRAME_OK，填 out
 *
 * ⚠ 为什么帧尾检查放在 CRC 之后？
 *   因为帧尾【不参与 CRC】（CRC 只覆盖 cmd+seq+payload）。
 *   先靠 CRC 挡住数据错误，再靠帧尾做"廉价的第二判据"——顺序反了的话，
 *   一个数据被改坏的帧会先报 ERR_TAIL，看起来像"边界问题"，误导排查。
 *
 * ⚠ 为什么 payload 用指针而不是拷贝？
 *   零拷贝。out->payload 直接指向调用者缓冲区里偏移 7 的位置。
 *   代价：这个指针的生命周期 == 那块缓冲的生命周期。缓冲区被覆盖，
 *        指针就是野的 —— 这就是主循环必须"先快照再解析"的原因。
 *
 * 编译运行：
 *   gcc -Wall -Wextra -O2 -o ex04 ex04_frame_parse.c && ./ex04
 * ==========================================================================*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "crc32_table.h"
#include "test_vectors.h"

/* 解析结果 —— 项目里就是这个名字，别改 */
typedef struct {
    uint8_t         cmd;            /* 命令字 */
    uint16_t        seq;            /* 包序号 */
    const uint8_t  *payload;        /* 指向【调用者的缓冲区内部】，不拷贝 */
    uint16_t        payload_len;    /* 载荷字节数 */
} OtaFrame_t;

/* 错误码 */
#define FRAME_OK      0
#define ERR_NULL     (-1)
#define ERR_SHORT    (-2)
#define ERR_HEADER   (-3)
#define ERR_LEN      (-4)
#define ERR_CRC      (-5)
#define ERR_TAIL     (-6)

static int g_fail = 0;

#define CHECK(cond, msg) do {                                        \
        if (cond) { printf("  [ ok ] %s\n", (msg)); }                \
        else      { printf("  [FAIL] %s\n", (msg)); g_fail++; }      \
    } while (0)

/* ==========================================================================
 *  TODO 区
 * ========================================================================*/

/* --------------------------------------------------------------------------
 * TODO: 解帧
 *
 *   buf / len : 收到的原始字节流
 *   out       : 解析结果
 *   返回值     : FRAME_OK(0) 或上面某个负的错误码
 *
 *   实现步骤（严格按这个顺序）：
 *     1) if (!buf || !out) return ERR_NULL;
 *     2) if (len < 13) return ERR_SHORT;
 *     3) if (buf[0] != 0xAA || buf[1] != 0x55) return ERR_HEADER;
 *
 *     4) flen = buf[2] | ((uint16_t)buf[3] << 8);
 *        if (flen < 3) return ERR_LEN;
 *        if (10 + flen > len) return ERR_LEN;      ← 10 = 2+2+4+2
 *
 *     5) 从 &buf[4] 起算 flen 个字节的 CRC，和帧里那 4 字节比：
 *          crc_calc = crc32_fast(0, &buf[4], flen);
 *          crc_recv = buf[4+flen] | (buf[5+flen]<<8)
 *                   | (buf[6+flen]<<16) | (buf[7+flen]<<24);
 *        if (crc_calc != crc_recv) return ERR_CRC;
 *
 *     6) if (buf[8+flen] != 0x0D || buf[9+flen] != 0x0A) return ERR_TAIL;
 *
 *     7) 填 out：
 *          out->cmd         = buf[4];
 *          out->seq         = buf[5] | ((uint16_t)buf[6] << 8);
 *          out->payload     = &buf[7];              ← 指针！不是 memcpy
 *          out->payload_len = (uint16_t)(flen - 3);
 *        return FRAME_OK;
 *
 *   注意：帧尾的下标 = 8 + flen，因为 8 = 2(帧头) + 2(长度) + 1(cmd) + 2(seq) + 1
 * ------------------------------------------------------------------------*/
int frame_parse(const uint8_t *buf, size_t len, OtaFrame_t *out)
{
    /* 你的代码 */
    (void)buf; (void)len; (void)out;
    return ERR_NULL;
}

/* ==========================================================================
 *  测试区 —— 不要改
 * ========================================================================*/

static const char *err_name(int rc)
{
    switch (rc) {
        case FRAME_OK:   return "OK";
        case ERR_NULL:   return "ERR_NULL";
        case ERR_SHORT:  return "ERR_SHORT";
        case ERR_HEADER: return "ERR_HEADER";
        case ERR_LEN:    return "ERR_LEN";
        case ERR_CRC:    return "ERR_CRC";
        case ERR_TAIL:   return "ERR_TAIL";
        default:         return "???";
    }
}

static void test_valid_frames(void)
{
    printf("\n[1] 四帧固定的合法样本（文件里写死的那几组字节）\n");

    OtaFrame_t f;

    /* --- ACK --- */
    memset(&f, 0, sizeof f);
    int rc = frame_parse(TV_ACK, sizeof TV_ACK, &f);
    CHECK(rc == FRAME_OK, "TV_ACK 解析成功");
    CHECK(f.cmd == 0x03, "  cmd == 0x03");
    CHECK(f.seq == 1, "  seq == 1");
    CHECK(f.payload_len == 3, "  payload_len == 3");
    CHECK(f.payload && f.payload[0]==0x01 && f.payload[1]==0x00 && f.payload[2]==0x00,
          "  payload == {01,00,00}");

    /* --- START --- */
    memset(&f, 0, sizeof f);
    rc = frame_parse(TV_START, sizeof TV_START, &f);
    CHECK(rc == FRAME_OK, "TV_START 解析成功");
    CHECK(f.cmd == 0x01 && f.seq == 0 && f.payload_len == 8,
          "  cmd=1 seq=0 payload_len=8");
    if (f.payload_len == 8) {
        uint32_t size = (uint32_t)f.payload[0] | ((uint32_t)f.payload[1] << 8)
                      | ((uint32_t)f.payload[2] << 16) | ((uint32_t)f.payload[3] << 24);
        uint32_t crc  = (uint32_t)f.payload[4] | ((uint32_t)f.payload[5] << 8)
                      | ((uint32_t)f.payload[6] << 16) | ((uint32_t)f.payload[7] << 24);
        CHECK(size == 5604,  "  payload 里的 size == 5604");
        CHECK(crc == 0x11223344u, "  payload 里的 crc == 0x11223344");
    }

    /* --- INFO --- */
    memset(&f, 0, sizeof f);
    rc = frame_parse(TV_INFO, sizeof TV_INFO, &f);
    CHECK(rc == FRAME_OK, "TV_INFO 解析成功");
    CHECK(f.cmd == 0x08 && f.seq == 0 && f.payload_len == 9, "  cmd=8 seq=0 payload_len=9");
    CHECK(f.payload && memcmp(f.payload, "boot v1.0", 9) == 0,
          "  payload 是字符串 \"boot v1.0\"");

    /* --- END --- */
    memset(&f, 0, sizeof f);
    rc = frame_parse(TV_END, sizeof TV_END, &f);
    CHECK(rc == FRAME_OK && f.cmd == 0x05 && f.seq == 22,
          "TV_END：cmd=5(CMD_END) seq=22");
}

static void test_zero_copy(void)
{
    printf("\n[2] 零拷贝：payload 必须指向【调用者的缓冲区内部】\n");
    static uint8_t buf[320];
    uint8_t pl[TV_DATA_PLEN];
    tv_fill_data_payload(pl);

    size_t n = tv_build(buf, 0x02, 0, pl, TV_DATA_PLEN);
    CHECK(n == TV_DATA_FRAME_LEN, "造出一个 269 字节的 DATA 帧");

    OtaFrame_t f;
    memset(&f, 0, sizeof f);
    int rc = frame_parse(buf, n, &f);
    CHECK(rc == FRAME_OK, "解析成功");
    CHECK(f.payload_len == 256, "payload_len == 256");
    CHECK(f.payload == &buf[7], "payload 指针 == &buf[7]（没有拷贝）");

    /* 证明它真的指向原缓冲：改原缓冲，payload 立刻跟着变 */
    if (f.payload == &buf[7]) {
        buf[7] = 0x5A;
        CHECK(f.payload[0] == 0x5A, "改 buf[7]，payload[0] 跟着变（证明是同一块内存）");
    }

    /* 顺便验证 269 字节的 DATA 帧解析后 payload 内容 */
    tv_fill_data_payload(pl);
    tv_build(buf, 0x02, 0, pl, TV_DATA_PLEN);
    frame_parse(buf, TV_DATA_FRAME_LEN, &f);
    int same = 1;
    for (int i = 0; i < 256; i++) if (f.payload[i] != (uint8_t)i) same = 0;
    CHECK(same, "payload[i] == i 全部正确");
}

static void test_corruptions(void)
{
    printf("\n[3] 六种损坏样本，必须给出【正确的错误码】\n");
    printf("        错误码对照：%s=%d %s=%d %s=%d %s=%d %s=%d %s=%d\n",
           err_name(ERR_NULL), ERR_NULL, err_name(ERR_SHORT), ERR_SHORT,
           err_name(ERR_HEADER), ERR_HEADER, err_name(ERR_LEN), ERR_LEN,
           err_name(ERR_CRC), ERR_CRC, err_name(ERR_TAIL), ERR_TAIL);

    static uint8_t buf[320];
    uint8_t pl[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
    const size_t total = 13 + 8;    /* 21 */
    OtaFrame_t f;

    /* 3.1 帧头坏了 */
    tv_build(buf, 0x02, 5, pl, 8);
    buf[0] = 0xAB;
    CHECK(frame_parse(buf, total, &f) == ERR_HEADER, "帧头第一字节改坏 -> ERR_HEADER");

    tv_build(buf, 0x02, 5, pl, 8);
    buf[1] = 0x56;
    CHECK(frame_parse(buf, total, &f) == ERR_HEADER, "帧头第二字节改坏 -> ERR_HEADER");

    /* 3.2 长度字段坏了（两种坏法） */
    tv_build(buf, 0x02, 5, pl, 8);
    buf[2] = 0xFF; buf[3] = 0xFF;               /* flen = 65535，整帧装不下 */
    CHECK(frame_parse(buf, total, &f) == ERR_LEN, "flen=65535（超出缓冲）-> ERR_LEN");

    tv_build(buf, 0x02, 5, pl, 8);
    buf[2] = 0x02; buf[3] = 0x00;               /* flen = 2 < 3 */
    CHECK(frame_parse(buf, total, &f) == ERR_LEN, "flen=2（小于最小值 3）-> ERR_LEN");

    /* 3.3 数据被改坏（CRC 覆盖区内的任何一个字节） */
    tv_build(buf, 0x02, 5, pl, 8);
    buf[10] ^= 0x01;
    CHECK(frame_parse(buf, total, &f) == ERR_CRC, "payload 翻 1 个 bit -> ERR_CRC");

    tv_build(buf, 0x02, 5, pl, 8);
    buf[4] ^= 0x01;
    CHECK(frame_parse(buf, total, &f) == ERR_CRC, "cmd 翻 1 个 bit -> ERR_CRC");

    tv_build(buf, 0x02, 5, pl, 8);
    buf[5] ^= 0x01;
    CHECK(frame_parse(buf, total, &f) == ERR_CRC, "seq 翻 1 个 bit -> ERR_CRC");

    /* 3.4 CRC 字段本身被改 */
    tv_build(buf, 0x02, 5, pl, 8);
    buf[15] ^= 0x80;
    CHECK(frame_parse(buf, total, &f) == ERR_CRC, "帧内 CRC 字段被改 -> ERR_CRC");

    /* 3.5 帧尾坏了 —— 注意 CRC 仍然是好的，因为帧尾不参与 CRC */
    tv_build(buf, 0x02, 5, pl, 8);
    buf[total - 1] = 0x0B;
    CHECK(frame_parse(buf, total, &f) == ERR_TAIL, "帧尾 0x0A 改成 0x0B -> ERR_TAIL");

    tv_build(buf, 0x02, 5, pl, 8);
    buf[total - 2] = 0x0C;
    CHECK(frame_parse(buf, total, &f) == ERR_TAIL, "帧尾 0x0D 改成 0x0C -> ERR_TAIL");

    /* 3.6 数据太短 */
    tv_build(buf, 0x02, 5, pl, 8);
    CHECK(frame_parse(buf, 12, &f) == ERR_SHORT, "只给 12 字节 -> ERR_SHORT");
    CHECK(frame_parse(buf, 0,  &f) == ERR_SHORT, "长度 0 -> ERR_SHORT");
    CHECK(frame_parse(buf, total - 1, &f) == ERR_LEN,
          "少给 1 字节（帧本身说需要 21）-> ERR_LEN");

    /* 3.7 空指针 */
    CHECK(frame_parse(NULL, total, &f) == ERR_NULL, "buf == NULL -> ERR_NULL");
    CHECK(frame_parse(buf, total, NULL) == ERR_NULL, "out == NULL -> ERR_NULL");
}

static void test_check_order(void)
{
    printf("\n[4] 检查顺序：同时坏两处时，先报哪一个\n");

    static uint8_t buf[64];
    uint8_t pl[8] = {1,2,3,4,5,6,7,8};
    OtaFrame_t f;

    tv_build(buf, 0x02, 5, pl, 8);
    buf[10] ^= 0x01;                        /* CRC 会挂 */
    buf[20]  = 0x00;                        /* 帧尾也会挂 */
    CHECK(frame_parse(buf, 21, &f) == ERR_CRC,
          "CRC 和帧尾同时坏 -> 先报 ERR_CRC（说明顺序是先 CRC 后帧尾）");

    tv_build(buf, 0x02, 5, pl, 8);
    buf[0] = 0x00;                          /* 帧头坏 */
    buf[10] ^= 0x01;                        /* CRC 坏 */
    CHECK(frame_parse(buf, 21, &f) == ERR_HEADER,
          "帧头和 CRC 同时坏 -> 先报 ERR_HEADER");

    tv_build(buf, 0x02, 5, pl, 8);
    buf[2] = 0xFF; buf[3] = 0xFF;           /* 长度坏 */
    buf[0] = 0x00;                          /* 帧头也坏 */
    CHECK(frame_parse(buf, 21, &f) == ERR_HEADER,
          "帧头和长度同时坏 -> 先报 ERR_HEADER");
}

static void test_trailing_bytes(void)
{
    printf("\n[5] 后面还跟着别的字节（流式接收的常态）\n");
    static uint8_t buf[128];
    uint8_t pl[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    size_t n = tv_build(buf, 0x02, 7, pl, 4);       /* 17 字节 */
    /* 后面再塞 30 个垃圾字节 */
    for (int i = 0; i < 30; i++) buf[n + i] = (uint8_t)(0xF0 + i);

    OtaFrame_t f;
    int rc = frame_parse(buf, n + 30, &f);
    CHECK(rc == FRAME_OK, "多给 30 个尾巴字节，仍然解析成功");
    CHECK(f.payload_len == 4, "payload_len 仍然是 4（len 字段说了算）");
    CHECK(f.payload[0] == 0xDE && f.payload[3] == 0xEF, "payload 内容正确");

    /* 注意：这一步只证明"多给字节不碍事"。
     * 如果帧前面有垃圾，frame_parse 是解不出来的 —— 那是练习 5 的活。 */
    static uint8_t buf2[128];
    memcpy(buf2, "GARBAGE!", 8);
    memcpy(buf2 + 8, buf, n);
    CHECK(frame_parse(buf2, 8 + n, &f) == ERR_HEADER,
          "帧【前面】有垃圾 -> ERR_HEADER（本函数不做滑动搜索）");
}

int main(void)
{
    printf("=============================================================\n");
    printf(" 练习 4：解帧 frame_parse()\n");
    printf("=============================================================\n");

    test_valid_frames();
    test_zero_copy();
    test_corruptions();
    test_check_order();
    test_trailing_bytes();

    printf("\n-------------------------------------------------------------\n");
    if (g_fail == 0)
        printf("  ALL PASS —— 你的解析器能挡住垃圾了。\n");
    else
        printf("  失败 %d 项。\n", g_fail);
    printf("-------------------------------------------------------------\n");
    return g_fail ? 1 : 0;
}
