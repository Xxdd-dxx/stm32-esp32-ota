# ESP32-C3 OTA 下载器 —— ESP-IDF 版使用说明

## 一、之前那个报错是怎么回事

```
CMake Error: The source directory "...\esp32_downloader"
does not appear to contain CMakeLists.txt
```

原因很简单：**你 VSCode 里装的是 ESP-IDF 插件，不是 PlatformIO 插件。**

- 我本机检查了你的 VSCode 扩展列表：有 `espressif.esp-idf-extension`，**没有** PlatformIO。
- 你点的是 ESP-IDF 插件的 **Build** 按钮（`D:\Espressif\tools\cmake\...\cmake.exe -G Ninja -DESP_PLATFORM=1 ...` 就是它发的）。
- 而 ESP-IDF 的规矩是：**工程根目录必须有 `CMakeLists.txt`**。
- 原来的 `esp32_downloader` 是按 PlatformIO 写的（只有 `platformio.ini` + `src/main.cpp`），自然没有 CMakeLists.txt → 直接报错。

**解决思路**：既然你的 ESP-IDF v5.4.4 环境已经完整装好了（工具链、Python 环境都在 `D:\Espressif`），
那就把工程改成**标准 ESP-IDF 工程**，不再依赖 PlatformIO。好处是不用再下载 1GB+ 的 Arduino 框架包，
而且简历上写"基于 ESP-IDF 原生开发"比"Arduino 封装"更硬。

---

## 二、新工程结构

```
esp32_downloader/
├── CMakeLists.txt              ← ★ 顶层，之前缺的就是它
├── sdkconfig.defaults          ← ★ 默认配置（含关键的控制台改道设置）
├── main/
│   ├── CMakeLists.txt          ← 声明只编译 ota_downloader.c
│   └── ota_downloader.c        ← ★ 主程序（纯 ESP-IDF API 重写）
├── README_ESPIDF.md            ← 本文件
└── _platformio_alt/            ← 原来 PlatformIO 版本，留作备份，不参与编译
    ├── platformio.ini
    └── src/main.cpp
```

**原来的 `src/main.cpp` 没有丢**，挪到 `_platformio_alt/` 了。里面的协议逻辑（帧格式、CRC32 表）
完整保留在新代码里，只是把 Arduino 的 `WiFi.h / HTTPClient.h / HardwareSerial` 换成了
ESP-IDF 的 `esp_wifi / esp_http_client / driver/uart`。

---

## 三、VSCode 里怎么操作（四步）

### 1. 打开正确的文件夹
VSCode 里 **File → Open Folder**，选：
```
D:\workBuddyFile\2026-09-10-14-17-26\ota_project\esp32_downloader
```
（要选 `esp32_downloader` 这一层，不要选它的上一层）

### 2. 确认芯片型号和串口
- 底部状态栏点 **"ESP32-C3"** 那块（或 `Ctrl+Shift+P` → 输入 `ESP-IDF: Set Espressif device target` → 选 **esp32c3**）。
- 底部状态栏的串口选择器选 **COM11**（你之前配的）。
  如果找不到串口：设备管理器里看 USB 串行设备是哪个 COM 口。

### 3. 改 WiFi 和 IP（**必做**）
打开 `main/ota_downloader.c`，改文件顶部这三行：

```c
#define WIFI_SSID     "YOUR_WIFI_SSID"                              /* ← 你的 WiFi 名 */
#define WIFI_PASS     "YOUR_WIFI_PASSWORD"                       /* ← 你的 WiFi 密码 */
#define FIRMWARE_URL  "http://192.168.1.100:8000/app_v11.bin"   /* ← 你电脑的 IP */
```

查电脑 IP：`cmd` 里执行 `ipconfig`，看「无线局域网适配器 WLAN」的 IPv4 地址。

### 4. 点按钮
状态栏从左到右依次点：

| 按钮 | 作用 | 对应命令 |
|------|------|----------|
| 🔨 **Build** | 编译 | `idf.py build` |
| ⚡ **Flash** | 烧录 | `idf.py flash` |
| 📺 **Monitor** | 看串口日志 | `idf.py monitor` |

> Monitor 里 **退出按 `Ctrl + ]`**（不是 Ctrl+C，Ctrl+C 会退出登录态但串口还占着）。

