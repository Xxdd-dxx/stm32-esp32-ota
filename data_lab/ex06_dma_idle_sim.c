/* ============================================================================
 * ex06_dma_idle_sim.c —— 练习 6：用纯软件复现「HT 假帧」bug
 *
 * 这是 8 个练习里最重要的一个。它不碰任何硬件 —— 只用数组模拟：
 *     · DMA 的环形缓冲区
 *     · CNDTR（DMA 剩余计数）递减
 *     · 半传输中断（HT）在"写满缓冲区一半"时触发
 *     · 空闲中断（IDLE）在"一帧发完"时触发
 * 然后你写两条接收策略，看它们在同一个字节流下的差别。
 *
 * --------------------------------------------------------------------------
 * 对照项目里的真实代码：
 *
 *   硬件侧：usart.c   hdma_usart2_rx.Init.Mode = DMA_CIRCULAR；            ← 循环模式
 *   启动   ：main.c   HAL_UART_Receive_DMA(&huart2, OtaRxBuf, 2048);       ← 缓冲 2048
 *                     紧接着 DMA1_Channel6->CCR &= ~(HTIE|TCIE);           ← 后来才关掉 HT
 *   中断   ：stm32f1xx_it.c  USART2_IRQHandler()
 *                     len = OTA_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(...);  ← 用 CNDTR 反推长度
 *
 * 为什么 HT 会捣乱？看这张图：
 *
 *   OtaRxBuf[2048]  ┌───────────────┬───────────────┐
 *                   │    1024 B     │    1024 B     │
 *                   └───────────────┴───────────────┘
 *                     ↑ HT 中断在这里！   ↑ TC 中断
 *
 *   DMA 只会数数，它不知道"帧"是什么。缓冲区收到一半（1024 字节）它就报一次
 *   "传输完成"事件 —— HAL 的 HAL_UARTEx_RxEventCallback 会照单全收，
 *   把 1024 当成"这一帧有 1024 字节"交给上层解析。
 *   可 1024 字节里装着的是 3 个半帧，解析必然失败，而且把真正的帧边界冲掉了。
 *
 * --------------------------------------------------------------------------
 * 五种配置，对着看结果（这就是你要跑出来的东西）：
 *
 *   场景   包净荷   HTIE   每帧重启   HLAF 线会不会被踩到
 *   ----   ------   ----   --------   ----------------------------
 *   S1      1024     开      否        每帧都踩（1037 > 1024）
 *   S2      1024     开      是        每帧都踩（重启也救不了大包）
 *   S3      1024     关      是        关了 HTIE，踩到也不报警
 *   S4       256     开      是        269 < 1024，永远踩不到
 *   S5       256     开      否        前几帧没事，攒到 1024 就崩
 *
 * 编译运行：
 *   gcc -Wall -Wextra -O2 -o ex06 ex06_dma_idle_sim.c && ./ex06
 * ==========================================================================*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "crc32_table.h"
#include "test_vectors.h"

#define BUF_SIZE   2048                         /* = OTA_RX_BUF_SIZE */
#define HALF       (BUF_SIZE / 2)               /* 1024：HT 中断的触发线 */
#define RING_CAP   16384                        /* 只为让观察方便，不做回绕 */
#define FW_SIZE    5604                         /* 实测固件大小 */

static int g_fail = 0;

#define CHECK(cond, msg) do {                                        \
        if (cond) { printf("  [ ok ] %s\n", (msg)); }                \
        else      { printf("  [FAIL] %s\n", (msg)); g_fail++; }      \
    } while (0)

/* ==========================================================================
 *  第一部分：硬件模型（已给你，不要改）
 * ========================================================================*/

static uint8_t  s_ring[RING_CAP];   /* DMA 的内存目标 */
static uint16_t s_cndtr;            /* DMA 剩余计数，递减 */
static size_t   s_wr;               /* 本次"启动"以来写了多少字节（重启会归零） */
static size_t   s_total;            /* 整个场景累计写了多少字节（不随重启归零） */
static int      s_htie;             /* 相当于 DMA_CCR 的 HTIE 位：半传输中断开关 */

/* 硬件里没有这两个寄存器，只是为了和"中断读到的值"做对照 */
static size_t hw_total_written(void) { return s_total; }

