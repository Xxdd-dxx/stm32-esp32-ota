/* ============================================================================
 * ex07_volatile.c —— 练习 7：volatile 到底防住了什么
 *
 * 对应项目代码：main.c 里的
 *      static volatile uint8_t  OtaRxReady;
 *      static volatile uint16_t OtaRxLen;
 *   （在 stm32f1xx_it.c 的中断里写，在 main.c 的 while(1) 里读）
 *
 * 为什么必须加 volatile？因为【编译器看不见中断】。
 * 在编译器眼里，整个程序只有它看到的那些 C 语句。如果它发现"这个变量在循环体里
 * 从来没人写过"，它就会认为这个值永远不变，于是：
 *
 *     · 把它读一次放进寄存器，循环里不再访问内存
 *     · 更狠的情况：把整个 判断 删掉，循环变成一个纯死循环
 *
 * 中断里写的那一次，编译器完全不知道 —— 它不模拟硬件。
 * 这就是"主循环永远等不到中断标志"的根源。
 *
 * --------------------------------------------------------------------------
 * 这个练习分两步，两步都要做：
 *
 *   第一步（看汇编）：把 -O2 生成的两段汇编摆在一起，看到差别。
 *       gcc -O2 -S -o - ex07_volatile.c | grep -A6 "^wait_"
 *
 *   第二步（跑起来）：用后台线程模拟中断，实测两个版本的行为。
 *       无 volatile 的那个：循环被优化成死循环，2.5 秒内永远退不出来。
 *       有 volatile 的那个：中断置位后立刻读到，正常退出。
 *
 * 编译运行：
 *   gcc -Wall -Wextra -O2 -o ex07 ex07_volatile.c -lpthread 2>/dev/null \
 *     || gcc -Wall -Wextra -O2 -o ex07 ex07_volatile.c
 *   ./ex07
 * ==========================================================================*/
#include <stdio.h>
#include <stdint.h>
#include <windows.h>        /* 用后台线程模拟中断（MinGW 自带，不用装东西） */

static int g_fail = 0;

#define CHECK(cond, msg) do {                                        \
        if (cond) { printf("  [ ok ] %s\n", (msg)); }                \
        else      { printf("  [FAIL] %s\n", (msg)); g_fail++; }      \
    } while (0)

/* ==========================================================================
 *  被"中断"改、被"主循环"读的两个标志
 * ========================================================================*/
static          int g_flag_plain = 0;   /* ❌ 没加 volatile */
static volatile int g_flag_vol   = 0;   /* ✅ 加了 volatile */

/* 模拟中断服务程序：800ms 之后同时把两个标志置 1 */
static DWORD WINAPI isr_sim(LPVOID p)
{
    (void)p;
    Sleep(800);
    g_flag_plain = 1;
    g_flag_vol   = 1;
    return 0;
}

/* ==========================================================================
 *  TODO 区：写两个"自旋等待"函数
 * ========================================================================*/

/* --------------------------------------------------------------------------
 * TODO 1: 自旋等 g_flag_plain（不带 volatile）
 *
 *   就一行：while (!g_flag_plain) { }
 *   然后在循环后面 return g_flag_plain;
 *
 *   ⚠ 写完先别急着跑 —— 先用下面的命令看一眼编译器把它变成了什么：
 *        gcc -O2 -S -o - ex07_volatile.c | sed -n '/^wait_/,/ret/p'
 *     你会看到它【只在进循环前读了 g_flag_plain 一次】，之后循环体里
 *     只剩一条 `jmp .L3` —— 一个纯死循环，再也不会去内存里看一眼。
 *     中断把内存改成 1 也没用，这个函数永远出不来。
 * ------------------------------------------------------------------------*/
int wait_plain(void)
{
    /* 你的代码 */
    return 0;
}

/* --------------------------------------------------------------------------
 * TODO 2: 自旋等 g_flag_vol（带 volatile）
 *
 *   写法一模一样：while (!g_flag_vol) { } return g_flag_vol;
 *
 *   再看一次汇编，这次循环体里会有一条 movl g_flag_vol(%rip), %eax ——
 *   每个循环都老老实实去内存里读一次。这才是我们要的行为。
 * ------------------------------------------------------------------------*/
int wait_vol(void)
{
    /* 你的代码 */
    return 0;
}

/* ==========================================================================
 *  测试脚手架 —— 不要改
 * ========================================================================*/

typedef int (*spin_fn)(void);
static spin_fn g_spin;

static DWORD WINAPI spin_thread(LPVOID p)
{
    (void)p;
    if (g_spin) g_spin();
    return 0;
}

/* 在后台线程里跑 f，最多等 ms 毫秒。
 * 返回 1 表示线程自己结束了，返回 0 表示卡住被超时干掉。 */
