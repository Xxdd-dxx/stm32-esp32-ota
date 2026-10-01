# 双 MCU OTA 系统实验步骤（按顺序勾选）

> 每一步都有“✅ 现象”作为完成判据，达不到就别往下走。  
> 出错对照 `README.md` 的“常见问题排查”表。

---

## 阶段 0：准备工作

### 0.1 软件清单

- [ ] **STM32CubeMX**（推荐 6.10+，去 ST 官网下载安装）
- [ ] **Keil MDK-ARM**（5.38+；AC5 或 AC6 编译器都行）+ STM32F1 Device Pack（Keil 装完会提示装 Pack Installer → 搜 STM32F1 Series）
- [ ] **STM32CubeProgrammer**（ST 官网免费，**本项目主力烧录工具**）
- [ ] **FlyMcu**（可选备用，搜"FlyMcu mcuisp"；它的「开始编程」会强制全片擦除，只能烧合并 hex）
- [ ] **CH340 驱动**（板子插上 USB 后 Windows 会自动装；不行去 wch.cn 下驱动）
- [ ] **VSCode + ESP-IDF 插件**（你机器上已装好：插件 `espressif.esp-idf-extension` + ESP-IDF v5.4.4，路径 `D:\Espressif`）
  > 不需要 PlatformIO —— 工程已改成 ESP-IDF 结构，见阶段 5 开头的说明。
- [ ] **串口助手**（推荐 MobaXterm 或 SSCOM，能看十六进制最好）
- [ ] **Python 3**（电脑上 `python --version` 能跑）

### 0.2 硬件清单

- [ ] STM32F103ZET6 开发板（含板载 CH340，型号 mcuDev / 正点原子 等）× 1
- [ ] ESP32-C3 开发板 × 1（任何 USB-C3 DevKit 都行）
- [ ] 杜邦线 3~4 根（母对母）
- [ ] 两根 USB 数据线（同时给两板供电 + 烧录）
- [ ] 可选：万用表（量 GND 通断用）

### 0.3 工程文件准备

- [x] 把 `ota_project/` 文件夹整个拷到方便的位置（路径别带中文和空格，比如 `D:\ota_project\`）

---

## 阶段 1：硬件搭建

### 1.1 接线

**断电操作。** 用 3 根杜邦线连接：

| ESP32-C3    | STM32F103       | 杜邦线颜色建议 |
| ----------- | --------------- | ------- |
| GPIO4 (TX)  | PA3 (USART2_RX) | 橙       |
| GPIO5 (RX)  | PA2 (USART2_TX) | 蓝       |
| GND         | GND             | 黑       |

> ⚠️ STM32 的 **PA9/PA10 不要碰**，板载 CH340 焊死在上面。  
> 引脚位置：找板上“所有 IO 外引接口”双排针，标着 PA2/PA3/GND 的脚。

### 1.2 通电验证

- [ ] STM32 板 USB 插电脑，确认“电源灯亮”
- [ ] 设备管理器 → 看到 CH340 的 COM 口（记下号，假设 COM3）
- [ ] ESP32-C3 板 USB 插电脑，确认电源灯亮，设备管理器也能识别 COM 口
- [ ] 万用表蜂鸣档测 ESP32 GND 和 STM32 GND 通不通 → **必须通**

✅ 现象：两板电源灯亮，设备管理器看到 2 个 CH340（或 1 个 CH340 + 1 个 USB-Serial-JTAG）。

---

## 阶段 2：开发 STM32 Bootloader

### 2.1 CubeMX 配置（照 `stm32_bootloader/CUBE_MX_CONFIG.md` 点）

- [ ] CubeMX → New Project → 搜 `STM32F103ZETx` → 选中 → Start Project
- [ ] **RCC** → HSE = Crystal/Ceramic Resonator
- [ ] **SYS** → Debug = Serial Wire
- [ ] **USART2** → Mode = Asynchronous；Baud 115200；NVIC 勾 USART2 global interrupt；DMA Settings 加 USART2_RX（Circular、Byte、High）
- [ ] **GPIO**：**PE4** → GPIO_Input，Pull-up，User Label `KEY`（⚠️ 不是 PA0，详见 2.7）
- [ ] **Clock Configuration**：HSE 8M → PLL × 9 → HCLK 72MHz，APB1 /2 = 36MHz
- [ ] **Project Manager**：Name `bootloader`，Toolchain MDK-ARM，勾 "Generate peripheral initialization as pair of .c/.h"，Generate Code

### 2.2 替换代码

- [ ] 用 `ota_project/stm32_bootloader/Core/Src/main.c` **整体覆盖**工程里的 `Core/Src/main.c`
- [ ] 拷 `ota_config.h`、`ota_protocol.h`、`flash_if.h` → `Core/Inc/`
- [ ] 拷 `ota_protocol.c`、`flash_if.c` → `Core/Src/`
- [ ] Keil 工程：右键 `Application/User/Core` 组 → Add Existing Files → 把两个新 .c 加进去

### 2.3 Keil 设置

- [ ] Options → C/C++ → Optimization：选 **-O1**（不能 -O0，会超 24KB）
- [ ] Options → Target → IROM1：Start `0x8000000`，Size `0x6000`（默认就是）
- [ ] Options → Output → 勾 **Create HEX File**

### 2.4 编译

- [ ] Build（F7）→ **0 Error, 0 Warning**
  - 报 `undefined reference to MX_DMA_Init` → 检查 dma.c 是否生成、是否加入工程
  - 报 `SystemClock_Config implicit declaration` → 已修，若仍报重看 main.c 第 63 行有没有 `void SystemClock_Config(void);`

### 2.5 烧录（FlyMcu + 板载 CH340）

> ⚠️ **先搞懂 FlyMcu 的一个隐式行为（本项目的头号坑）**
>
> 点「开始编程」时，FlyMcu 内部固定执行 **全片擦除 → 写入 → 校验** 三段式。  
> 也就是说：**即使你不点「清除芯片(Z)」，它也会把整片 Flash 擦光再写**。
>
> 后果：用 FlyMcu 直接烧 `app_demo.hex`，会把 Bootloader（0x08000000）一起擦掉  
> → 芯片复位后取不到入口 → **LED 全灭、串口无输出**（这就是最常见的"烧完 App 板子死了"）。
>
> ✅ **正确做法：最后用「合并 hex」一次烧入两个程序，见 3.4 节。**

- [ ] 打开 FlyMcu
- [ ] 选芯片：STM32F1（高容量）
- [ ] 串口：选 STM32 板的 COM 口，波特率 **115200**
- [ ] ✅ 勾 **"DTR 的低电平复位，RTS 高电平进 BootLoader"**
- [ ] 程序文件：选 `bootloader/MDK-ARM/bootloader/Objects/bootloader.hex`
- [ ] 点 **"开始编程"** → 自动全擦 + 写入
- [ ] 日志最后一行应为 `共写入 XXXX 字节，进度100%`（你的 Bootloader 约 7KB）

> 一键下载失败兜底：板上 BOOT0 跳线切 `1` → 按复位 → FlyMcu 点开始编程 → 烧完 BOOT0 切回 `0` → 再按复位

### 2.6 验证 Bootloader 现象

- [ ] 复位 STM32
- [ ] 观察 `自定义LED指示灯` 区的 **LED0**：**慢闪（约 200ms 一次）**
- [ ] 因为没有 App，进不了跳转 → 一直停在下载模式

✅ 现象：LED0 慢闪，说明 Bootloader 在跑且没找到有效 App（正常）。

> **如果 LED0 不闪**：默认代码用 PB5。不亮说明你板子的 LED0 在别的脚。看下面「LED 引脚排查表」，改 `ota_config.h` 里 `OTA_LED_PORT` / `OTA_LED_PIN`，重新编译烧录即可。

| 改 ota_config.h 这两行                                            | 含义        |
| ------------------------------------------------------------- | --------- |
| `#define OTA_LED_PORT GPIOB` `#define OTA_LED_PIN GPIO_PIN_0` | 试 PB0     |
| `#define OTA_LED_PORT GPIOB` `#define OTA_LED_PIN GPIO_PIN_5` | 试 PB5（默认） |
| `#define OTA_LED_PORT GPIOE` `#define OTA_LED_PIN GPIO_PIN_5` | 试 PE5     |
| `#define OTA_LED_PORT GPIOE` `#define OTA_LED_PIN GPIO_PIN_6` | 试 PE6     |