/* ★ 中断里唯一能读到的东西：2048 - CNDTR
 *   循环模式下 CNDTR 会自己重装，所以这个值会"回绕"变小 —— 后面你会看到后果 */
static size_t hw_received(void) { return (size_t)(BUF_SIZE - s_cndtr); }

static void hw_dma_start(void)
{
    s_cndtr = BUF_SIZE;
    s_wr    = 0;
}

static void hw_reset_all(void)
{
    hw_dma_start();
    s_total = 0;
    s_htie  = 0;
}

static void (*s_on_ht)(void);       /* HT 中断服务程序 */
static void (*s_on_idle)(void);     /* IDLE 中断服务程序 */

/* DMA 往内存里搬 n 个字节。CPU 不参与，但硬件会顺手报两种事件 */
static void hw_dma_write(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s_wr < RING_CAP) s_ring[s_wr] = p[i];
        s_wr++;
        s_total++;

        /* CNDTR 递减；减到 0 时循环模式自动重装 */
        if (s_cndtr > 0) s_cndtr--;
        else             s_cndtr = BUF_SIZE;

        /* 写满缓冲区一半 → HT 中断。HTIE 关掉就不会进来 */
        if (s_htie && s_wr != 0 && (s_wr % HALF) == 0) {
            if (s_on_ht) s_on_ht();
        }
    }
}

/* 发送方发完一帧，总线空闲 → UART 硬件置 IDLE 标志 → 进中断 */
static void hw_idle(void)
{
    if (s_on_idle) s_on_idle();
}

/* ==========================================================================
 *  第二部分：接收策略（你要写的就是这里）
 * ========================================================================*/

/* 帧校验器 —— 练习 4 你写过一版，这里是测试用的，不要求完全一致：
 * 只有"长度正好是一帧 + 头/CRC/尾都对"才返回 1 */
static int frame_ok(const uint8_t *p, size_t n)
{
    if (n < 13 || n > RING_CAP) return 0;
    if (p[0] != 0xAA || p[1] != 0x55) return 0;
    uint16_t flen = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
    if (flen < 3) return 0;
    if ((size_t)10 + flen != n)  return 0;              /* 必须正好一帧 */
    uint32_t calc = crc32_fast(0, &p[4], flen);
    uint32_t recv = (uint32_t)p[4 + flen] | ((uint32_t)p[5 + flen] << 8)
                  | ((uint32_t)p[6 + flen] << 16) | ((uint32_t)p[7 + flen] << 24);
    if (calc != recv) return 0;
    if (p[8 + flen] != 0x0D || p[9 + flen] != 0x0A) return 0;
    return 1;
}

typedef struct {
    size_t last_delivered;      /* 上次交付到哪个偏移 */
    int    restart_per_frame;   /* 策略开关：IDLE 之后是否 DMAStop + 重新启动 */

    /* 统计（deliver 里自动填） */
    int    deliveries;          /* 一共交付了多少次 */
    int    frames_ok;           /* 其中校验通过的 */
    int    frames_bad;          /* 校验失败的 */
    int    anomalies;           /* "长度算不出来"的次数（CNDTR 回绕导致） */
    int    shown;               /* 已经打印了几条明细 */
} receiver_t;

static receiver_t *g_rx;

/* --------------------------------------------------------------------------
 * 交付 [last_delivered, upto) 这一段给协议层 —— 已给你，不要改
 * ------------------------------------------------------------------------*/
static void deliver(receiver_t *r, size_t upto)
{
    r->deliveries++;

    if (upto <= r->last_delivered) {
        /* CNDTR 回绕了，算不出长度 —— 真实项目里这种帧只能丢 */
        r->anomalies++;
        r->frames_bad++;
        if (r->shown < 8) {
            printf("        第 %2d 次交付：长度算不出来（upto=%zu <= last=%zu）<- CNDTR 回绕\n",
                   r->deliveries, upto, r->last_delivered);
            r->shown++;
        }
        return;
    }

    size_t len = upto - r->last_delivered;
    int ok = frame_ok(s_ring + r->last_delivered, len);

    if (ok) r->frames_ok++; else r->frames_bad++;

    if (r->shown < 8) {
        printf("        第 %2d 次交付：长度 %4zu  %s\n",
               r->deliveries, len, ok ? "校验通过" : "**校验失败**");
        r->shown++;
    }
    r->last_delivered = upto;
}

