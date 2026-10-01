/* ============================================================================
 * ex05_framer_sm.c —— 练习 5：从一串乱字节里"认出"一帧（字节流状态机）
 *
 * 对应项目代码：ESP32 端 ota_downloader.c 的 read_frame()
 *   项目里那个版本的写法是：一次读 1 字节塞进 buf[]，然后检查 buf[0..1]
 *   是不是 AA 55；不是就把整个数组左移一格（memmove），继续等。
 *   这里让你写一个【状态机版】——不搬内存，只用一个"状态 + 计数"。
 *
 * 为什么需要它？
 *   UART 收到的是【字节流】，没有"消息边界"这个概念。
 *   上一帧的尾巴、噪声、半个帧头，什么都可能夹在中间。
 *   你只能一个字节一个字节地喂进去，靠状态机判断"现在处在帧的哪个位置"。
 *
 * 状态机长这样：
 *
 *   HUNT_HI ──0xAA──► HUNT_LO ──0x55──► LEN ──2字节──► BODY ──收满──► 校验
 *      ▲                 │                 │             │            │
 *      │            不是0x55           flen 不合理      （计数）      CRC/帧尾
 *      └─────────────────┴─────────────────┴─────────────────────────┘
 *                              失败就回到 HUNT_HI 重新找
 *
 * 关键设计：一旦拿到 flen，整帧长度就确定了（total = 10 + flen），
 *          接下来只需要傻数够 total 个字节再校验 —— 不用再猜。
 *
 * 编译运行：
 *   gcc -Wall -Wextra -O2 -o ex05 ex05_framer_sm.c && ./ex05
 * ==========================================================================*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "crc32_table.h"
#include "test_vectors.h"

#define FRAMER_BUF_SIZE 320         /* 够放最大的 269 字节帧 */

/* framer_feed() 的返回值 */
#define FRAMER_MORE        0        /* 还没凑够，继续喂 */
#define FRAMER_FRAME       1        /* 凑满一帧，且校验通过 */
#define FRAMER_BAD_LEN   (-1)       /* flen 不合理，已丢弃重新找帧头 */
#define FRAMER_BAD_CRC   (-2)       /* CRC 不对，已丢弃 */
#define FRAMER_BAD_TAIL  (-3)       /* 帧尾不对，已丢弃 */

static int g_fail = 0;

#define CHECK(cond, msg) do {                                        \
        if (cond) { printf("  [ ok ] %s\n", (msg)); }                \
        else      { printf("  [FAIL] %s\n", (msg)); g_fail++; }      \
    } while (0)

/* ==========================================================================
 *  状态机的持久状态 —— 你可以自由增删这里的变量
 * ========================================================================*/
static uint8_t  s_buf[FRAMER_BUF_SIZE];     /* 正在攒的帧 */
static uint16_t s_n   = 0;                  /* 已经攒了几个字节 */
static uint32_t s_total = 0;                /* 这一帧一共要多少字节（0 = 还不知道） */

/* ==========================================================================
 *  TODO 区：实现状态机
 * ========================================================================*/