App 工程的 `main.c` 顶部同样有这两个 #define，保持一致。

### 2.7 ⚠️ KEY0 实际引脚 = PE4（不是 PA0！已按原理图实测确认）

**mcuDev ZET6 原理图**：

```
KEY0 ──┬── PE4          KEY1 ──┬── PE3
       │                        │
      [按键]                   [按键]
       │                        │
      GND                      GND
```

所以：
- `KEY_Pin = GPIO_PIN_4`、`KEY_GPIO_Port = GPIOE`
- **必须开内部上拉**（`GPIO_PULLUP`）—— 按键另一端只接 GND，未按下时引脚浮空，读数随机
- 按下 = **低电平** → `KEY_ACTIVE_LEVEL = GPIO_PIN_RESET` ✓

> 教训：CubeMX 默认把 PA0-WKUP 配成 KEY，但**原理图上 KEY0 其实接 PE4**。
> 这是本项目"按键一直无反应"的真正根因。改引脚时三处都要动：
> ① `main.h` 的 `KEY_Pin` / `KEY_GPIO_Port`
> ② `gpio.c` 开 `__HAL_RCC_GPIOE_CLK_ENABLE()` + `Pull = GPIO_PULLUP`
> ③ `ota_config.h` 的 `KEY_ACTIVE_LEVEL`

### 2.8 进下载模式的操作（两种都行）

**方式 A（推荐 —— Bootloader 已加"上电 3 秒窗口"）**：

1. 按一下 STM32 的 **RST**
2. 松手后 **3 秒内**（此时 LED **超快闪**，50ms 一次翻转）
3. **按一下 KEY0（PE4）** → LED 立刻变 **200ms 慢闪** = 进下载模式 ✓

**方式 B（按住法）**：按住 KEY0 不松 → 按一下 RESET → 松开 KEY0 → LED 慢闪

| LED 行为 | 含义 |
|---|---|
| 50ms 超快闪（持续 3 秒后停） | 上电窗口打开中，等你按 KEY0 |
| 200ms 慢闪（一直） | **已在下载模式**，等 ESP32 发帧 |
| 50ms 快闪（一直） | OTA 传输中（`g_ota.active=1`）|
| App 的节奏（150/500ms） | 没进下载模式，跳 App 了 |

---

## 阶段 3：开发 STM32 测试 App v1.0

### 3.1 CubeMX 配置（照 `stm32_app/CUBE_MX_CONFIG.md`）

- [ ] CubeMX 新工程 → `STM32F103ZETx`
- [ ] RCC → HSE Crystal
- [ ] SYS → Debug = Serial Wire
- [ ] **USART1** → Asynchronous 115200 8N1（板载 CH340 走这条，**不接外线**）
- [ ] GPIO：**PE4** → Input + Pull-up，Label `KEY`（App 里预留，本版本暂不使用；⚠️ 不是 PA0）
  - **LED0 引脚不用在 CubeMX 里配！** 代码启动时通过 `OTA_LED_Init()` 自动初始化，默认 PB5，与 Bootloader 一致
- [ ] 时钟树：72MHz（同 Bootloader）
- [ ] 工程名 `app_demo`，Generate

### 3.2 替换代码

- [ ] 用 `ota_project/stm32_app/Core/Src/main.c` 整体覆盖工程的 `Core/Src/main.c`

### 3.3 Keil 关键设置（决定成败）

- [ ] Options → Target → **IROM1**：Start `0x08008000`，Size `0x3C000`
- [ ] Options → Output → 勾 **Create HEX File**
- [ ] 检查 `APP_VERSION` 是 `"1.0.0"`，`BLINK_PERIOD_MS` 是 `500`


