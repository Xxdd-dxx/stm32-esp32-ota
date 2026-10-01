# 双 MCU OTA 系统 — 最终实验方案（PLAN.md）

> 生成时间：2026-09-11 15:05。本文档是**唯一权威操作手册**，取代此前所有零散步骤。
> 硬件：mcuDev STM32F103ZET6 核心板 + ESP32-C3 开发板。
> 每一步都有「✅ 现象」判定，不通过就停下按表排查，不要跳步。

---

## 0. 为什么之前一直失败 — 已修复的 4 个坑（必读）

| # | 坑 | 状态 |
|---|---|---|
| 1 | **App 栈只有 1.9KB** → printf 栈溢出 HardFault → LED 全灭 | ✅ 已修复。`startup_stm32f103xe.s` 的 `Stack_Size = 0xF000`，已实测 hex 里 SP = `0x2000F060`(60KB) |
| 2 | **FlyMcu「开始编程」内部固定全片擦除** → 烧 App 必然杀掉 Bootloader | ✅ 已绕开。改用 merged.hex 一次烧双程序 / 或用 CubeProgrammer 扇区擦除 |
| 3 | **Bootloader 跳转前 `__disable_irq()`，PRIMASK=1 被带进 App** → SysTick 永不触发 → `HAL_GetTick()` 冻结 → LED 永不闪 | ✅ **本轮 15:03 已直接写入你的工程** `D:\Projects_keil\app_demo\Core\Src\main.c` 第 66 行 `__set_PRIMASK(0);`。**之前这个修复只写进了模板文件，你的 Keil 工程一直没拿到 —— 这就是"改了却没用"的真正原因** |
| 4 | Keil 后置命令 fromelf 路径写错（`.\Objects\` 应为 `.\app_demo\`）→ 生成不了 OTA 用的 .bin | ⏳ 需要你在 Keil 里改 1 处（见 2.3 节第 6 项，不影响当前测试） |

---

## 1. 硬件资源分配（按 mcuDev ZET6 原理图）

### 1.1 板载外设

| 板载资源 | 引脚 | 电平逻辑 | 本项目用途 |
|---|---|---|---|
| LED0（自定义指示灯） | **PB5** | 推挽输出 | App/Bootloader 状态灯 |
| KEY0（用户按键） | **PA0** | **按下 = 低电平**（外部上拉 3.3V） | 按住+复位 → 强制进下载模式 |
| CH340（USB 转串口） | USART1：PA9=TX, PA10=RX | — | **烧录 + printf 日志口**，电脑上 = COM5 |
| 板载 AMS1117 | 5V→3.3V | — | USB 供电即可，外接 AMS1117 收起来 |
| BOOT0 | 跳线/焊盘 | 无跳线帽时用杜邦线拉 3.3V 进 ISP | CubeProgrammer 连接兜底用 |

### 1.2 双板接线（3 根线，已接好，复查即可）

| ESP32-C3 | 方向 | STM32F103ZET6 | 说明 |
|---|---|---|---|
| GPIO4 (TX)  | → | PA3 (USART2_RX) | 固件数据 |
| GPIO5 (RX)  | ← | PA2 (USART2_TX) | ACK/状态 |
| GND | — | GND | **必须共地** |

- 两板各自 USB 供电，**不接 5V 互联**。
- ⚠️ **别碰 PA9/PA10**（板载 CH340 焊死占用）。
- 电脑端口号：STM32 板 = **COM5**；ESP32 板 = 另一个 COM（设备管理器里看，PlatformIO 会自动识别）。

---

## 2. 软件配置清单（全量，逐项核对）

### 2.1 CubeMX — App 工程（`D:\Projects_keil\app_demo`）

| 页签 | 配置项 | 值 |
|---|---|---|
| RCC | HSE | Crystal/Ceramic Resonator |
| SYS | Debug | Serial Wire |
| USART1 | Mode / Baud / 字帧 | Asynchronous / **115200** / 8N1 无流控 |
| GPIO | PA0 | 输入，Label `KEY`（Pull 任选，板上有外部上拉）|
| Clock | HSE 8M → PLL×9 | HCLK 72M，APB1÷2=36M，APB2=72M |
| Project | Toolchain | MDK-ARM |

> GPIO 里**不需要**配置 LED —— 代码用 `OTA_LED_Init()` 手动初始化 PB5。

### 2.2 CubeMX — Bootloader 工程（`D:\Projects_keil\bootloader`）

同上，另加：

| 配置项 | 值 |
|---|---|
| USART2 | Asynchronous 115200 8N1，**PA2(TX)/PA3(RX)** |
| USART2 NVIC | ☑ USART2 global interrupt |
| USART2 DMA | 添加 USART2_RX：**Circular / Byte / 优先级 High** |
| GPIO | PA0 输入，Label `KEY` |

### 2.3 Keil — Options for Target（App 工程，逐标签页）

| 标签页 | 配置项 | 值 | 状态 |
|---|---|---|---|
| **Target** | IROM1 | Start **`0x08008000`**，Size `0x3C000` | ✅ 已对 |
| | IRAM1 | Start `0x20000000`，Size **`0x10000`** | ✅ 已对 |
| | Use MicroLIB | **☑ 建议勾上**（消除 C 库半主机风险，printf 更稳）| ⏳ 建议勾 |
| **Output** | Create HEX File | **☑** | ✅ 已对 |
| **C/C++** | Optimization | -O1（App 不强制，-O1 更稳）| 建议 |
| **Linker** | Use Memory Layout from Target Dialog | **☑ 必须勾**（不勾 Target 页的内存设置全部无效）| ⏳ 核对 |
| **User** | After Build/Rebuild Run #1 | `fromelf --bin --output=app_v11.bin .\app_demo\app_demo.axf` | ⏳ **路径原来是 `.\Objects\`，改成 `.\app_demo\`** |

### 2.4 启动文件 `startup_stm32f103xe.s`

| 项 | 值 | 状态 |
|---|---|---|
| `Stack_Size` | **`EQU 0x0000F000`**（60KB）| ✅ 已改并编译验证 |

### 2.5 printf 配置（已在代码里）

- `fputc` 重定向到 `huart1`（USART1 → 板载 CH340 → COM5）。
- App main.c 第 58~66 行：`SCB->VTOR = APP_FLASH_BASE;` 之后 **`__set_PRIMASK(0);`**（本轮 15:03 已写入）。

### 2.6 串口助手（电脑端）

| 项 | 值 |
|---|---|
| 端口 | **COM5**（STM32 板的 CH340）|
| 波特率 | **115200** |
| 数据位/校验/停止位 | 8 / 无 / 1 |
| 流控 | 无 |
| ⚠️ | **打开串口助手前必须先关掉 FlyMcu / CubeProgrammer**（它们占用 COM5）|

---

## 3. 烧录工具设置

### 3.1 FlyMcu（本机已验证可用，主路线）

| 项 | 值 |
|---|---|
| 串口 | COM5，115200 |
| ✅ | **DTR 的低电平复位，RTS 高电平进 BootLoader**（一键下载核心）|
| 程序文件 | 只烧 **bootloader.hex**（首烧）或 **merged.hex**（双程序一起）|
| ⚠️ | 「开始编程」内部固定**全片擦除**——所以**永远不要用它单独烧 app_demo.hex** |

### 3.2 STM32CubeProgrammer（ST 官方，备选路线）

**实测关键发现（mcuDev 这块板子的特殊性）：**

> 这块板的 ISP 电路和 ST 官方参考设计反向 —— **只有 `RTS = Low + DTR = Low` 才能连上**。
> 默认的 `RTS = High / DTR = High`、以及 `High/Low` 等其他 4 种组合全都失败（`启动装置 : KO`）。
> 这是 mcuDev ZET6 板的硬件特性，**不是工具 bug**，记住这条经验即可，不用再反复试。

**连接参数（右上角下拉选 UART）：**

| 项 | 值 |
|---|---|
| Port / Baudrate | COM5 / **115200** |
| Parity / Data / Stop / Flow | **None** / 8 / 1 / None |
| **RTS** | **Low** ← 实测唯一能连的组合 |
| **DTR** | **Low** ← 同上 |

**为什么是 Low/Low（电路原理）：**

mcuDev 的 ISP 电路用两个 **NPN 三极管**把 CH340 的 DTR/RTS 映射到 NRST/BOOT0。
NPN 的特性是 **高电平导通**，所以电平是"反着用"的：

| CH340 线 | 静态电平 | 三极管 | 目标脚 | 实际结果 |
|---|---|---|---|---|
| DTR | **Low** | 截止 | NRST | 上拉 → **释放复位**（芯片能跑了）✓ |
| RTS | **Low** | 截止 | BOOT0 | 上拉 → **BOOT0 = 1**（进 ISP）✓ |
| DTR | High | 导通 | NRST | 拉低 → 芯片被一直按住复位 ❌ |
| RTS | High | 导通 | BOOT0 | 拉低 → BOOT0 = 0（跑 Flash，不进 ISP）❌ |

所以 **`DTR=Low + RTS=Low` = 释放复位 + BOOT0=1**，正是 ISP 需要的那一组；
`High/High` 等于"按住复位 + BOOT0=0"，芯片永远起不来 → 必然 `启动装置 : KO`。

> 设好电平后若仍连不上，按一下板子 RESET 再点「连接」（BOOT0 只在复位瞬间被采样）。

**⚠️ 烧进程序之后就再也连不上？看下面这一节（本项目的第 5 号坑）**

### 3.2.1 为什么"下载了一个程序后就连不上"（本现象 = 芯片在运行）

**症状**：空片（或刚擦除完）时 CubeProgrammer 能连；一旦烧进 Bootloader / merged.hex，之后就 `启动装置 : KO`。

**根因：芯片里有可运行的代码，上电就跑起来占用了 USART1（PA9/PA10）。**

```
空片状态:   0x08000000 = 0xFF → CPU 取不到有效入口 → 跑飞/死循环
            → 没人碰 PA9/PA10 → ISP 握手干净 → ✅ 能连