/* --------------------------------------------------------------------------
 * TODO 1: 喂一个字节，推进状态机
 *
 *   返回值：FRAMER_MORE / FRAMER_FRAME / FRAMER_BAD_LEN / FRAMER_BAD_CRC / FRAMER_BAD_TAIL
 *
 *   逻辑骨架（照这个写，不用另辟蹊径）：
 *
 *     if (s_total == 0) {                       // 还在找帧头 / 读长度
 *         if (s_n == 0) {                       // 等第一个 0xAA
 *             if (b == 0xAA) s_buf[s_n++] = b;
 *             return FRAMER_MORE;
 *         }
 *         if (s_n == 1) {                       // 等 0x55
 *             if (b == 0x55)      s_buf[s_n++] = b;
 *             else if (b == 0xAA) s_buf[0] = 0xAA;   // 连续的 AA AA 55，别丢
 *             else                s_n = 0;
 *             return FRAMER_MORE;
 *         }
 *         // s_n 是 2 或 3：读长度字段
 *         s_buf[s_n++] = b;
 *         if (s_n < 4) return FRAMER_MORE;
 *
 *         uint16_t flen = (uint16_t)(s_buf[2] | ((uint16_t)s_buf[3] << 8));
 *
 *         // ★★ 这里有个真坑，务必看清楚 ★★
 *         //   如果先算 s_total = 10 + flen，再把 s_total 存进 uint16_t，
 *         //   那么 flen = 0xFFFF 时会溢出成 9 —— 一个"看起来合法"的小帧，
 *         //   状态机会继续往下数 9 个字节，把后面的真帧一起吃进去。
 *         //   正确做法：【先拿 flen 做范围检查】，确认它装得下，再算总长。
 *         if (flen < 3 || flen > (FRAMER_BUF_SIZE - 10)) {
 *             s_n = 0;
 *             return FRAMER_BAD_LEN;
 *         }
 *         s_total = (uint32_t)(10 + flen);       // ★ 整帧长度确定了
 *         return FRAMER_MORE;
 *     }
 *
 *     // s_total != 0：往死里数够 s_total 个字节
 *     s_buf[s_n++] = b;
 *     if (s_n < s_total) return FRAMER_MORE;
 *
 *     // ---- 收满了，校验 ----
 *     uint16_t flen = (uint16_t)(s_total - 10);
 *     uint32_t calc = crc32_fast(0, &s_buf[4], flen);
 *     uint32_t recv = (uint32_t)s_buf[4+flen] | ((uint32_t)s_buf[5+flen] << 8)
 *                   | ((uint32_t)s_buf[6+flen] << 16) | ((uint32_t)s_buf[7+flen] << 24);
 *     int rc;
 *     if (calc != recv)                          rc = FRAMER_BAD_CRC;
 *     else if (s_buf[8+flen] != 0x0D || s_buf[9+flen] != 0x0A) rc = FRAMER_BAD_TAIL;
 *     else                                       rc = FRAMER_FRAME;
 *
 *     if (rc != FRAMER_FRAME) { s_n = 0; s_total = 0; }   // 丢弃，重新找帧头
 *     return rc;
 *
 *   ⚠ 别忘了：成功时【不要】清零 s_n/s_total，因为外面还要通过
 *     framer_frame()/framer_len() 把这一帧拿走。清零放在 framer_reset() 里，
 *     由调用者在取走数据之后手动调。
 * ------------------------------------------------------------------------*/
int framer_feed(uint8_t b)
{
    /* 你的代码 */
    (void)b;
    return FRAMER_MORE;
}

/* --------------------------------------------------------------------------
 * TODO 2: 三个访问函数 + 复位
 * ------------------------------------------------------------------------*/
const uint8_t *framer_frame(void)   { return s_buf; }          /* 已给你 */
uint16_t       framer_len(void)     { return s_n; }            /* 已给你 */

/* 取走帧之后调用，准备接收下一帧 */
void framer_reset(void)
{
    /* 你的代码：s_n = 0; s_total = 0; */
}

/* ==========================================================================
 *  测试区 —— 不要改
 * ========================================================================*/

/* 把一个字节数组一次性喂进去，统计各类事件的次数。
 * 每收到一帧就调一次 on_frame 回调（可以为 NULL）。 */
typedef void (*frame_cb_t)(const uint8_t *frame, uint16_t len, void *ctx);

static void feed_all(const uint8_t *p, size_t n,
                     frame_cb_t cb, void *ctx,
                     int *frames, int *bad_crc, int *bad_len, int *bad_tail)
{
    *frames = *bad_crc = *bad_len = *bad_tail = 0;
    for (size_t i = 0; i < n; i++) {
        int rc = framer_feed(p[i]);
        if (rc == FRAMER_FRAME) {
            (*frames)++;
            if (cb) cb(framer_frame(), framer_len(), ctx);
            framer_reset();
        } else if (rc == FRAMER_BAD_CRC)  (*bad_crc)++;
        else if (rc == FRAMER_BAD_LEN)    (*bad_len)++;
        else if (rc == FRAMER_BAD_TAIL)   (*bad_tail)++;
    }
}