### 3.4 烧录 App v1.0（关键：这一烧绝对不能全擦）

- [ ] 确认 app_demo 工程 IROM1 = `0x08008000` / `0x3C000`，且已点过 **Rebuild**（不是 Build）
- [ ] 确认 `Objects/app_demo.hex` 的**修改时间是刚刚**（时间旧 = 烧的是老代码）
- [ ] CubeProgrammer → UART → COM5 → **连接**
  > ⚠️ **连接前先把 UART 配置里的 `RTS` 和 `DTR` 都设为 `High`**。  
  > 默认 `DTR=Low` 会通过一键下载电路把 STM32 的 NRST 一直拉低，芯片处于复位态，  
  > 必然报 `错误:启动装置 : KO`。改 High 后芯片才会被释放并进入系统 BootLoader。
- [ ] 「擦除/编程」标签 → 文件路径选 `app_demo.hex`（**别选成 bootloader.hex**）
- [ ] 开始地址：**留空**
- [ ] ⚠️ 自动模式里 **取消勾选「全芯片擦除」** ← **这一步的全部成败在此**
  - 取消后，CubeProgrammer 只会擦写 App 覆盖到的扇区（**索引 16 起** = 0x08008000 起）
  - Bootloader 所在的扇区 0~15 完全不会被动
- [ ] ✅ 勾 **校验编程**
- [ ] 点 **开始编程** → 日志 `Download verified successfully`
- [ ] 点「断开连接」→ 按 RESET → LED0 应 **500ms 闪** + 串口打印 `[APP] version 1.0.0`

> **LED0 判据对照表**（复位后看 PB5 那个自定义指示灯）：
>
> | LED0 现象  | 含义                                   |
> | -------- | ------------------------------------ |
> | 200ms 慢闪 | 只有 Bootloader 在跑（App 区空 / 无效 / 被擦）   |
> | 500ms 闪  | Bootloader 跳转成功，App 在运行 ✓            |
> | 完全不亮     | 0x08000000 是空的，芯片启动失败（Bootloader 被擦） |

- [ ] **之后再改 App，统一走 OTA（ESP32 下载），不要再手动重烧**

> **App 区残留旧数据导致异常时**：切到 CubeProgrammer 的 **「擦除扇区」** 标签  
> → 只勾选 **索引 16 及之后**（0x08008000 起）的扇区 → 点「擦除选定扇区」  
> → 再回「擦除/编程」烧 App。**千万别勾索引 0（=Bootloader 起点）**。

> **FlyMcu 备用方案（只有在用不了 CubeProgrammer 时才用）**  
> FlyMcu 的「开始编程」内部固定"全片擦除 → 写入 → 校验"，界面里关不掉，  
> 所以必须把 Bootloader + App **合并成一个 hex 一次烧**：
>
> ```cmd
> cd ota_project\tools
> python merge_hex.py bootloader.hex app_demo.hex -o merged.hex
> ```
>
> 然后 FlyMcu 选 `merged.hex` → 开始编程。  
> （合并前务必确认 app_demo 的 IROM1 = 0x08008000，否则两段地址会互相覆盖）

### 3.5 验证跳转

- [ ] 复位 STM32
- [ ] LED0：**每 500ms 闪烁一次**
- [ ] 关掉 FlyMcu（释放 COM 口）→ 打开串口助手，选同一个 COM，115200 8N1
- [ ] 复位 STM32，串口应打印：
  ```
  ========================================
  [APP]  STM32F103 OTA Demo
  [APP]  version : 1.0.0
  [APP]  base    : 0x08008000
  [APP]  VTOR    : 0x08008000
  ...
  [APP] alive 0s (v1.0.0)
  [APP] alive 1s (v1.0.0)
  ```

✅ 现象：LED 慢闪 + 串口打印 v1.0.0 + 每秒心跳 → Bootloader→App 跳转链路通！

---

## 阶段 4：制作 v1.1 升级固件 + 启动服务器

### 4.1 改 App 工程（两个宏）

打开 `D:\Projects_keil\app_demo\Core\Src\main.c`，改第 24、26 行：

- [ ] `#define APP_VERSION      "1.1.0"`
- [ ] `#define BLINK_PERIOD_MS  150`（灯闪频率变快，肉眼即可看出升级生效）

> 其余代码**一个字都不要动**（`APP_FLASH_BASE` 保持 `0x08008000UL`）。

### 4.2 Rebuild 生成 bin

`fromelf` 后置命令**已经在你的 Keil 工程里配好了**（Options → User → After Build/Rebuild → Run #1）：

```
fromelf --bin --output=app_v11.bin .\app_demo\app_demo.axf
```

- [ ] Keil 点 **Rebuild** → 输出 `0 Error(s)`
- [ ] 确认生成 `D:\Projects_keil\app_demo\MDK-ARM\app_v11.bin`，且**修改时间是刚才**

> `app_v11.bin` 是命令里写死的输出名，所以每次做升级固件都叫这个名（正好符合本项目）。

### 4.3 启动 HTTP 服务器

- [ ] 把 `app_v11.bin` 拷到 `D:\workBuddyFile\2026-09-10-14-17-26\ota_project\tools\`
- [ ] cmd 执行：
  ```cmd
  cd /d D:\workBuddyFile\2026-09-10-14-17-26\ota_project\tools
  python -m http.server 8000
  ```
- [ ] 浏览器访问 `http://localhost:8000/app_v11.bin` → 应直接触发下载（服务器 OK）
- [ ] **保持这个 cmd 窗口不要关**（关了 ESP32 就下载不到）

### 4.4 确认电脑 IP

- [ ] cmd 跑 `ipconfig`，看「无线局域网适配器 WLAN」的 IPv4 地址
- [ ] **你当前机器的 IP 是 `192.168.1.100`**（本机实测），已预填进 ESP32 代码
- [ ] 手机浏览器访问 `http://192.168.1.100:8000/app_v11.bin`（手机连同一 WiFi）→ 能下载说明网段可达

