# STM32 测试 App — CubeMX 配置步骤

> App 与 Bootloader 是**两个独立工程**。App 链接到 0x08008000，
> 用 USART1（板载 CH340）打印日志，PB0（板载 LED0）闪烁。

## 1. CubeMX 配置（与 Bootloader 类似，仅串口不同）

1. 新建工程 → `STM32F103ZETx`
2. **RCC**：HSE → Crystal/Ceramic Resonator
3. **SYS**：Debug → Serial Wire
4. **USART1**（板载 CH340 走这条）：
   - Mode → Asynchronous，115200 / 8N1
   - NVIC 不需要勾中断（只用阻塞发送 printf）
   - 引脚自动 PA9(TX) / PA10(RX)，**不要外接任何东西，CH340 焊在板上**
5. **GPIO**：
   - PA0 → GPIO_Input，User Label `KEY`（App 里预留，本版本暂不使用）
   - **LED0 引脚不用在 CubeMX 里配！**——代码里通过 `OTA_LED_Init()` 自动初始化 `OTA_LED_PORT/OTA_LED_PIN`（默认 PB5）。改引脚去改 `main.c` 顶部那两个 #define 即可。
6. **时钟树**：与 Bootloader 相同（HSE 8M × PLL9 = 72MHz，APB1 /2）
7. Project Name：`app_demo`，生成代码（勾 pair of .c/.h）

## 2. 替换代码

用 `Core/Src/main.c` 整体覆盖工程生成的 main.c。

## 3. Keil 关键设置（决定成败）

### 3.1 修改 Flash 地址（告诉链接器"我不在 0x08000000"）
- Options for Target → **Target** 标签 → IROM1：
  - Start: `0x8008000`
  - Size:  `0x3C000`  （240KB）

### 3.2 烧录策略（CH340 + FlyMcu）
- Options → **Output** → 勾选 **Create HEX File**
- 改过 IROM1 后，hex 内的地址自动就是 0x08008000，FlyMcu 直接烧
- **FlyMcu 烧 App 的正确流程**（FlyMcu 0.188 没有"指定地址擦除"选项）：
  1. 烧 App 之前，**确认 Bootloader 烧录时已经做过「清除芯片(Z)」全片擦除**——这样 App 区 0x08008000~ 是 0xFF，hex 能直接写入
  2. 烧 App 时**不要再点「清除芯片(Z)」**（会杀掉 Bootloader）
  3. 选 hex → 直接点「开始编程」
- ❌ 以后反复改 App 调试，不要再用 FlyMcu 烧 App——用 STM32CubeProgrammer（ST 官方），它支持指定扇区擦除

### 3.3 printf 重定向
本代码已包含 Keil MDK 的 `fputc` 重定向。
如果用 STM32CubeIDE(GCC)：把 `fputc` 改为：
```c
int _write(int fd, char *ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, 100);
    return len;
}
```

## 4. 编译两版固件

| 版本 | APP_VERSION | 灯行为 | 用途 |
|---|---|---|---|
| v1.0 | `"1.0.0"` | 500ms 慢闪 | ST-Link 直接烧到 0x08008000，验证跳转 |
| v1.1 | `"1.1.0"` | 150ms 快闪 | 生成 bin 放服务器，验证 OTA |

## 5. 生成 bin 文件

Keil → Options → User → After Build/Rebuild 勾 Run #1，填：
```
fromelf --bin --output=app_v11.bin .\Objects\app_demo.axf
```
（axf 名字以你的工程实际输出为准）
编译后工程目录下得到 `app_v11.bin` → 拷到电脑的 `firmware/` 目录。

或者用命令行（cmd 里，在工程目录）：
```
C:\Keil_v5\ARM\ARMCLANG\bin\fromelf --bin --output=app_v11.bin Objects\app_demo.axf
```

## 6. 烧录 v1.0（CH340 + FlyMcu）

- 板子 USB 连电脑 → 打开 FlyMcu → 选 CH340 的 COM 口，115200
- 勾 "DTR 的低电平复位，RTS 高电平进 BootLoader"（一键下载）
- **不要点「清除芯片(Z)」**——烧 Bootloader 时已经全片擦过一次了
- 程序文件选 `app_demo.hex` → 开始编程
- 复位 → 应看到：**LED 每 500ms 闪烁 + CH340 串口打印 `[APP] version 1.0.0`**
- 打印内容包含运行地址和 VTOR，可当场验证跳转是否成功
- 烧完关闭 FlyMcu 释放 COM 口，再用串口助手看日志