/* 用来收集回调里拿到的帧 */
static uint8_t g_got[8][FRAMER_BUF_SIZE];
static uint16_t g_got_len[8];
static int g_got_n;

static void collect_cb(const uint8_t *f, uint16_t len, void *ctx)
{
    (void)ctx;
    if (g_got_n < 8) {
        memcpy(g_got[g_got_n], f, len);
        g_got_len[g_got_n] = len;
    }
    g_got_n++;
}

static void reset_collector(void) { g_got_n = 0; memset(g_got_len, 0, sizeof g_got_len); }

static void test_single_frame(void)
{
    printf("\n[1] 逐字节喂一整帧（最基础）\n");
    reset_collector();
    int fr, bc, bl, bt;
    feed_all(TV_ACK, sizeof TV_ACK, collect_cb, NULL, &fr, &bc, &bl, &bt);

    CHECK(fr == 1, "收到 1 帧");
    CHECK(bc == 0 && bl == 0 && bt == 0, "没有误报错误");
    CHECK(g_got_n == 1 && g_got_len[0] == sizeof TV_ACK, "帧长度 == 16");
    CHECK(g_got_n >= 1 && memcmp(g_got[0], TV_ACK, sizeof TV_ACK) == 0,
          "内容与原始帧逐字节一致");

    reset_collector();
    feed_all(TV_START, sizeof TV_START, collect_cb, NULL, &fr, &bc, &bl, &bt);
    CHECK(fr == 1 && g_got_n == 1 && memcmp(g_got[0], TV_START, sizeof TV_START) == 0,
          "TV_START (21B) 也一致");

    reset_collector();
    feed_all(TV_INFO, sizeof TV_INFO, collect_cb, NULL, &fr, &bc, &bl, &bt);
    CHECK(fr == 1 && memcmp(g_got[0], TV_INFO, sizeof TV_INFO) == 0,
          "TV_INFO (22B) 也一致");

    reset_collector();
    feed_all(TV_END, sizeof TV_END, collect_cb, NULL, &fr, &bc, &bl, &bt);
    CHECK(fr == 1 && memcmp(g_got[0], TV_END, sizeof TV_END) == 0,
          "TV_END (21B) 也一致");
}

static void test_back_to_back(void)
{
    printf("\n[2] 三帧首尾相连喂进去（真实场景：ESP32 连续收到多个 ACK）\n");
    static uint8_t stream[128];
    size_t n = 0;
    memcpy(stream + n, TV_ACK,   sizeof TV_ACK);   n += sizeof TV_ACK;
    memcpy(stream + n, TV_INFO,  sizeof TV_INFO);  n += sizeof TV_INFO;
    memcpy(stream + n, TV_START, sizeof TV_START); n += sizeof TV_START;

    reset_collector();
    int fr, bc, bl, bt;
    feed_all(stream, n, collect_cb, NULL, &fr, &bc, &bl, &bt);

    CHECK(fr == 3, "收到 3 帧");
    CHECK(g_got_n == 3, "回调被调了 3 次");
    CHECK(g_got_n >= 3 && memcmp(g_got[0], TV_ACK,   sizeof TV_ACK)   == 0, "第 1 帧是 ACK");
    CHECK(g_got_n >= 3 && memcmp(g_got[1], TV_INFO,  sizeof TV_INFO)  == 0, "第 2 帧是 INFO");
    CHECK(g_got_n >= 3 && memcmp(g_got[2], TV_START, sizeof TV_START) == 0, "第 3 帧是 START");
}