有程序状态: 0x08000000 有代码 → 上电立刻执行
            → 程序（Bootloader 或 App）初始化了 USART1 并持续 printf
            → PA9 一直被程序驱动着发送，和 CubeProgrammer 抢同一根线
            → ISP 握手收不到应答 → ❌ KO
```

> **反过来说，这个现象是一条重要诊断信息：它说明你的芯片确实成功从 Flash 启动并跑起了代码** ——
> 只是这个代码还没把 LED 点起来。所以问题不在"启动"，而在"启动之后"。

**三种解法（按可靠度排序）：**

| # | 方法 | 操作 |
|---|---|---|
| **1 推荐** | **手动 BOOT0 = 1 + 复位**（强制进系统 BootLoader，用户程序完全不运行）| 用母对母杜邦线把板上 **BOOT0 焊盘/排针** 短到 **3.3V** → 按一下 RESET → 点「连接」→ 连上后先「断开连接」→ 拔线 → 重新「连接」→ 烧录 |
| **2** | **复位窗口法** | 按住板上 RESET 键不放 → 点「连接」→ 在 CubeProgrammer 开始握手的瞬间松开 RESET |
| **3 最省事** | **改用 FlyMcu + merged.hex**（FlyMcu 的"DTR低电平复位+RTS高电平进BootLoader"会自己产生复位脉冲，每次都能把芯片拉进 ISP，不受此问题影响）| 见 3.1 节 |

**根治建议**：**烧录期间一律用方法 1 或 3**。因为只要 Flash 里有能跑的程序，CubeProgrammer 的纯静态电平就永远抢不过正在运行的 App。

> 另外这也解释了为什么"擦除芯片后就能连" —— 擦除后 Flash 变回 0xFF，芯片跑飞，USART1 空出来了。

**烧录设置（左侧 Erasing & Programming）：**

| 场景 | 文件 | 擦除模式 |
|---|---|---|
| 阶段 A 单独烧 App（0x08000000）| app_demo.hex | ☑ 全芯片擦除 |
| 首烧 Bootloader | bootloader.hex | ☑ 全芯片擦除 |
| **补烧 App（Bootloader 已在）** | app_demo.hex | **☐ 取消全擦**（自动只擦 0x08008000 起的扇区，不杀 Bootloader）|
| 一次烧双程序 | merged.hex | ☑ 全芯片擦除 |

其余：开始地址**留空**（hex 自带地址），☑ 校验编程。

**连不上时（已经按上面设好 Low/Low 仍然 KO）：**

1. 杜邦线兜底：把板上 **BOOT0 焊盘/排针** 用一根母对母杜邦线短到 **3.3V** → 按一下 RESET → 点 Connect
2. 连上后先「断开连接」→ 拔掉杜邦线 → 再「连接」（此时可以正常烧录了）

---

## 4. 实验步骤（严格按顺序）

### 步骤 1：让修复真正进编译（2 分钟）

1. 回到 Keil。若弹出「main.c 已在编辑器外被修改」→ 点 **『是』重新加载**。
   （若 main.c 在编辑器里有未保存的旧修改，先放弃保存，以磁盘上的新版为准 —— 里面有第 66 行 `__set_PRIMASK(0);`）
2. Options for Target → Target → ☑ **Use MicroLIB**。
3. Options for Target → User → 把 Run #1 改成 `fromelf --bin --output=app_v11.bin .\app_demo\app_demo.axf`。
4. **Rebuild**（不是 Build）→ 输出 `0 Error(s)`。

### 步骤 2：重新合并 + 烧录（3 分钟）

cmd 里执行（一条条粘贴）：

```cmd
copy /Y "D:\Projects_keil\app_demo\MDK-ARM\app_demo\app_demo.hex" "D:\workBuddyFile\2026-09-10-14-17-26\ota_project\tools\"
cd /d D:\workBuddyFile\2026-09-10-14-17-26\ota_project\tools
"C:\Users\JD\.workbuddy\binaries\python\versions\3.13.12\python.exe" merge_hex.py bootloader.hex app_demo.hex -o merged.hex
```

然后：
1. **关闭 FlyMcu 之外所有占 COM5 的程序** → 打开 FlyMcu → 选 `merged.hex` → 开始编程。
2. 烧完**关闭 FlyMcu** → 打开串口助手（COM5 / 115200）→ 按一下 STM32 的 RESET。

**✅ 成功判据（三条全中）：**

```
① LED0(PB5) 以 500ms 周期闪烁
② 串口打印 banner：
   [APP]  STM32F103 OTA Demo
   [APP]  version : 1.0.0
   [APP]  VTOR    : 0x08008000
