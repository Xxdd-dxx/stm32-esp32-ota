/* ============================================================================
 * ex01_byteorder.c —— 练习 1：整数 ↔ 字节数组（小端）
 *
 * 这是整条数据传输链路的"原子操作"。项目里所有 uint16_t / uint32_t 上下线，
 * 都要经过你在这四个函数里写的东西。
 *
 * 对应项目代码：
 *   ESP32  ota_downloader.c  send_frame()
 *              out[2] = (uint8_t)(flen & 0xFF);
 *              out[3] = (uint8_t)(flen >> 8);
 *   STM32  ota_protocol.c    ota_parse_frame()
 *              flen = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
 *
 * 编译运行：
 *   gcc -Wall -Wextra -O2 -o ex01 ex01_byteorder.c && ./ex01
 *
 * 写完务必再跑一次这个（专抓未定义行为）：
 *   gcc -Wall -Wextra -O2 -fsanitize=undefined -o ex01_ub ex01_byteorder.c && ./ex01_ub
 * ==========================================================================*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg) do {                                        \
        if (cond) { printf("  [ ok ] %s\n", (msg)); }                \
        else      { printf("  [FAIL] %s\n", (msg)); g_fail++; }      \
    } while (0)

/* ==========================================================================
 *  TODO 区 —— 只改这四个函数，别动下面的测试
 * ========================================================================*/

/* --------------------------------------------------------------------------
 * TODO 1: 把 16 位整数 v 按【小端】写进 p[0..1]（低字节在前）
 *   例：v = 0x0102  →  p[0] = 0x02, p[1] = 0x01
 * ------------------------------------------------------------------------*/
void put_u16(uint8_t *p, uint16_t v)
{
    /* 你的代码 */
    (void)p; (void)v;
}

/* --------------------------------------------------------------------------
 * TODO 2: 从 p[0..1] 拼回 uint16_t（TODO 1 的逆运算）
 * ------------------------------------------------------------------------*/
uint16_t get_u16(const uint8_t *p)
{
    /* 你的代码 */
    (void)p;
    return 0;
}

/* --------------------------------------------------------------------------
 * TODO 3: 把 32 位整数 v 按【小端】写进 p[0..3】
 *   例：v = 0x12345678  →  p[] = {78, 56, 34, 12}
 * ------------------------------------------------------------------------*/
void put_u32(uint8_t *p, uint32_t v)
{
    /* 你的代码 */
    (void)p; (void)v;
}

/* --------------------------------------------------------------------------
 * TODO 4: 从 p[0..3] 拼回 uint32_t
 *
 *   ⚠ 这里有个真坑：p[3] 是 uint8_t，在表达式里会被提升成 int（有符号）。
 *     如果 p[3] == 0x80，那么 `p[3] << 24` 会得到 0x80000000，溢出 int 的
 *     表示范围 —— 这是 C 标准里的【未定义行为】，随编译器优化级别变化。
 *     正确做法：移位【之前】先转成 uint32_t。
 *
 *   写完之后跑一次 -fsanitize=undefined 那个编译命令，看看有没有报错。
 * ------------------------------------------------------------------------*/
uint32_t get_u32(const uint8_t *p)
{
    /* 你的代码 */
    (void)p;
    return 0;
}

/* ==========================================================================
 *  测试区 —— 不要改
 * ========================================================================*/

static void test_known_pattern(void)
{
    printf("\n[1] 已知模式（手算就能验证）\n");
    uint8_t b[4];

    memset(b, 0xCC, sizeof b);
    put_u16(b, 0x0102);
    CHECK(b[0] == 0x02 && b[1] == 0x01, "put_u16(0x0102) -> {0x02, 0x01}");

    memset(b, 0xCC, sizeof b);
    put_u16(b, 0x00FF);
    CHECK(b[0] == 0xFF && b[1] == 0x00, "put_u16(0x00FF) -> {0xFF, 0x00}");

    memset(b, 0xCC, sizeof b);
    put_u16(b, 0xFFFF);
    CHECK(b[0] == 0xFF && b[1] == 0xFF, "put_u16(0xFFFF) -> {0xFF, 0xFF}");

    memset(b, 0xCC, sizeof b);
    put_u32(b, 0x12345678u);
    CHECK(b[0]==0x78 && b[1]==0x56 && b[2]==0x34 && b[3]==0x12,
          "put_u32(0x12345678) -> {78, 56, 34, 12}");

    memset(b, 0xCC, sizeof b);
    put_u32(b, 0x08044000u);            /* App B 起始地址 */
    CHECK(b[0]==0x00 && b[1]==0x40 && b[2]==0x04 && b[3]==0x08,
          "put_u32(0x08044000) -> {00, 40, 04, 08}   <- App B 地址");

    /* 逆运算方向 */
    uint8_t    src16[2] = {0x02, 0x01};
    uint8_t    src32[4] = {0x78, 0x56, 0x34, 0x12};
    CHECK(get_u16(src16) == 0x0102, "get_u16({02,01}) -> 0x0102");
    CHECK(get_u32(src32) == 0x12345678u, "get_u32({78,56,34,12}) -> 0x12345678");
}