static void test_garbage_prefix(void)
{
    printf("\n[3] 前面塞一段垃圾（含落单的 0xAA）\n");
    static uint8_t stream[256];
    size_t n = 0;

    /* 40 个不会造成误同步的垃圾字节 */
    for (int i = 0; i < 40; i++) stream[n++] = (uint8_t)(0x10 + (i % 0x60));
    /* 故意来两个落单的 0xAA —— 状态机不能被骗走 */
    stream[n++] = 0xAA;
    stream[n++] = 0x00;
    stream[n++] = 0xAA;
    stream[n++] = 0xAA;         /* 连续三个 A：第三个才是真帧头的前一个字节 */
    /* 现在接一帧真的 */
    memcpy(stream + n, TV_INFO, sizeof TV_INFO); n += sizeof TV_INFO;

    reset_collector();
    int fr, bc, bl, bt;
    feed_all(stream, n, collect_cb, NULL, &fr, &bc, &bl, &bt);

    printf("        （垃圾段结束在流偏移 %zu，真帧长 %zu）\n", n - sizeof TV_INFO, sizeof TV_INFO);
    CHECK(fr == 1, "垃圾里只有 1 个真帧，认出来了");
    CHECK(g_got_n >= 1 && memcmp(g_got[0], TV_INFO, sizeof TV_INFO) == 0, "内容正确");
    CHECK(bc == 0, "垃圾没有引发 CRC 误报（说明 AA AA 55 的连续处理是对的）");
}

static void test_false_sync_recovery(void)
{
    printf("\n[4] 垃圾里出现假的 AA 55（长度字段离谱）——必须能重新同步\n");
    static uint8_t stream[256];
    size_t n = 0;

    stream[n++] = 0xAA; stream[n++] = 0x55;     /* 假帧头 */
    stream[n++] = 0xFF; stream[n++] = 0xFF;     /* flen = 65535 >> 缓冲，应该立刻放弃 */
    for (int i = 0; i < 20; i++) stream[n++] = (uint8_t)(0x10 + i);  /* 假帧"身体" */
    memcpy(stream + n, TV_ACK, sizeof TV_ACK); n += sizeof TV_ACK;   /* 真帧 */

    reset_collector();
    int fr, bc, bl, bt;
    feed_all(stream, n, collect_cb, NULL, &fr, &bc, &bl, &bt);

    CHECK(bl >= 1, "识别出 flen 离谱（FRAMER_BAD_LEN）");
    CHECK(fr == 1, "重新同步成功，后面那帧真帧没丢");
    CHECK(g_got_n >= 1 && memcmp(g_got[0], TV_ACK, sizeof TV_ACK) == 0, "拿到的帧正确");

    /* 更毒的一种：flen 刚好让 10+flen 溢出成 0
     *   0xFFF6 = 65526，10 + 65526 = 65536 = 0x10000 -> 存进 uint16_t 就是 0
     *   如果不先检查 flen 的范围，状态机会以为"长度还不知道"，继续往下吃字节，
     *   把后面那帧真的 DATA 整个吞掉。 */
    static uint8_t s2[512];
    size_t m = 0;
    s2[m++] = 0xAA; s2[m++] = 0x55;
    s2[m++] = 0xF6; s2[m++] = 0xFF;
    for (int i = 0; i < 20; i++) s2[m++] = (uint8_t)(0x30 + i);
    memcpy(s2 + m, TV_INFO, sizeof TV_INFO); m += sizeof TV_INFO;

    reset_collector();
    feed_all(s2, m, collect_cb, NULL, &fr, &bc, &bl, &bt);
    CHECK(bl >= 1, "flen=0xFFF6（10+flen 会溢出成 0）也被拦住");
    CHECK(fr == 1 && g_got_n >= 1
          && memcmp(g_got[0], TV_INFO, sizeof TV_INFO) == 0,
          "溢出样本之后的那帧真帧仍然拿得到");
}

static void test_truncated_tail(void)
{
    printf("\n[5] 流在帧中间断掉（对方重启 / 掉线）\n");
    int fr, bc, bl, bt;

    reset_collector();
    feed_all(TV_ACK, sizeof TV_ACK - 3, collect_cb, NULL, &fr, &bc, &bl, &bt);
    CHECK(fr == 0 && bc == 0 && bl == 0 && bt == 0, "半个帧不产生任何事件（静静等）");

    /* 接着把剩下的补上，应该就拼出来了 */
    for (size_t i = sizeof TV_ACK - 3; i < sizeof TV_ACK; i++) {
        int rc = framer_feed(TV_ACK[i]);
        if (rc == FRAMER_FRAME) { fr++; framer_reset(); }
    }
    CHECK(fr == 1, "补上剩下 3 字节后凑成完整帧");
}