③ 之后每 500ms 一条 [APP] alive Ns (v1.0.0)
```

**成功了 → 直接跳到步骤 4（OTA 演示准备）。**

### 步骤 3：二分法定位（仅当步骤 2 失败才做）

目的：把 **App 本身**和 **Bootloader 跳转**两个变量拆开测。

**3.1 把 App 单独跑在 0x08000000（2 处改动）：**

1. Keil → Options → Target → **IROM1 Start 由 `0x08008000` 改为 `0x08000000`**（Size 不动）。
2. main.c 顶部 → **`#define APP_FLASH_BASE 0x08008000UL` 改为 `0x08000000UL`**。
3. **Rebuild** → FlyMcu 烧 **app_demo.hex**（此时全片擦除无所谓，本来就不带 Bootloader）→ 复位。

**✅ 现象：LED 闪 + 串口 banner + alive**（和步骤 2 期望一样，只是不经跳转）

| 结果 | 结论 | 下一步 |
|---|---|---|
| 步骤 3 ✅ 但步骤 2 ❌ | App 健康，**问题在 Bootloader 跳转/合并环节** | 把 merged.hex 烧录后的串口现象告诉我（重点：有没有 banner）|
| 步骤 3 ❌（LED 也不闪）| **App 自身问题**（时钟/串口/printf）| 把串口看到的原样发我（乱码也要发，乱码=波特率/时钟问题）|
| 步骤 3 ❌ 且串口一个字节都没有 | App 早期就挂 | 用 LED 探针（见 STEPS.md 附录 B-1 末尾）|