> ⚠️ 三个前提必须满足：
> 1. ESP32 与电脑在**同一个 WiFi**；
> 2. 该 WiFi **不需要网页认证**。校园网/酒店网这类要跳认证页的网络 ESP32 连不上
>    → 改用**手机热点**，电脑也连到该热点，然后重新 `ipconfig` 查 IP 并更新代码里的 URL；
> 3. Windows 防火墙：Python 服务器首次运行弹窗时选**「允许访问」**，否则 ESP32 连不上 8000 端口。

---

## 阶段 5：烧录 ESP32-C3 下载器

> **⚠️ 重要变更（2026-09-12）**：本工程已从 PlatformIO 改成 **ESP-IDF** 结构。
>
> 原因：你 VSCode 里装的是 **ESP-IDF 插件**（不是 PlatformIO），点 Build 时
> 因为工程根目录缺 `CMakeLists.txt`，报了
> `CMake Error: The source directory ... does not appear to contain CMakeLists.txt`。
> 现已补齐标准 ESP-IDF 工程结构，并改用 ESP-IDF 原生 API 重写代码。
>
> 详细说明 + 常见报错对照见 `esp32_downloader/README_ESPIDF.md`。

### 5.1 打开工程

- [ ] VSCode → 打开文件夹 `D:\workBuddyFile\2026-09-10-14-17-26\ota_project\esp32_downloader`
  （要选 `esp32_downloader` 这一层，不要选上一层的 `ota_project`）
- [ ] 确认底部状态栏芯片显示 **ESP32-C3**、串口显示 **COM11**
  - 芯片不对：`Ctrl+Shift+P` → `ESP-IDF: Set Espressif device target` → 选 `esp32c3`
  - 串口不对：点状态栏串口图标重选

### 5.2 改配置（必改）

打开 `main/ota_downloader.c` 顶部「用户配置区」：

```c
#define WIFI_SSID     "YOUR_WIFI_SSID"        /* ← 改成电脑所连的 WiFi 名称 */
#define WIFI_PASS     "YOUR_WIFI_PASSWORD"        /* ← 改成对应密码            */
#define FIRMWARE_URL  "http://192.168.1.100:8000/app_v11.bin"  /* ← 电脑 IP */
```

- [ ] 填 `WIFI_SSID` / `WIFI_PASS`
- [ ] `FIRMWARE_URL` 的 IP 若与阶段 4.4 查到的不同，一起改掉

### 5.3 ⚠️ UART 引脚冲突 → 已用 GPIO4/5 避开

ESP32-C3 的 **UART0（串口控制台）默认占用 GPIO20(TX)/GPIO21(RX)**。
本工程把 STM32 通信挪到 **GPIO4/5**，UART0 完全留给控制台日志（你熟悉的 COM11），板子上**任意一个 USB 口都同时负责烧录 + 看日志**。

代码里已经写好：
- `main/ota_downloader.c`：`PIN_STM32_TX=4`, `PIN_STM32_RX=5`
- `sdkconfig.defaults`：`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`（控制台走 UART0）

### 5.4 编译烧录

状态栏从左到右依次点：

- [ ] 🔨 **Build** —— 首次编译要几分钟（要编译整个 ESP-IDF）
- [ ] ⚡ **Flash** —— 烧录
- [ ] 📺 **Monitor** —— 看日志，**退出按 `Ctrl + ]`**（不是 Ctrl+C）

✅ 现象（ESP32 侧全部就绪）：

```
I (xxx) OTA-DL: === ESP32-C3 OTA Downloader (ESP-IDF) ===
I (xxx) OTA-DL: Connecting WiFi: xxxxx ...
I (xxx) OTA-DL: WiFi OK, IP: 10.2.x.x
I (xxx) OTA-DL: --------------------------------------------
I (xxx) OTA-DL: Make sure STM32 is in BOOTLOADER mode:
I (xxx) OTA-DL:   press RST, then press KEY0(PE4) within 3s
I (xxx) OTA-DL:   (LED0 blinking slowly = ready)
I (xxx) OTA-DL: --------------------------------------------
```

看到最后那个分隔线，说明 WiFi 通了；接着 8 秒倒计时结束后就会去下载固件。

> Monitor 占用的是 **ESP32 的串口**，与 STM32 的 COM5 是两个独立端口，互不影响。
> 前提是电脑上要能看到 ESP32 的 COM 口（插对 USB 口后设备管理器里会出现）。

---

## 阶段 6：联调演示（核心环节）

### 6.0 三个窗口同时开着（强烈建议）

| 窗口 | 怎么开 |
|---|---|
| ESP32 日志 | VSCode → ESP-IDF **Monitor**（📺 图标，退出按 `Ctrl + ]`）|
| STM32 日志 | 串口助手 → **COM5 / 115200 / 8N1** |
| HTTP 服务器 | 阶段 4.3 那个 cmd（保持运行，别关）|

### 6.1 让 STM32 进入下载模式

**方式 A（推荐 —— 3 秒窗口）**：

- [ ] **按一下 STM32 的 RESET**
- [ ] 松手后 3 秒内，LED0 会 **50ms 超快闪**（提示"窗口打开中"）
- [ ] 这 3 秒内 **按一下 KEY0（PE4）**
- [ ] LED0 立刻变 **200ms 慢闪** = 进下载模式 ✓

**方式 B（按住法）**：

- [ ] **按住 STM32 板上的 KEY0（PE4）不松开**
- [ ] **按一下 RESET** → **先松 RESET，再松 KEY0**
- [ ] LED0 变 **200ms 慢闪** = 进下载模式 ✓

> ⚠️ KEY0 在 **PE4**，不是 PA0。若按了没反应，检查 `main.h` 的
> `KEY_Pin=GPIO_PIN_4 / KEY_GPIO_Port=GPIOE` 和 `gpio.c` 里的 `GPIO_PULLUP`。

> Bootloader 的下载模式是**死循环等待**，不会超时退出 —— 所以先做这一步最从容。