static int run_with_timeout(spin_fn f, int ms)
{
    g_spin = f;
    HANDLE h = CreateThread(NULL, 0, spin_thread, NULL, 0, NULL);
    if (!h) return -1;

    DWORD w = WaitForSingleObject(h, (DWORD)ms);
    int exited = (w == WAIT_OBJECT_0);
    if (!exited) {
        /* 循环被优化成死循环了，只能强杀 */
        TerminateThread(h, 0);
        printf("        （%d ms 超时，线程被强杀）\n", ms);
    } else {
        printf("        （线程自己退出了）\n");
    }
    CloseHandle(h);
    g_spin = NULL;
    return exited;
}

int main(void)
{
    printf("=============================================================\n");
    printf(" 练习 7：volatile —— 编译器看不见中断\n");
    printf("=============================================================\n");

    /* ---------------------------------------------------------------
     * 第一步：看汇编（这一步不用跑，照着命令行自己看一眼就行）
     * -------------------------------------------------------------*/
    printf("\n[第一步] 看编译器把这两个函数变成了什么\n");
    printf("        先执行（在 data_lab 目录下）：\n");
    printf("            gcc -O2 -S -o - ex07_volatile.c | sed -n '/^wait_/,/ret/p'\n");
    printf("        你会看到（GCC 13.1 实测输出）：\n");
    printf("            wait_plain:\n");
    printf("                movl  g_flag_plain(%%rip), %%eax   <- 只读了【一次】\n");
    printf("                testl %%eax, %%eax\n");
    printf("                jne   .L1\n");
    printf("            .L3: jmp   .L3                     <- 纯死循环，再也不读内存了\n");
    printf("            .L1: ret\n");
    printf("\n");
    printf("            wait_vol:\n");
    printf("            .L6: movl  g_flag_vol(%%rip), %%eax     <- 每轮都读内存\n");
    printf("                 testl %%eax, %%eax\n");
    printf("                 je    .L6\n");
    printf("                 movl  g_flag_vol(%%rip), %%eax\n");
    printf("                 ret\n");

    /* ---------------------------------------------------------------
     * 第二步：真的跑一遍
     * -------------------------------------------------------------*/
    printf("\n[第二步] 用后台线程模拟中断，实测行为\n");

    printf("  ── 不 volatile 的版本 ──\n");
    g_flag_plain = 0; g_flag_vol = 0;
    HANDLE isr1 = CreateThread(NULL, 0, isr_sim, NULL, 0, NULL);
    int a = run_with_timeout(wait_plain, 2500);
    WaitForSingleObject(isr1, 2000);
    CloseHandle(isr1);
    CHECK(a == 0, "无 volatile：2.5 秒内退不出来（标志根本没被读）");

    printf("  ── 有 volatile 的版本 ──\n");
    g_flag_plain = 0; g_flag_vol = 0;
    HANDLE isr2 = CreateThread(NULL, 0, isr_sim, NULL, 0, NULL);
    int b = run_with_timeout(wait_vol, 2500);
    WaitForSingleObject(isr2, 2000);
    CloseHandle(isr2);
    CHECK(b == 1, "有 volatile：中断置位后被读到，正常退出");

    /* ---------------------------------------------------------------
     * 第三步：把结论套回项目
     * -------------------------------------------------------------*/
    printf("\n=============================================================\n");
    printf(" 套回项目\n");
    printf("=============================================================\n");
    printf("  main.c 里那两个变量：\n");
    printf("      static volatile uint8_t  OtaRxReady;   <- 中断里写 1\n");
    printf("      static volatile uint16_t OtaRxLen;     <- 中断里写长度\n");
    printf("\n");
    printf("  主循环：\n");
    printf("      while (1) { if (OtaRxReady) { ...取走帧... OtaRxReady = 0; } }\n");
    printf("\n");
    printf("  如果漏了 volatile，-O2 下编译器会认为\"循环体内没人写 OtaRxReady\"，\n");
    printf("  于是把它读到寄存器里反复比较 —— 中断写进内存的 1，主循环永远看不见。\n");
    printf("  现象就是：串口在收、中断在进、但主循环像死了一样不做任何反应。\n");
    printf("\n");
    printf("  反过来说：只有【中断改、主循环读】的这类标志才需要 volatile。\n");
    printf("  OtaRxBuf[2048] 那种大数组【不要】加 —— volatile 会禁止一切优化，\n");
    printf("  让每一次数组访问都真的去读写内存，性能掉一大截。\n");
    printf("=============================================================\n");

    printf("\n-------------------------------------------------------------\n");
    if (g_fail == 0)
        printf("  ALL PASS —— 你亲眼看到编译器是怎么把中断标志吃掉的。\n");
    else
        printf("  失败 %d 项。\n", g_fail);
    printf("-------------------------------------------------------------\n");
    return g_fail ? 1 : 0;
}