**3.2 测完恢复（2 处改回）：** IROM1 → `0x08008000`；`APP_FLASH_BASE` → `0x08008000UL`；Rebuild。

### 步骤 4：OTA 升级链路（ESP32 + 本地服务器）

前提：步骤 2 的三条判据全过。

1. **做 v1.1 固件**：main.c 里 `APP_VERSION` 改 `"1.1.0"`、`BLINK_PERIOD_MS` 改 `150` → Rebuild
   （Rebuild 后 Keil 自动执行 fromelf 生成 `MDK-ARM\app_v11.bin`，这就是 OTA 要发的固件文件）。
2. **起本地 HTTP 服务器**：把 `app_v11.bin` 放到一个目录，cmd 里 `cd` 过去 → `python -m http.server 8000`。
3. **ESP32 代码**（`ota_project\esp32_downloader\src\main.cpp`）改两处：
   - WiFi SSID/密码（和电脑同一网络）
   - 固件 URL：`http://<你的电脑IP>:8000/app_v11.bin`
4. VSCode + PlatformIO → Upload 到 ESP32 → 打开 Serial Monitor。
5. **触发升级**：按住 STM32 的 KEY0 → 按一下 RESET → 松开 KEY0（LED 变快闪=下载模式）→ 按一下 ESP32 的 RESET（或 EN）。
6. **✅ 成功判据**：ESP32 日志显示下载进度条 → 传输完成 → STM32 自动复位 → LED 从 500ms 闪变成 150ms 快闪 → 串口打印 `version : 1.1.0`。

