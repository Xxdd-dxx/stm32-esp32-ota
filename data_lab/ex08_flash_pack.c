/* ============================================================================
 * ex08_flash_pack.c —— 练习 8：字节流 → 32 位字（数据落 Flash 的最后一步）
 *
 * 对应项目代码：flash_if.c 的 flash_write_bytes()
 *
 *   for (uint32_t off = 0; off < len; off += 4) {
 *       uint8_t  tmp[4] = {0xFF, 0xFF, 0xFF, 0xFF};
 *       uint32_t chunk  = (len - off > 4) ? 4 : (len - off);
 *       for (uint32_t k = 0; k < chunk; k++) tmp[k] = src[off + k];
 *       uint32_t word = (uint32_t)tmp[0] | ((uint32_t)tmp[1] << 8)
 *                     | ((uint32_t)tmp[2] << 16) | ((uint32_t)tmp[3] << 24);
 *       HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst + off, word);
 *   }
 *
 * 为什么不能简单写成 *(uint32_t*)src ？
 *   ① 对齐：真实调用是 flash_write_bytes(addr, &frame[7], 256)，
 *      源地址是"帧缓冲基址 + 7"，不保证是 4 的倍数。
 *      Cortex-M3 对非对齐的字访问会 HardFault（M3 的 LDR 不支持非对齐地址）。
 *   ② 越界读：len 不是 4 的倍数时，强转会多读后面最多 3 个字节，
 *      把不属于本次数据的内容拼进 Flash。Flash 写错了只能擦了重来。
 *
 * 为什么补 0xFF ？
 *   Flash 擦除后每个字节都是 0xFF，而写入只能把 1 变成 0。
 *   补 0xFF 等于"这些位保持原样"，写出来的结果和"没写"等价 ——
 *   将来重写这一页时不会因为残留 0 而写不进去（Flash 写入要求目标位是 1）。
 *
 * 编译运行：
 *   gcc -Wall -Wextra -O2 -o ex08 ex08_flash_pack.c && ./ex08
 * ==========================================================================*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "crc32_table.h"
#include "test_vectors.h"

static int g_fail = 0;

#define CHECK(cond, msg) do {                                        \
        if (cond) { printf("  [ ok ] %s\n", (msg)); }                \
        else      { printf("  [FAIL] %s\n", (msg)); g_fail++; }      \
    } while (0)

/* ==========================================================================
 *  TODO 区
 * ========================================================================*/

/* --------------------------------------------------------------------------
 * TODO 1: 把 src 的 len 个字节按【小端】打包成 32 位字，写进 dst[]
 *
 *   返回值：写出的字数 = (len + 3) / 4
 *
 *   步骤：对每个字（i = 0, 1, 2, ...）：
 *       1) 先假设这 4 个字节全是 0xFF（Flash 擦除态）
 *       2) 把本次真正有的字节覆盖进去（最多 4 个）
 *       3) 按小端拼成 uint32_t：b0 | b1<<8 | b2<<16 | b3<<24
 *       4) 写进 dst[i]
 *
 *   ⚠ 每个字节都要先转 uint32_t 再移位，否则 b3 = 0x80 时是未定义行为
 *     （练习 1 里讲过这个坑）。
 *
 *   ⚠ len == 0 时直接返回 0，一个字都不写。
 *
 *   ⚠ 别忘了先判断 src 是不是 NULL（len>0 时才需要检查）。
 * ------------------------------------------------------------------------*/
size_t pack_words(const uint8_t *src, size_t len, uint32_t *dst)
{
    /* 你的代码 */
    (void)src; (void)len; (void)dst;
    return 0;
}

/* --------------------------------------------------------------------------
 * TODO 2: 逆运算 —— 从 32 位字里把 len 个字节还原出来
 *
 *   注意：只还原 len 个字节，多出来的填充字节不要往外写。
 *   （实际项目里不需要这个函数 —— 读 Flash 是直接按地址读字节的。
 *     这里让你写，是为了用"往返相等"来验证打包逻辑。）
 * ------------------------------------------------------------------------*/
void unpack_words(const uint32_t *src, size_t len, uint8_t *dst)
{
    /* 你的代码 */
    (void)src; (void)len; (void)dst;
}