### 6.2 触发 OTA 升级

- [ ] 按一下 ESP32 的 **RST / EN** 键（让它重新执行 setup）

✅ ESP32 日志应依次出现：

```
I (xxx) OTA-DL: Downloading: http://192.168.1.100:8000/app_v11.bin
I (xxx) OTA-DL: HTTP status=200, size=XXXX
I (xxx) OTA-DL: OTA session started (XX packets, XXXX bytes)
I (xxx) OTA-DL: Progress: .../... (10%)
I (xxx) OTA-DL: Progress: .../... (100%)
I (xxx) OTA-DL: All packets sent. CRC32 = 0xXXXXXXXX
I (xxx) OTA-DL: ========================================
I (xxx) OTA-DL:   OTA SUCCESS! STM32 rebooting to new app
I (xxx) OTA-DL: ========================================
```

同时 **STM32 的 LED0 会从慢闪变成快闪（50ms 心跳 = 正在收包）**。

### 6.3 验证升级结果

- [ ] STM32 自动复位（Bootloader 校验通过后 `NVIC_SystemReset()`）
- [ ] LED0 变成 **150ms 快闪**（v1.1 的新节奏，明显比 500ms 快）
- [ ] STM32 串口打印：

```
[APP]  version : 1.1.0
[APP]  VTOR    : 0x08008000
[APP] alive 0s (v1.1.0)
```

✅✅✅ **版本号由 1.0.0 变 1.1.0、且灯闪明显变快 = OTA 完整闭环成功！**

### 6.4 失败定位（最常踩的 4 种）

| ESP32 日志停在 | 含义 | 处理 |
|---|---|---|
| `HTTP GET failed: -1` / `404` | 服务器没开 / URL 错 | 看 4.3 的 cmd 还在跑吗；浏览器先能下载 |
| `CMD_START not acked` | STM32 没进下载模式，或 TX/RX 接反 | 确认 LED0 是 200ms 慢闪；把 GPIO20/21 两根线对调再试 |
| 卡在 Progress 中途不动 | 杜邦线接触不良 | 重插、缩短线长、波特率降到 57600 重试（改 ESP32 与 Bootloader 两端一致）|
| 反复 `[RETRY]` | 丢包 | 检查两板是否共地；线尽量短 |

---

## 阶段 7：演示录制与简历落地

### 7.1 录制演示视频（3~5 分钟）

- [ ] 屏幕分屏：左 ESP32 串口日志 / 右 STM32 串口日志
- [ ] 流程：
  1. 复位 STM32，展示 v1.0.0 现象（30s）
  2. 按 KEY+RESET 进下载模式，展示 LED 慢闪（20s）
  3. 复位 ESP32，跑完 OTA，展示进度条和 SUCCESS（30~60s）
  4. 复位 STM32，展示 v1.1.0 + 灯变快（30s）
- [ ] 上传 B 站 / YouTube（不公开也行），简历里放链接

### 7.2 简历项目描述

照 README 里的“简历描述模板”改写，配演示视频链接。

### 7.3（可选）阶段扩展

- [ ] 加 IWDG 看门狗 + 5 秒心跳回滚机制
- [ ] 加断点续传（包序号持久化到 Config 区）
- [ ] 把 HTTP 换 MQTT，云端推送升级
- [ ] 录第二版演示视频，体现"容灾设计"

---

## 附录 A：CubeProgrammer 连不上芯片的三级解决方案

> 报错特征：`错误:启动装置 : KO。请验证启动模式配置并检查串行端口配置。重置设备，然后再试一次。`

### 为什么连不上

mcuDev 这类板子的「一键下载」电路把 CH340 的 **DTR / RTS 两根控制线**通过
**NPN 三极管网络**（高电平导通）接到了 STM32 的 **NRST（复位）** 和 **BOOT0（启动选择）**：

| CH340 线 | 静态电平 | 三极管 | 目标脚 | 实际结果 |
|---|---|---|---|---|
| DTR | **Low** | 截止 | NRST | 上拉 → **释放复位**（芯片能跑）✓ |
| RTS | **Low** | 截止 | BOOT0 | 上拉 → **BOOT0 = 1**（进 ISP）✓ |
| DTR | High | 导通 | NRST | 拉低 → 芯片**一直被按住复位** ❌ |
| RTS | High | 导通 | BOOT0 | 拉低 → BOOT0 = 0（跑 Flash）❌ |

**关键结论：这块板子（实测）只有 `RTS = Low + DTR = Low` 才能连上。**
其他组合（如默认的 `High/High`）等于「按住复位 + BOOT0=0」，芯片永远起不来 → `启动装置 : KO`。

> 设好电平后若仍连不上，按一下板子 RESET 再点「连接」（BOOT0 只在复位瞬间被采样）。

### 方案 0（最常见！先看这个）：烧进程序之后就再也连不上

**症状**：空片 / 刚擦除完时能连；一旦烧进 Bootloader 或 merged.hex，之后一律 `KO`。

**根因**：芯片里有能跑的程序，**上电就跑起来占用了 USART1（PA9/PA10）**，和 ISP 握手抢同一根线。

```
空片:   0x08000000 = 0xFF → CPU 跑飞 → 没人碰 PA9/PA10 → ISP 握手干净 → ✅ 能连
有程序: 0x08000000 有代码 → 上电立即执行 → 初始化 USART1 并持续 printf
        → PA9 一直被程序驱动 → 与 CubeProgrammer 抢线 → ❌ KO
```

> **这也是个好消息**：它说明你的芯片确实成功从 Flash 启动并跑起了代码，只是还没把 LED 点起来 ——
> 问题在「启动之后」，不在「启动」。

**解法（按可靠度）：**