---

## 5. 现象速查总表

| 现象 | 含义 | 处理 |
|---|---|---|
| 复位后 LED 全灭 + 串口无输出 | App 没跑起来 | 走步骤 3 二分法 |
| 串口有 banner，LED 不闪 | `__set_PRIMASK(0)` 没进编译 | 确认 main.c 第 66 行存在 → Rebuild（不是 Build）|
| LED 200ms 慢闪不停 | Bootloader 进了下载模式，没找到有效 App | App 没烧上 → 检查 merged.hex 是否重新生成（看文件时间）|
| 串口乱码 | 波特率/时钟/共地问题 | 115200；查 GND；换 USB 线 |
| banner 里 VTOR 不是 0x08008000 | 烧的是旧 hex | 重新 Rebuild + copy + merge |
| OTA 中 CRC 失败 | 杜邦线松动 | 重插、缩短线长 |
| OTA 后仍打 v1.0.0 | 升级未完成 | 看 ESP32 日志的断点位置 |
| **空片能连、烧了程序后 CubeProgrammer 一律 KO** | **芯片里的程序在跑，占用 USART1（PA9/PA10）与 ISP 抢线** | **手动把 BOOT0 短到 3.3V + 复位再连**（见 3.2.1）；或直接用 FlyMcu + merged.hex |

---

## 6. 当前文件位置备忘

| 文件 | 路径 |
|---|---|
| App Keil 工程 | `D:\Projects_keil\app_demo\` |
| Bootloader Keil 工程 | `D:\Projects_keil\bootloader\` |
| App hex 原件 | `D:\Projects_keil\app_demo\MDK-ARM\app_demo\app_demo.hex` |
| 合并工具 + hex | `D:\workBuddyFile\2026-09-10-14-17-26\ota_project\tools\` |
| ESP32 工程 | `D:\workBuddyFile\2026-09-10-14-17-26\ota_project\esp32_downloader\` |
| 详细排障手册 | 同目录 `STEPS.md`（含全部历史坑的原理分析）|