/* --------------------------------------------------------------------------
 * TODO 1: HT 中断（DMA 半传输）来了，做什么？
 *
 *   真实代码里这一行就是罪魁祸首：把"半缓冲"当成"一帧结束"。
 *   在这里，你只需要【把已收到的部分当成一帧交付出去】：
 *
 *       deliver(g_rx, hw_received());
 *
 *   就这么一行。这一行会让上面所有场景的 S1/S2/S5 全部崩掉 ——
 *   跑一遍就明白了。
 * ------------------------------------------------------------------------*/
static void on_ht_event(void)
{
    /* 你的代码（一行） */
}

/* --------------------------------------------------------------------------
 * TODO 2: IDLE 中断（一帧发完）来了，做什么？
 *
 *   1) 交付：      deliver(g_rx, hw_received());
 *   2) 如果 g_rx->restart_per_frame 为真：
 *          hw_dma_start();                  // DMAStop + 重新启动，CNDTR 归 2048
 *          g_rx->last_delivered = 0;        // 缓冲区从 0 重新用
 *
 *   ★ 顺序很重要：必须先交付，再重启。反了就把数据冲掉了。
 * ------------------------------------------------------------------------*/
static void on_idle_event(void)
{
    /* 你的代码 */
}

/* ==========================================================================
 *  第三部分：测试脚手架（已给你，不要改）
 * ========================================================================*/

static void run_scenario(const char *name, int plen, int ht, int restart,
                         int *ok_out, int *bad_out, int *anom_out)
{
    int pkts = (FW_SIZE + plen - 1) / plen;

    printf("\n------------------------------------------------------------\n");
    printf(" %s\n", name);
    printf("   包净荷 %d 字节 -> 帧长 %d 字节 | HTIE=%s | 每帧重启=%s | 共 %d 包\n",
           plen, plen + 13, ht ? "开" : "关", restart ? "是" : "否", pkts);
    printf("------------------------------------------------------------\n");

    /* 复位硬件 + 接收方 */
    memset(s_ring, 0, sizeof s_ring);
    hw_reset_all();
    receiver_t r;
    memset(&r, 0, sizeof r);
    r.restart_per_frame = restart;
    g_rx     = &r;
    s_htie   = ht;
    s_on_ht   = on_ht_event;
    s_on_idle = on_idle_event;

    /* 造固件数据 */
    static uint8_t fw[FW_SIZE];
    uint32_t x = 0x2468ACE0u;
    for (int i = 0; i < FW_SIZE; i++) {
        x = x * 1664525u + 1013904223u;
        fw[i] = (uint8_t)(x >> 17);
    }

    hw_dma_start();

    static uint8_t frame[1200];
    for (int i = 0; i < pkts; i++) {
        int off = i * plen;
        int n   = (FW_SIZE - off > plen) ? plen : (FW_SIZE - off);
        size_t flen = tv_build(frame, 0x02, (uint16_t)i, fw + off, (uint16_t)n);

        hw_dma_write(frame, flen);      /* 一帧的字节全部灌进 DMA */
        hw_idle();                      /* 发完，总线空闲 */
    }

    printf("   小计：交付 %d 次，其中校验通过 %d 帧、失败 %d 次，长度异常 %d 次\n",
           r.deliveries, r.frames_ok, r.frames_bad, r.anomalies);
    printf("   对照：硬件累计写入 %zu 字节；场景结束时 (2048 - CNDTR) = %zu\n",
           hw_total_written(), hw_received());

    *ok_out   = r.frames_ok;
    *bad_out  = r.frames_bad;
    *anom_out = r.anomalies;

    g_rx = NULL;
    s_on_ht = s_on_idle = NULL;
}