| # | 方法 | 操作 |
|---|---|---|
| **1** | **手动 BOOT0 = 1 + 复位**（用户程序完全不运行，最可靠）| 用母对母杜邦线把 **BOOT0 焊盘/排针** 短到 **3.3V** → 按 RESET → 点「连接」→ 连上后先「断开连接」→ 拔线 → 重新「连接」→ 烧录 |
| **2** | **复位窗口法** | 按住板上 RESET 不放 → 点「连接」→ 握手瞬间松开 RESET |
| **3** | **改用 FlyMcu + merged.hex** | FlyMcu 自带复位脉冲时序，不受此影响（见方案 3）|

### 方案 1（先试这个，30 秒）：把 RTS 和 DTR 都设成 Low

| 界面项 | 值 |
|---|---|
| 端口 / 波特率 | COM5 / 115200 |
| 奇偶校验 | **None**（不要 Even）|
| 数据位 / 停止位 / 流量控制 | 8 / 1.0 / Off |
| **RTS** | **Low** ← 实测关键 |
| **DTR** | **Low** ← 实测关键 |
| 延迟脉冲 | 空 |

改完点「连接」。若仍 KO，按方案 0 的三种解法处理。

### 方案 2（板子上没有跳线帽时）：用杜邦线手动把 BOOT0 拉到 3.3V

**没有跳线帽 ≠ 没有 BOOT0 引脚**，只是缺个短接帽。找一条**母对母杜邦线**短接即可：

```
① 断电，在板上找标着 BOOT0（或 B0 / BOOT0 排针）的引脚，
   没有丝印就翻原理图，找 BOOT0 网络标号接出去的那根排针
② 用杜邦线把 BOOT0 引脚 ↔ 板上的 3.3V 引脚 短接
③ 上电（或按一下 RESET）
④ CubeProgrammer：RTS=Low、DTR=Low → 点「连接」
   （BOOT0 已经手动拉高了，不需要 RTS 再管它）
⑤ 连上 → 烧完 → 点「断开连接」
⑥ 拔掉那根杜邦线 → 按 RESET
```

> 部分 mcuDev 板的 BOOT0 是 **2.54mm 两针排针但没配帽子**，用杜邦线直接套上去就行；  
> 如果 BOOT0 是焊死在 GND 的（少数版本），那就只能走方案 3。

### 方案 3（最省事，推荐）：放弃 CubeProgrammer，回到 FlyMcu + merged.hex

**FlyMcu 已经在你板子上验证过可用**（日志里成功全片擦除 + 写入 + 校验）。  
它的「DTR 低电平复位 + RTS 高电平进 BootLoader」是**专用脉冲时序**，  
恰好匹配一键下载电路，所以不需要跳线帽。

```
① Keil 里把 bootloader 和 app_demo 都编译好（app 记得先改 IRAM1 = 0x10000）
② 两个 hex 拷到 ota_project/tools/
③ cd /d D:\workBuddyFile\2026-09-10-14-17-26\ota_project\tools
   python merge_hex.py bootloader.hex app_demo.hex -o merged.hex
④ FlyMcu → 程序文件选 merged.hex → 勾「DTR 的低电平复位，RTS 高电平进 BootLoader」
   → 点「开始编程」→ 一次把 Bootloader + App 全烧进去
⑤ 复位 → LED0 500ms 闪 + 串口 v1.0.0
```

> merged.hex 方案完美绕开 CubeProgrammer 的连接难题，也绕开了"烧 App 会擦掉 Bootloader"的坑，  
> **本项目推荐这条路线**。CubeProgrammer 只在需要"只擦 App 扇区、保留 Bootloader"的调试场景才有优势。

---


## 故障速查表