/* ==========================================================================
 *  测试区 —— 不要改
 * ========================================================================*/

static void test_single_word(void)
{
    printf("\n[1] 整字打包（4 的整数倍，不需要补）\n");

    uint8_t  src[4] = {0x01, 0x02, 0x03, 0x04};
    uint32_t dst[2] = {0xDEADBEEFu, 0xDEADBEEFu};

    size_t n = pack_words(src, 4, dst);
    CHECK(n == 1, "len=4 -> 写出 1 个字");
    CHECK(dst[0] == 0x04030201u, "{01,02,03,04} -> 0x04030201（小端）");
    CHECK(dst[1] == 0xDEADBEEFu, "第 2 个字没被碰");
    CHECK(dst[0] == ((uint32_t)src[0] | ((uint32_t)src[1] << 8)
                   | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24)),
          "和逐字节手拼的结果一致");

    uint8_t  src2[8] = {1,2,3,4,5,6,7,8};
    uint32_t dst2[2] = {0,0};
    n = pack_words(src2, 8, dst2);
    CHECK(n == 2 && dst2[0] == 0x04030201u && dst2[1] == 0x08070605u,
          "len=8 -> 2 个字，第二个是 0x08070605");
}

static void test_padding(void)
{
    printf("\n[2] 不足 4 字节要补 0xFF —— 这一项专抓「直接强转」的写法\n");

    /* 后面故意放 0x99：如果实现里写了 *(uint32_t*)src，就会把它吸进来 */
    uint8_t  src[8] = {0xAA, 0xBB, 0xCC, 0x99, 0x88, 0x77, 0x66, 0x55};
    uint32_t dst[2] = {0xDEADBEEFu, 0xDEADBEEFu};

    size_t n = pack_words(src, 3, dst);
    printf("        你算出 0x%08X，应为 0xFFCCBBAA\n", (unsigned)dst[0]);
    CHECK(n == 1, "len=3 -> 写出 1 个字");
    CHECK(dst[0] == 0xFFCCBBAAu, "高字节补 0xFF（没把后面的 0x99 吸进来）");
    CHECK(dst[1] == 0xDEADBEEFu, "没有多写字");

    uint8_t  s1[4] = {0x7E, 0x00, 0x00, 0x00};
    uint32_t d1[1] = {0};
    pack_words(s1, 1, d1);
    CHECK(d1[0] == 0xFFFFFF7Eu, "len=1 -> 0xFFFFFF7E");

    uint8_t  s2[4] = {0x34, 0x12, 0x00, 0x00};
    uint32_t d2[1] = {0};
    pack_words(s2, 2, d2);
    CHECK(d2[0] == 0xFFFF1234u, "len=2 -> 0xFFFF1234");

    uint32_t d3[1] = {0xAAAAAAAAu};
    size_t n3 = pack_words(NULL, 0, d3);
    CHECK(n3 == 0 && d3[0] == 0xAAAAAAAAu, "len=0 -> 不写字、返回 0");
}

static void test_roundtrip(void)
{
    printf("\n[3] 往返：unpack(pack(x)) == x（len 从 0 到 200 全量）\n");

    static uint8_t  src[256], back[256];
    static uint32_t w[64];
    for (int i = 0; i < 256; i++) src[i] = (uint8_t)(i * 7 + 1);

    int bad = 0;
    for (size_t len = 0; len <= 200; len++) {
        memset(w, 0, sizeof w);
        memset(back, 0xCC, sizeof back);

        size_t n = pack_words(src, len, w);
        if (n != (len + 3) / 4) { printf("        len=%zu 字数不对（%zu）\n", len, n); bad++; }

        unpack_words(w, len, back);
        if (memcmp(src, back, len) != 0) { printf("        len=%zu 内容不一致\n", len); bad++; }

        /* 还原时不能多写：len 之后紧邻的 2 个字节必须还是哨兵 */
        for (size_t i = len; i < 256 && i < len + 2; i++) {
            if (back[i] != 0xCC) {
                printf("        len=%zu 多写了第 %zu 字节\n", len, i);
                bad++; break;
            }
        }
    }
    CHECK(bad == 0, "len = 0..200 全部往返成功，且没有越界写");
}

