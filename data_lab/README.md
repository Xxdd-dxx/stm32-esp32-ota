# 数据传输练习（data_lab）

把 OTA 项目里「数据传输」那条链路拆成 8 个小练习，**全部在 PC 上做，不需要板子**。
每个 `.c` 文件都是「骨架 + 自测 main」：你把 `TODO` 填完，编译运行，看到
`ALL PASS` 就算过关。

## 一、环境

已确认可用：

| 工具 | 版本 | 说明 |
|---|---|---|
| gcc (MinGW-w64) | 13.1.0 | 直接 `gcc` 就是它 |
| python | 3.13 | 只用来生成 `crc32_table.h` / `test_vectors.h`（已生成好，不用再跑）|

编译一个练习：

```bash
cd D:/workBuddyFile/2026-09-10-14-17-26/ota_project/data_lab
gcc -Wall -Wextra -O2 -o ex01 ex01_byteorder.c && ./ex01
```

或者用脚本（推荐）：

```bash
./run.sh 1        # 只跑练习 1
./run.sh all      # 全部跑一遍
./run.sh 1 ub     # 练习 1 额外开 UBSan 检查未定义行为
```

## 二、练习清单

| # | 文件 | 练什么 | 对应的项目代码（写不出来去这里抄思路）|
|---|---|---|---|
| 1 | `ex01_byteorder.c` | 整数 ↔ 字节数组（小端拆拼） | `ota_downloader.c` `send_frame()` 的 `out[2..6]` |
| 2 | `ex02_crc32.c` | CRC32 按位版 + 查表版，两者互证 | `ota_protocol.c` `ota_crc32_update()` |
| 3 | `ex03_frame_build.c` | 组一帧（帧头/长度/CRC/帧尾） | `ota_downloader.c` `send_frame()` |
| 4 | `ex04_frame_parse.c` | 解一帧 + 6 种损坏样本 | `ota_protocol.c` `ota_parse_frame()` |
| 5 | `ex05_framer_sm.c` | 字节流滑窗状态机（含脏字节） | `ota_downloader.c` `read_frame()` |
| 6 | `ex06_dma_idle_sim.c` | **复现「HT 假帧」bug** | `main.c` `OTA_RxStart()` + `stm32f1xx_it.c` |
| 7 | `ex07_volatile.c` | `volatile` 到底防住了什么 | `main.c` `OtaRxReady` / `OtaRxLen` |
| 8 | `ex08_flash_pack.c` | 字节流 → 32 位字（补 `0xFF`）| `flash_if.c` `flash_write_bytes()` |

难度递增。**1→2→3→4→5 是纯逻辑，6→7→8 开始碰硬件语义。**

## 三、给你的两个"工具"文件（不用你写）

| 文件 | 是什么 |
|---|---|
| `crc32_table.h` | 256 项 CRC32 查表 + `crc32_fast()`。练习 3~6 直接用它，免得被 CRC 卡住 |
| `test_vectors.h` | 预先算好的**合法帧字节流**（`TV_ACK` / `TV_START` / `TV_INFO` / `TV_END`）+ DATA 帧的长度与 CRC 常量 |

`test_vectors.h` 是练习 3/4/5/6 的"标准答案输入"。它的内容是**你的程序必须能产出的字节**，
所以别改它 —— 该改的是你的代码。

两个文件都由 `_gen_tables.py` 生成，里面那份 `crc32_bitwise()` 是 Python 版的
**基准实现**，已自检通过 `CRC32("123456789") == 0xCBF43926`。

## 四、建议节奏

| 天 | 做哪几个 | 你要获得的能力 |
|---|---|---|
| Day 1 | 1、2 | 「整数和字节之间是怎么换的」，CRC 为什么能检错 |
| Day 2 | 3、4 | 帧的每个字节是谁写的、CRC 覆盖哪一段 |
| Day 3 | 5 | 怎么从一串乱字节里"认出"一帧 |
| Day 4 | 6 | **为什么当时前 3 包成功、第 4 包全挂** |
| Day 5 | 7、8 | `volatile` 与 Flash 写入粒度 |

## 五、做完这些你会得到什么

- 面试被问「你们这个协议为什么要 `& 0xFF` 和 `>> 8`」——你能当场手写出来
- 被问「为什么用 CRC32 不用 CRC16」——你知道残余错误率的量级
- 被问「DMA 怎么收变长帧」——你能画出 CNDTR 递减的过程，并说出 HT 中断为什么会捣乱
- 被问「`volatile` 不加会怎样」——你见过编译器真的把那个 `if` 优化掉

---

## 六、一个提前的提醒

`ex06` 是这 8 个里最重要的一个。它不碰任何硬件，只在 PC 上用数组模拟 DMA 的
环形缓冲 + CNDTR 递减，然后跑两条接收策略：

- 策略 A（把"DMA 半传输"当帧结束）→ 会严格复现你当时的症状
- 策略 B（只在 IDLE 时交付，长度 = `BUF_SIZE - CNDTR`）→ 22/22 全过

**先自己按策略 A 写一遍，看它怎么崩**，你才算真的理解那个 bug 是怎么来的。