int main(void)
{
    printf("============================================================\n");
    printf(" 练习 6：DMA 半传输（HT）假帧 —— 为什么「传几包就断」\n");
    printf("============================================================\n");

    int ok[5], bad[5], anom[5];

    run_scenario("S1  包 1024B | HTIE 开 | 不重启  <- 你最初用的配置", 1024, 1, 0,
                 &ok[0], &bad[0], &anom[0]);
    printf("   >>> 预期：一包都过不去（每帧 1037 字节都踩过 1024 线）\n");

    run_scenario("S2  包 1024B | HTIE 开 | 每帧重启", 1024, 1, 1,
                 &ok[1], &bad[1], &anom[1]);
    printf("   >>> 预期：还是几乎全挂。每帧 1037 字节自己就越过 1024 线，\n");
    printf("             重启 DMA 也没用。只有最后一个短包（497B）侥幸通过。\n");

    run_scenario("S3  包 1024B | HTIE 关 | 每帧重启", 1024, 0, 1,
                 &ok[2], &bad[2], &anom[2]);
    printf("   >>> 预期：全通过 —— 关掉 HTIE 之后，大包也安全了\n");

    run_scenario("S4  包  256B | HTIE 开 | 每帧重启  <- 项目现在的配置", 256, 1, 1,
                 &ok[3], &bad[3], &anom[3]);
    printf("   >>> 预期：全通过 —— 269 < 1024，HT 永远踩不到\n");

    run_scenario("S5  包  256B | HTIE 开 | 不重启  <- 你上次卡住的那版", 256, 1, 0,
                 &ok[4], &bad[4], &anom[4]);
    printf("   >>> 预期：前几包成功，攒到 1024 字节时被 HT 打断；\n");
    printf("             再往后 CNDTR 回绕，连长度都算不出来了。\n");

    printf("\n============================================================\n");
    printf(" 验收\n");
    printf("============================================================\n");
    CHECK(ok[0] == 0,              "S1：一包都没通过（复现「seq=0 五次重试全失败」）");
    CHECK(ok[1] == 1,              "S2：只有最后那个短包(497B)侥幸通过，其余全挂");
    CHECK(ok[2] == 6,              "S3：关掉 HTIE 后，6 包全通过");
    CHECK(ok[3] == 22,             "S4：项目现配置 22 包全通过");
    CHECK(ok[4] > 0 && ok[4] < 22, "S5：部分成功就崩（复现「前几包成功、之后全挂」）");
    CHECK(bad[4] > 0 || anom[4] > 0, "S5：伴随坏交付 / 长度异常");

    printf("\n============================================================\n");
    printf(" 总结表\n");
    printf("============================================================\n");
    printf("  场景   包净荷  HTIE  每帧重启   通过帧数   坏交付   长度异常\n");
    printf("  ----   ------  ----  --------   --------   ------   --------\n");
    printf("  S1      1024    开      否        %2d/6      %3d      %3d\n", ok[0], bad[0], anom[0]);
    printf("  S2      1024    开      是        %2d/6      %3d      %3d\n", ok[1], bad[1], anom[1]);
    printf("  S3      1024    关      是        %2d/6      %3d      %3d\n", ok[2], bad[2], anom[2]);
    printf("  S4       256    开      是       %2d/22     %3d      %3d\n", ok[3], bad[3], anom[3]);
    printf("  S5       256    开      否       %2d/22     %3d      %3d\n", ok[4], bad[4], anom[4]);

    printf("\n============================================================\n");
    printf(" 从这五个场景里能读出来的三件事\n");
    printf("============================================================\n");
    printf("  1) 单帧长度必须小于 DMA 缓冲的一半。\n");
    printf("     269 < 1024 安全；1037 > 1024 每帧都踩线。\n");
    printf("     这就是项目把 OTA_PACKET_SIZE 从 1024 降到 256 的真正原因。\n");
    printf("\n");
    printf("  2) 「每帧重启 DMA」能把 CNDTR 拉回 2048，避免它回绕；\n");
    printf("     但救不了「单帧自己就越线」的情况（看 S2）。\n");
    printf("     两件事必须一起做：包变小 + 关 HTIE + 每帧重启。\n");
    printf("\n");
    printf("  3) CNDTR 一旦回绕，(2048 - CNDTR) 就算不出长度了。\n");
    printf("     真实代码里这个值会变成一个很小的数，越过 last_delivered，\n");
    printf("     在 C 里做 size_t 减法会下溢成天文数字 —— 缓冲区直接被读穿。\n");
    printf("     项目用 DMA_NORMAL + 每帧 DMAStop/Restart 从根上避免了它。\n");
    printf("============================================================\n");

    printf("\n------------------------------------------------------------\n");
    if (g_fail == 0)
        printf("  ALL PASS —— 你现在知道那个 bug 长什么样了。\n");
    else
        printf("  失败 %d 项。\n", g_fail);
    printf("------------------------------------------------------------\n");
    return g_fail ? 1 : 0;
}