| 现象                                                                    | 90% 原因                                                                                                                                                                        | 一键修法                                                                                                                                                                  |
| --------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| FlyMcu“无法进入 BootLoader”                                               | 一键下载电路没触发                                                                                                                                                                     | BOOT0 切 1 + 手动复位再烧                                                                                                                                                    |
| STM32 串口乱码                                                            | 波特率不对 / GND 没接                                                                                                                                                                | 统一 115200 / 测 GND 通断                                                                                                                                                  |
| ESP32 不停复位                                                            | 供电不足                                                                                                                                                                          | 换 USB 口/线；470uF 电容并电源                                                                                                                                                 |
| ESP32 WiFi 连不上                                                        | SSID/密码错 / 信号弱                                                                                                                                                                | 重新输；用手机热点测                                                                                                                                                            |
| HTTP 404                                                              | URL 或文件名错                                                                                                                                                                     | 浏览器先访问 URL 能下载吗                                                                                                                                                       |
| STM32 收不到包                                                            | TX/RX 接反                                                                                                                                                                      | 把 ESP32 的橙蓝两根线对调                                                                                                                                                      |
| OTA 报 CRC 失败                                                          | 杜邦线接触不良                                                                                                                                                                       | 重插；缩短线长                                                                                                                                                               |
| 烧 App 后 Bootloader 没了                                                 | 烧 App 时点了「清除芯片(Z)」                                                                                                                                                            | 重烧 Bootloader（必须先擦），App 区已是 0xFF 再次直写即可                                                                                                                               |
| LED 行为完全不对                                                            | 板子 LED 极性反                                                                                                                                                                    | main.c 改 `GPIO_PIN_SET/RESET` 对调                                                                                                                                      |
| **CubeProgrammer 报 `GETID 命令未确认` / `连接丢失`**                           | **UART 配置里「奇偶校验」被设成了 Even**                                                                                                                                                   | **改成 `无`（None）**；顺带确认 波特率115200 / 数据位8 / 停止位1 / 流量控制Off                                                                                                               |
| CubeProgrammer 一直连不上（奇偶校验已是 None）                                     | ① RTS/DTR 电平不对；② 一键下载电路没被触发 | ① **RTS 和 DTR 都设成 `Low`**（这块板子实测只有 Low/Low 能连）→ ② 仍不行就手动把 **BOOT0 短到 3.3V** → 按 RESET → 点连接 → 连上后先断开 → 拔线 → 再连接 |
| **空片能连、烧了程序后 CubeProgrammer 一律 `启动装置 : KO`**                          | **芯片里的程序在跑，占用 USART1（PA9/PA10）与 ISP 抢线**（空片时 Flash=0xFF 跑飞，USART1 空着所以能连）| **手动把 BOOT0 短到 3.3V + 按 RESET 再连**（见附录 A 方案 0）；或改用 **FlyMcu + merged.hex**（FlyMcu 自带复位脉冲，不受影响）|
| **App 烧进去后 LED 全灭 + 串口无输出（Bootloader 正常）**                            | **Keil 里 IRAM1 Size 设太小**（如 0x800），链接器把栈顶放到 RAM 极低地址 → App 一进 printf 就栈溢出 HardFault                                                                                           | **Options→Target→IRAM1：Start `0x20000000`，Size `0x10000`（64KB）** → **Rebuild** → 重新生成 hex                                                                             |
| **IRAM1 明明写着 `0x10000`，但 hex 里的初始 SP 还是小值（如 `0x200007C0`）**           | ① 只按了 Build（F7）没按 **Rebuild**，链接器没重新跑；② 拷到 `tools/` 的 hex 是**旧的**（没覆盖成功）；③ **Linker 标签页「Use Memory Layout from Target Dialog」没勾**——这时 Target 标签里的 IRAM1 完全是摆设，实际用的是别的内存布局     | ① 点 **Rebuild**（不是 Build）→ ② 看 `Objects/app_demo.hex` 的**修改时间**是不是刚才 → ③ 重新拷到 `tools/` → ④ **Linker 标签页确认勾了「Use Memory Layout from Target Dialog」**（没勾的话 IRAM1 改动不生效） |
| 想跳过一切猜测，直接验证 hex 里的栈有多大                                               | ——                                                                                                                                                                            | 跑附录 B 的一行命令读初始 SP。**`0x20010000` = 64KB 栈（正确）；`0x200007C0` 这种小值 = 栈不够，必须重编**                                                                                          |
| **scatter 里 `RW_IRAM1` 明明是 `0x10000`，但 hex 里 SP 只有 1~4KB**            | **scatter 只写了 `.ANY (+RW +ZI)`，没有单独的栈区** → 链接器把启动文件里 `AREA STACK` 当成普通 ZI 变量，跟其他数据一起排 → 栈顶落在数据后面（`0x200007C0`），下面 62KB RAM 全空着，**实际可用栈只有 1KB**                                | **改 `Core/Startup/startup_stm32f103zetx.s`：`Stack_Size EQU 0x00000400` → `EQU 0x0000F000`** → Rebuild。再跑附录 B 验证 SP 变成 `0x2000Fxxx`                                    |
| **开机 banner 能打印（`[APP] version : 1.0.0`），但 LED 完全不闪、之后再无 `alive` 输出** | **IAP 经典坑：Bootloader 跳转前执行了 `__disable_irq()`（PRIMASK=1），该状态被带进 App**，而启动文件与 C 库都不会自动清零 → SysTick 中断永不触发 → `HAL_GetTick()` 冻结在 0 → 主循环 `HAL_GetTick() - lastBlink > 500` 永不成立 | **App 的 `main()` 里 `SCB->VTOR = APP_FLASH_BASE;` 下一行加 `__set_PRIMASK(0);`** → Rebuild（详见附录 B-2）                                                                       |

---

## 附录 B-1：栈放置问题（本项目的头号隐蔽坑）

### 现象

- Bootloader 烧进去 LED 正常慢闪 ✓
- App 烧进去 LED 全灭、串口无输出 ✗
- 合并 hex 烧进去同样全灭 ✗

### 根因：栈被塞在数据后面，不是 RAM 顶部

CubeMX/Keil 自动生成的 scatter（`.sct`）大致长这样：

```
LR_IROM1 0x08008000 0x0003C000  {
  ER_IROM1 0x08008000 0x0003C000  {
   *.o (RESET, +First)
   *(InRoot$$Sections)
   .ANY (+RO)
  }
  RW_IRAM1 0x20000000 0x00010000  {      ; ← 这一行是对的（64KB）
   .ANY (+RW +ZI)                        ; ← 但没有为栈单独划区！
  }
}
```

**关键是最后一行**：`.ANY (+RW +ZI)` 会把所有 RW/ZI 段（包括启动文件里  
`AREA STACK` 定义的栈）当普通变量一起排。结果栈被塞在全局变量后面：

```
0x20000000 ┌──────────────┐
           │ .data/.bss   │  ~1KB 全局变量
0x200003C0 ├──────────────┤
           │ Stack 1KB    │  ← 栈顶 = 0x200007C0（就在这儿！）
0x200007C0 ├──────────────┤
           │              │
           │  空闲 62KB    │  ← 完全用不到
           │              │
0x20010000 └──────────────┘
```

栈一溢出就往下踩全局变量，表现为**立刻 HardFault / 莫名死机**。

> 为什么 Bootloader 没事？它的栈有 3.9KB，而且 **Bootloader 里一次 printf 都没有**  
> （只有串口发包，不需要 `vsnprintf` 的大栈帧）。App 一上来就是连续 8 条 printf，  
> 每次 `vsnprintf` 都要几百字节栈，1KB 立刻就爆。

### 修法（改一行）

打开 CubeMX 工程里的启动文件：

```
<你的工程>\Core\Startup\startup_stm32f103zetx.s
```

找到顶部这段：

```asm
; <h> Stack Configuration
;   <o> Stack Size (in Bytes) <0x0-0xFFFFFFFF:8>
; </h>

Stack_Size      EQU     0x00000400          ; ← 改这里
```

改成：

```asm
Stack_Size      EQU     0x0000F000          ; 60KB，把栈顶顶到 RAM 高处
```

保存 → **Rebuild** → 跑附录 B 验证。SP 应变成 `0x2000Fxxx` 这个量级（约 60KB 栈）。

> ⚠️ 如果链接报 "region too small / 无法放置"，说明 RAM 里数据比预想多，  
> 把 `0xF000` 降到 `0xE000` / `0xC000` 再试，改完仍需 SP ≥ `0x2000C000` 才安全。
>
> 同样的改动也建议对 Bootloader 做（更稳），但 Bootloader 现在的 3.9KB 实测能跑，  
> 可以不动。

