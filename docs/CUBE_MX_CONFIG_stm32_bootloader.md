# STM32 Bootloader — CubeMX 配置步骤

> 芯片：STM32F103ZETx（mcuDev / 含板载 CH340 板子）
> 工具链：MDK-ARM (Keil) 或 STM32CubeIDE 均可，下文以 Keil 为例

## 1. 新建工程

1. 打开 STM32CubeMX → New Project
2. 搜索 `STM32F103ZETx` → 选中，Start Project

## 2. 引脚与外设配置

### 2.1 RCC（时钟源）
- `System Core → RCC`
  - HSE → **Crystal/Ceramic Resonator**（板载 8MHz 晶振）
  - LSE 保持 Disable

### 2.2 SYS（调试口）
- `System Core → SYS`
  - Debug → **Serial Wire**（SWD，ST-Link 调试/烧录用，必须开）
  - Timebase Source → SysTick

### 2.3 USART2（OTA 通信口，接 ESP32）
- `Connectivity → USART2`
  - Mode → **Asynchronous**
  - Parameter Settings：Baud Rate **115200**、Word Length 8、Parity None、Stop 1
  - `NVIC Settings` 标签页：✅ **USART2 global interrupt**（必须勾）
  - `DMA Settings` 标签页：
    - 点 Add → **USART2_RX**
    - Stream/Channel 默认（DMA1 Channel 6）
    - Direction: Peripheral To Memory
    - Priority: **High**
    - Mode: **Circular**（循环模式，防止漏数据）
    - Data Width: **Byte**（按字节）
- **确认右侧引脚自动变成 PA2(TX)、PA3(RX)**

### 2.4 GPIO
- **PA0**（用户按键，强制进下载模式）：
  - 点引脚选 `GPIO_Input`，User Label 填 `KEY`
  - Pull: **No pull-up and no pull-down**（板子外部已有上拉/下拉，mcuDev KEY0 是外部上拉到 3.3V，按下接地 = 低电平触发）
  - ⚠️ 如果你的板子按键按下是低电平，已经是默认（KEY_ACTIVE_LEVEL 在 ota_config.h 里）
- **LED0 引脚不用在 CubeMX 里配！**——代码会通过 `OTA_LED_Init()` 自动初始化 `OTA_LED_PORT/OTA_LED_PIN`（默认 PB5）。改引脚去改 `ota_config.h` 那两个 #define 即可，不用回 CubeMX 重新生成。

### 2.5 不需要配置的
- USART1：板载 CH340 用，Bootloader 阶段不碰（保持 Disable）
- CRC 外设：我们用软件查表 CRC（与 ESP32 zlib-CRC32 完全一致），不用硬件 CRC（F1 硬件 CRC 是非反射版本，两端对不上，是个坑）
- LED0 GPIO：代码自己初始化，不用在 CubeMX 里配

## 3. 时钟树配置

进入 `Clock Configuration`：
1. HSE = 8 MHz
2. PLL Source Mux → **HSE**
3. PLLMUL → **×9**
4. System Clock Mux → **PLLCLK** → HCLK = **72** MHz
5. APB1 Prescaler → **/2**（APB1 = 36MHz，USART2 在 APB1 上）
6. 其余默认（APB2 = 72MHz）

## 4. 工程生成

`Project Manager`：
- Project Name：`bootloader`
- Toolchain/IDE：**MDK-ARM**（或 STM32CubeIDE）
- Code Generator 页：
  - ✅ *Generate peripheral initialization as a pair of '.c/.h' files per peripheral*
  - ✅ *Keep User Code when re-generating*

点 **GENERATE CODE**。

## 5. 生成后替换/添加代码

1. 用本目录 `Core/Src/main.c` **整体覆盖**工程里的 `Core/Src/main.c`
2. 把 `Core/Inc/ota_config.h`、`Core/Inc/ota_protocol.h`、`Core/Inc/flash_if.h` 拷到 `Core/Inc/`
3. 把 `Core/Src/ota_protocol.c`、`Core/Src/flash_if.c` 拷到 `Core/Src/`
4. Keil 里把两个新 .c 文件加入 Application/User 组（右键组 → Add Existing Files）

## 6. Keil 关键设置（重要）

1. **优化等级**：Options → C/C++ → Optimization 选 **-O1 或 -O2**
   （Bootloader 空间 24KB，-O0 编译 HAL 会超）
2. Options → Target → IROM1：Start `0x8000000`，Size `0x6000`（24KB，默认即是）
3. Options → Output → 勾选 **Create HEX File**（串口烧录必需，hex 自带地址）

## 7. 编译烧录（CH340 + FlyMcu 串口方案）

- Build（F7）→ 0 Error → 生成 `Objects/bootloader.hex`
- 用 **FlyMcu** 烧录（详见根目录 README "CH340 + FlyMcu 串口烧录步骤"）：
  - 勾选 "DTR 的低电平复位，RTS 高电平进 BootLoader"（一键下载）
  - 首次烧录可选全片擦除
- 现象：LED 慢闪（因为没有有效 App，自动进下载模式）→ 正常！
- 没有 SWD 调试器时，CubeMX 里 SYS 的 Debug 保持默认即可，不影响串口烧录