static void test_roundtrip_u16(void)
{
    printf("\n[2] 全量往返：get_u16(put_u16(x)) == x\n");
    uint8_t b[2];
    int bad = 0, first_bad = -1;
    for (uint32_t i = 0; i <= 0xFFFFu; i++) {
        put_u16(b, (uint16_t)i);
        if (get_u16(b) != (uint16_t)i) {
            if (first_bad < 0) first_bad = (int)i;
            bad++;
        }
    }
    if (bad) printf("        第一个不通过的值：0x%04X（共 %d 个失败）\n",
                    (unsigned)first_bad, bad);
    CHECK(bad == 0, "65536 个 uint16_t 值全部往返成功");
}

static void test_roundtrip_u32(void)
{
    printf("\n[3] 往返：get_u32(put_u32(x)) == x（重点是高位为 1 的边界）\n");

    static const uint32_t v[] = {
        0u, 1u, 0xFFu, 0x100u, 0xFFFFu, 0x10000u, 0x7FFFFFFFu,
        0x80000000u,      /* ← 这一档最容易踩未定义行为 */
        0x80000001u, 0xFFFFFFFEu, 0xFFFFFFFFu,
        0x08000000u, 0x08008000u, 0x08044000u,   /* 项目里真实出现的地址 */
        5604u, 269u, 259u, 16u                    /* 项目里真实出现的长度 */
    };

    int bad = 0;
    uint8_t b[4];
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        put_u32(b, v[i]);
        uint32_t r = get_u32(b);
        if (r != v[i]) { printf("        0x%08X -> 0x%08X\n",
                               (unsigned)v[i], (unsigned)r); bad++; }
    }

    /* 再撒 10 万个伪随机值（线性同余，够用） */
    uint32_t x = 0x12345678u;
    for (int i = 0; i < 100000; i++) {
        x = x * 1664525u + 1013904223u;
        put_u32(b, x);
        if (get_u32(b) != x) { printf("        随机值失配 0x%08X\n", (unsigned)x); bad++; break; }
    }
    CHECK(bad == 0, "19 个边界值 + 100000 个随机值全部往返成功");
}

static void test_fields_in_one_buffer(void)
{
    printf("\n[4] 真实场景：多个字段连续写进同一块缓冲\n");
    /* 这一组就是 CMD_DATA 帧前 7 个字节的写法，照抄项目 send_frame() */
    uint8_t  out[12];
    uint16_t flen = 259;        /* 3 + 256 */
    uint16_t seq  = 65535;

    memset(out, 0, sizeof out);
    put_u16(&out[0], flen);
    put_u16(&out[2], seq);
    put_u32(&out[4], 0xDEADBEEFu);

    CHECK(out[0] == 0x03 && out[1] == 0x01, "flen=259      -> out[0..1] = 03 01");
    CHECK(out[2] == 0xFF && out[3] == 0xFF, "seq=65535     -> out[2..3] = FF FF");
    CHECK(out[4] == 0xEF && out[5] == 0xBE  && out[6] == 0xAD && out[7] == 0xDE,
          "0xDEADBEEF    -> out[4..7] = EF BE AD DE");

    /* 反向读回来，验证能拼对 */
    CHECK(get_u16(&out[0]) == 259,   "get_u16(out+0) == 259");
    CHECK(get_u16(&out[2]) == 65535, "get_u16(out+2) == 65535");
    CHECK(get_u32(&out[4]) == 0xDEADBEEFu, "get_u32(out+4) == 0xDEADBEEF");
}

int main(void)
{
    printf("=============================================================\n");
    printf(" 练习 1：整数 <-> 字节数组（小端）\n");
    printf("=============================================================\n");

    test_known_pattern();
    test_roundtrip_u16();
    test_roundtrip_u32();
    test_fields_in_one_buffer();

    printf("\n-------------------------------------------------------------\n");
    if (g_fail == 0)
        printf("  ALL PASS —— 四个函数都对了。\n");
    else
        printf("  失败 %d 项，回去改 TODO 1~4。\n", g_fail);
    printf("-------------------------------------------------------------\n");
    return g_fail ? 1 : 0;
}