static void test_unaligned_source(void)
{
    printf("\n[4] 源地址不对齐（真实情况：payload = &frame[7]）\n");

    static uint8_t buf[64];
    for (int i = 0; i < 64; i++) buf[i] = (uint8_t)(i + 1);

    static uint32_t w[16];
    /* 故意用奇数地址当源 */
    size_t n = pack_words(buf + 1, 9, w);
    CHECK(n == 3, "9 字节 -> 3 个字");
    /* buf[1..9] = {02,03,04,05,06,07,08,09,0A} */
    CHECK(w[0] == 0x05040302u, "第 1 个字 0x05040302");
    CHECK(w[1] == 0x09080706u, "第 2 个字 0x09080706");
    CHECK(w[2] == 0xFFFFFF0Au, "第 3 个字 0xFFFFFF0A（补 0xFF）");

    static uint32_t w2[16];
    pack_words(buf, 9, w2);
    CHECK(w2[0] == 0x04030201u, "对齐源：{01,02,03,04} -> 0x04030201");

    printf("        在 x86 上对齐不对齐结果都一样（x86 硬件支持非对齐）；\n");
    printf("        但 Cortex-M3 上 *(uint32_t*)未对齐地址会直接 HardFault，\n");
    printf("        所以这套写法必须永远按字节拼装。\n");
}

static void test_real_packets(void)
{
    printf("\n[5] 拿项目真实的包尺寸来试\n");

    static uint8_t  pl[256];
    static uint32_t w[64];
    tv_fill_data_payload(pl);
    size_t n = pack_words(pl, 256, w);
    CHECK(n == 64, "256 字节 -> 64 个字（正好整除，不用补）");
    CHECK(w[0]  == 0x03020100u, "第 1 个字 == 0x03020100");
    CHECK(w[63] == 0xFFFEFDFCu, "第 64 个字 == 0xFFFEFDFC");

    static uint8_t  pl2[228];
    static uint32_t w2[57];
    for (int i = 0; i < 228; i++) pl2[i] = (uint8_t)i;
    n = pack_words(pl2, 228, w2);
    CHECK(n == 57, "最后一包 228 字节 -> 57 个字（5604 = 21*256 + 228）");

    static uint8_t  fw[5604];
    static uint32_t wfw[1401];
    static uint8_t  back[5604];
    uint32_t x = 0x13579BDFu;
    for (int i = 0; i < 5604; i++) {
        x = x * 1664525u + 1013904223u;
        fw[i] = (uint8_t)(x >> 19);
    }
    n = pack_words(fw, 5604, wfw);
    CHECK(n == 1401, "整片 5604 字节 -> 1401 个字");

    unpack_words(wfw, 5604, back);
    CHECK(memcmp(fw, back, 5604) == 0, "整片回读内容一致");
    CHECK(crc32_fast(0, fw, 5604) == crc32_fast(0, back, 5604),
          "回读后 CRC 也一致（这就是项目里写 Flash 之后的校验手法）");
}

int main(void)
{
    printf("=============================================================\n");
    printf(" 练习 8：字节流 -> 32 位字（Flash 写入粒度）\n");
    printf("=============================================================\n");

    test_single_word();
    test_padding();
    test_roundtrip();
    test_unaligned_source();
    test_real_packets();

    printf("\n=============================================================\n");
    printf(" 记住三个数字\n");
    printf("=============================================================\n");
    printf("  STM32F1 的 Flash 编程粒度最小是【半字(2B)】，HAL 还给到【字(4B)】。\n");
    printf("  F103 的页大小是【2KB】—— 擦除只能整页擦，写之前必须擦。\n");
    printf("  实测固件 5604 字节 = 1401 个字 = 2.7 个 Flash 页。\n");
    printf("=============================================================\n");

    printf("\n-------------------------------------------------------------\n");
    if (g_fail == 0)
        printf("  ALL PASS —— 数据从字节流到 Flash 的最后一步你也会了。\n");
    else
        printf("  失败 %d 项。\n", g_fail);
    printf("-------------------------------------------------------------\n");
    return g_fail ? 1 : 0;
}