static void test_corrupt_payload(void)
{
    printf("\n[6] 帧头对、长度对，但内容被改坏\n");
    static uint8_t bad[64];
    memcpy(bad, TV_START, sizeof TV_START);
    bad[10] ^= 0x01;                 /* payload 区翻 1 bit */

    int fr, bc, bl, bt;
    reset_collector();
    feed_all(bad, sizeof TV_START, collect_cb, NULL, &fr, &bc, &bl, &bt);
    CHECK(fr == 0 && bc == 1, "报 FRAMER_BAD_CRC，不交付");

    static uint8_t bad2[64];
    memcpy(bad2, TV_START, sizeof TV_START);
    bad2[sizeof TV_START - 1] = 0x0B;   /* 帧尾坏 */

    reset_collector();
    feed_all(bad2, sizeof TV_START, collect_cb, NULL, &fr, &bc, &bl, &bt);
    CHECK(fr == 0 && bt == 1, "报 FRAMER_BAD_TAIL，不交付");
}

static void test_stress(void)
{
    printf("\n[7] 压力测试：1000 帧 + 中间夹随机垃圾\n");
    static uint8_t stream[1000 * 300];
    static uint8_t ref[1000][300];
    static uint16_t ref_len[1000];
    size_t n = 0;
    int expect = 1000;

    uint32_t x = 0x13579BDFu;
    for (int i = 0; i < expect; i++) {
        x = x * 1664525u + 1013904223u;
        uint16_t plen = (uint16_t)(x % 257);            /* 0 ~ 256 */

        uint8_t pl[256];
        for (uint16_t k = 0; k < plen; k++) {
            x = x * 1664525u + 1013904223u;
            pl[k] = (uint8_t)(x >> 13);
        }
        uint16_t len = (uint16_t)tv_build(ref[i], (uint8_t)(i & 0xFF), (uint16_t)i, pl, plen);
        ref_len[i] = len;

        /* 前面塞 0~9 个垃圾字节（避开 0xAA，保证"期望帧数"可预测） */
        uint16_t junk = (uint16_t)(x % 10);
        for (uint16_t k = 0; k < junk; k++) stream[n++] = (uint8_t)(0x20 + (k % 0x40));
        memcpy(stream + n, ref[i], len);
        n += len;
    }

    /* 逐帧比对 */
    static const uint8_t *want[1000];
    static uint16_t want_len[1000];
    for (int i = 0; i < expect; i++) { want[i] = ref[i]; want_len[i] = ref_len[i]; }

    /* 用一个"逐帧校验"的回调 */
    int idx = 0, mismatch = 0;
    for (size_t i = 0; i < n; i++) {
        int rc = framer_feed(stream[i]);
        if (rc == FRAMER_FRAME) {
            if (idx < expect) {
                if (framer_len() != want_len[idx] ||
                    memcmp(framer_frame(), want[idx], framer_len()) != 0) {
                    if (mismatch < 3)
                        printf("        第 %d 帧不一致（长 %u，应 %u）\n",
                               idx, (unsigned)framer_len(), (unsigned)want_len[idx]);
                    mismatch++;
                }
            }
            idx++;
            framer_reset();
        }
    }
    printf("        送入 %zu 字节，解析出 %d 帧，期望 %d 帧\n", n, idx, expect);
    CHECK(idx == expect, "帧数完全对上");
    CHECK(mismatch == 0, "1000 帧内容逐字节全对");
}

int main(void)
{
    printf("=============================================================\n");
    printf(" 练习 5：字节流状态机 framer_feed()\n");
    printf("=============================================================\n");

    test_single_frame();
    test_back_to_back();
    test_garbage_prefix();
    test_false_sync_recovery();
    test_truncated_tail();
    test_corrupt_payload();
    test_stress();

    printf("\n-------------------------------------------------------------\n");
    if (g_fail == 0)
        printf("  ALL PASS —— 你现在能从任何字节流里捞出帧了。\n");
    else
        printf("  失败 %d 项。\n", g_fail);
    printf("-------------------------------------------------------------\n");
    return g_fail ? 1 : 0;
}