---

## 四、⚠️ 关键注意点（踩坑预警）

### 1. 单 USB 口方案（默认采用）

板子只有一个 USB 口 → ESP32 控制台改回 UART0（日志直接走你熟悉的 COM11），
STM32 通信挪到 GPIO4/5，避免和 UART0 抢脚。

**接线**（杜邦线需要从原来的 GPIO20/21 挪到 GPIO4/5）：

| ESP32-C3 | STM32F103 |
|---|---|
| GPIO4 (TX) | PA3 (RX) |
| GPIO5 (RX) | PA2 (TX) |
| GND | GND |

**代码里已经改好**：
- `main/ota_downloader.c`：`PIN_STM32_TX=4`, `PIN_STM32_RX=5`
- `sdkconfig.defaults`：`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`

烧录后看 Monitor → COM11 应该直接出 `[ota_downloader] ...` 日志。

> 如果你板子有"USB"和"UART"两个 Type-C 口（DevKitM-1），但只有一个能烧/看日志，
> 说明本工程的默认方案（GPIO4/5 + UART0 控制台）正合你用。
> 另一个备选（控制台走原生 USB）已废弃，不要再选。

**第 2 步**：`sdkconfig.defaults` 里把控制台改回 UART0
```ini
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=n
```

**第 3 步**：`idf.py fullclean` 后重新 Build（改了 sdkconfig 必须全清重编）

**第 4 步**：杜邦线从 ESP32 的 GPIO20/21 挪到 **GPIO5 / GPIO4**

这样日志走原来的 UART 口，与 STM32 的通信走 GPIO4/5，互不干扰。

### 3. 改了 `sdkconfig.defaults` 之后必须 `fullclean`

`sdkconfig` 一旦生成就不会再读 `.defaults`。所以每次改完 defaults，
要执行 **Full Clean**（状态栏 🗑️ 图标）再 Build，否则改动不生效。

---

## 五、完整联调流程（重新捋一遍）

**前提**：STM32 的 Bootloader 已经烧好（前面 `[APP] alive 40s (v1.0.0)` + LED 闪烁那步已验证通过）。

```
① Keil 打开 app_demo 工程
   → APP_VERSION  改成 "1.1.0"
   → BLINK_PERIOD_MS 改成 150
   → Rebuild → fromelf 生成 app_v11.bin

② 把 app_v11.bin 放到工具目录，起 HTTP 服务器
   cd D:\workBuddyFile\2026-09-10-14-17-26\ota_project\tools
   python -m http.server 8000

③ VSCode 打开 esp32_downloader → Build → Flash → Monitor
   （确认日志打印出 "WiFi OK, IP: 10.x.x.x"）

④ 让 STM32 进入 Bootloader 下载模式
   按住 KEY0(PA0) → 按一下 RESET → 松开 KEY0
   现象：LED0 慢闪（200ms），Bootloader 在等 ESP32

⑤ ESP32 按板上 RST 键复位，触发下载
   日志会依次出现：
     OTA session started (N packets, M bytes)
     Progress: ... 10% 20% ... 100%
     All packets sent. CRC32 = 0xXXXXXXXX
     OTA SUCCESS! STM32 rebooting to new app

⑥ 成功判据
   串口（CH340 那个，115200）打印 version:1.1.0
   LED0 变成 150ms 快闪
```

---

## 六、常见报错对照

| 现象 | 原因 | 处理 |
|------|------|------|
| `does not appear to contain CMakeLists.txt` | 缺顶层 CMakeLists.txt | ✅ 已修复 |
| Monitor 一片空白 | 插错 USB 口 | 换插 "USB" 口 |
| `CMD_START not acked` | STM32 不在下载模式 | 按 ④ 重新进下载模式，再按 ESP32 的 RST |
| `HTTP connect failed` | IP 写错 / 服务器没起 / 不在同一 WiFi | 核对 `ipconfig` 的 IP；确认 `python -m http.server 8000` 在跑 |
| `Bad HTTP response. status=404` | bin 文件不在 tools 目录 | 把 `app_v11.bin` 拷到 `python -m http.server` 的执行目录 |
| `Packet N failed after 5 retries` | 串口线接反 / 波特率 / STM32 卡死 | 检查 GPIO21→PA3、GPIO20←PA2、共地 |