### 更彻底的做法（可选）：自定义 scatter 给栈单独划区

```
RW_IRAM1 0x20000000 0x0000F000  {     ; 60KB 只放数据
 .ANY (+RW +ZI)
}
ARM_LIB_STACK 0x20010000 EMPTY -0x00008000 { }   ; 32KB 栈，顶在 RAM 末尾
```

但这需要同时把启动文件里的 `AREA STACK` 删掉，否则会有两个栈。  
**改 `Stack_Size` 那一行是风险最低的方案，优先用它。**

---


## 附录 B：一行命令验证 App 的栈大小（每次 Rebuild 后必跑）

App 的初始栈指针就写在 hex 的第一个数据记录里（也就是 App 向量表的第 0 项）。  
读出来就知道栈够不够，**不用烧录、不用猜**。

```bash
cd "D:/workBuddyFile/2026-09-10-14-17-26/ota_project/tools"
"C:/Users/JD/.workbuddy/binaries/python/versions/3.13.12/python.exe" -c "
for f in ['bootloader.hex','app_demo.hex','merged.hex']:
    try:
        fp = open(f, errors='ignore')
    except: continue
    for line in fp:
        if line.startswith(':10000000') or line.startswith(':10800000'):
            sp = int.from_bytes(bytes.fromhex(line[9:17]), 'little')
            print(f'{f:16s} -> 初始SP = 0x{sp:08X}  (栈空间 {(sp-0x20000000)/1024:.1f} KB)')
            break
```

**判读标准：**

| 文件               | 期望值                                             | 不对时怎么办                                                                          |
| ---------------- | ----------------------------------------------- | ------------------------------------------------------------------------------- |
| `bootloader.hex` | `0x20000F80` 附近（3~4KB，够用）                       | 变了也没事，Bootloader 代码简单、不进 printf                                                 |
| `app_demo.hex`   | **`0x2000Fxxx`（≈60KB，即 `Stack_Size = 0xF000`）** | 还是 `0x200007C0` 这种 1~2KB 小值 → 回上面那条排查（Rebuild / 覆盖旧文件 / Linker 勾选 / Stack_Size） |
| `merged.hex`     | 与 `bootloader.hex` 相同                           | 若等于 app 的值，说明合并顺序反了，重新合并时把 bootloader 放第一个参数                                    |

> ⚠️ `merged.hex` 的第 0 项来自 **Bootloader**（0x08000000），不是 App。  
> 所以 `merged.hex` 的初始 SP 应该跟 `bootloader.hex` 一致，这才是正常的。  
> **App 的栈要看 `app_demo.hex`**（或 merged.hex 里 0x08008000 那一项），别搞混。

---

## 附录 B-2：IAP 跳转后 SysTick 停摆（PRIMASK 遗留坑）

### 现象（很有迷惑性）

烧完 merged.hex 复位后：

- ✅ 串口**能**打印开机 banner：`[APP] version : 1.0.0` / `[APP] VTOR : 0x08008000`
- ❌ 之后**再无任何输出**，也没有 `[APP] alive Ns` 心跳
- ❌ **LED 一直不亮**（既不是 200ms 慢闪，也不是 500ms 闪烁）

关键特征：**banner 能出来，说明 App 已经跑到 main 里了、UART 也是通的**，所以问题不在栈、不在地址、不在跳转本身。

### 原因

Bootloader 的 `JumpToApp()` 里按 IAP 规范做了：

```c
__disable_irq();        /* → CPSID i，PRIMASK = 1 */
...
jumpToApp();            /* 跳到 App 的 Reset_Handler */
```

`PRIMASK` 是 **CPU 内核寄存器**，不会因为跳转而复位。而：

- App 的 `startup_stm32f1xx.s` 里 `Reset_Handler` 只调 `SystemInit()` 和 `__main`，**不动 PRIMASK**
- ARM C 库的 `__main` / `__rt_entry` 初始化过程也**不动 PRIMASK**
- `HAL_Init()` 里 `SysTick_Config()` 只写 `SysTick->CTRL`，**依然不动 PRIMASK**

结果：**全局中断始终被屏蔽**。SysTick 的计数器在跑，但中断请求永远递不出去 → `uwTick` 冻结在 0 → `HAL_GetTick()` 永远返回 0。

于是 App 主循环：

```c
uint32_t now = HAL_GetTick();          /* 永远是 0 */
if (now - lastBlink > BLINK_PERIOD_MS) /* 0 - 0 = 0，永远不 > 500 */
```

这个条件永远不成立 → **LED 一次都不翻转、alive 心跳一行都不打印**。

而 banner 是 `printf` 走 **UART 轮询发送**（不依赖中断），所以照样能打出来——这就是"串口有输出但程序像死了一样"的原因。

### 修法（二选一，推荐第 1 种）

**① 在 App 的 main() 里解除屏蔽（本项目已采用）**

```c
    SCB->VTOR = APP_FLASH_BASE;   /* 先把向量表指到 App */
    __set_PRIMASK(0);             /* 再开全局中断（顺序不能反） */
```

> 顺序很关键：必须**先**设好 `SCB->VTOR`，**再**解除屏蔽。  
> 反过来的话，开中断的瞬间若命中旧向量表（Bootloader 区），会跳到错误的处理函数。

**② 在 Bootloader 跳转前就不屏蔽中断**

把 `JumpToApp()` 里的 `__disable_irq()` 换成：跳转前重新 `__enable_irq()`（但要确保 NVIC 挂起位已清、SysTick 已停，本项目 `JumpToApp()` 里第 4/5 步已经做了）。

### 为什么只烧 Bootloader 时一切正常

Bootloader 自己**从不**屏蔽全局中断（`__disable_irq()` 只在跳转函数里执行），而且它不进 `printf`，栈需求也小——所以单独烧 Bootloader 时 LED 慢闪完全正常。这个坑只在「**Bootloader 跳 App**」这条路径上才会暴露。
