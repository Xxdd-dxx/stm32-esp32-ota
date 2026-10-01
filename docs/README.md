# 双 MCU 无线 OTA 远程固件升级系统

ESP32-C3（WiFi 通信协处理器）+ STM32F103ZET6（主控 + Bootloader）
UART + DMA · 自定义二进制协议 · CRC32 校验 · 双区备份

---

## 一、项目结构

```
ota_project/
├── stm32_bootloader/        # STM32 Bootloader 工程（CubeMX + HAL）
│   ├── CUBE_MX_CONFIG.md    # CubeMX 图形化配置步骤（照着点）
│   └── Core/
│       ├── Inc/
│       │   ├── ota_config.h       # Flash 分区 / 协议常量
│       │   ├── ota_protocol.h     # 协议接口
│       │   └── flash_if.h         # Flash 读写接口
│       └── Src/
│           ├── main.c             # 完整 main（可直接覆盖 CubeMX 生成的）
│           ├── ota_protocol.c     # 帧解析 + CRC32 + 组帧发送
│           └── flash_if.c         # 擦除 / 写入 / B→A 复制 / 配置区
├── stm32_app/                # 测试用 App 工程（LED + 版本打印）
│   ├── CUBE_MX_CONFIG.md    # App 的 CubeMX 配置 + Keil 关键设置
│   └── Core/Src/main.c
├── esp32_downloader/         # ESP32-C3 下载器（VSCode + ESP-IDF 5.4）
│   ├── CMakeLists.txt        # 顶层工程文件（ESP-IDF 必需）
│   ├── sdkconfig.defaults    # 默认配置（控制台改走原生 USB）
│   ├── main/ota_downloader.c # WiFi + HTTP 流式下载 + 分包发送
│   └── README_ESPIDF.md      # ESP-IDF 版使用说明 + 常见报错对照
└── README.md
```

## 二、Flash 分区（STM32F103ZET6，512KB）

| 地址 | 大小 | 用途 |
|---|---|---|
| 0x08000000 | 24KB | Bootloader |
| 0x08006000 | 8KB | System Config（标志位/版本/CRC） |
| 0x08008000 | 240KB | App A（当前运行区） |
| 0x08044000 | 240KB | App B（OTA 下载暂存区） |

## 三、通信协议（帧格式，小端）

```
| 帧头(2B) | 长度(2B) | 指令(1B) | 包序号(2B) | Payload(N B) | CRC32(4B) | 帧尾(2B) |
| AA 55    | N+3     | CMD      | SEQ       | DATA         | CRC       | 0D 0A   |
```
- 长度 = 指令 + 序号 + Payload 的字节数
- CRC32 覆盖范围：指令 + 序号 + Payload（标准 zlib CRC-32）
- 双端逐包 CRC 校验 + 全文件流式 CRC 校验

| 指令 | 值 | Payload | 说明 |
|---|---|---|---|
| CMD_START | 0x01 | 固件大小(4B)+总包数(2B) | 开始升级，STM32 擦除 App B |
| CMD_DATA | 0x02 | 1KB 数据 | 固件包 |
| CMD_ACK | 0x03 | 序号(2B)+状态(1B) | 应答 |
| CMD_NACK | 0x04 | 序号(2B) | 请求重传 |
| CMD_END | 0x05 | 发送端累计CRC32(4B) | 传输结束 |
| CMD_DONE | 0x06 | 状态(1B) | STM32 处理结果 |
| CMD_GET_INFO | 0x07 | 空 | 查询版本 |
| CMD_INFO | 0x08 | 版本信息 | 应答查询 |

## 四、完整操作流程

### 步骤 1：编译烧录 Bootloader
1. CubeMX 按 `stm32_bootloader/CUBE_MX_CONFIG.md` 配置并生成工程
2. 把 `Core/` 下的 main.c、ota_config.h、ota_protocol.c/h、flash_if.c/h 拷进工程
3. 把 `ota_protocol.c`、`flash_if.c` 加入 Keil 源文件组
4. **优化等级设为 -O1 以上**（Options → C/C++ → Optimization，-O0 编译 HAL 会超过 24KB Bootloader 分区）
5. **Options → Output → 勾选 "Create HEX File"**（串口烧录用 hex 最稳，自带地址信息）
6. 编译后用 **FlyMcu + 板载 CH340** 烧录（详见下方"附：CH340 烧录步骤"，首次可选全片擦除）

### 步骤 2：编译烧录 App v1.0
1. CubeMX 按 `stm32_app/CUBE_MX_CONFIG.md` 生成工程（LED 引脚不用配，代码自动初始化）
2. 替换 main.c，**关键设置**：
   - Options → Target → IROM1：Start `0x08008000`，Size `0x3C000`
   - Options → Output → 勾选 **Create HEX File**
   - （改了 IROM1 后，Keil 生成的 hex 记录的地址就是 0x08008000，FlyMcu 直接烧 hex 即可，无需手动改写入地址）
3. `APP_VERSION` 保持 `"1.0.0"`，编译
4. **FlyMcu 烧 App：⚠️ 不要点「清除芯片(Z)」（FlyMcu 0.188 没有"指定地址范围"选项）**
   - 原因：烧 Bootloader 时已经全片擦过一次，App 区 0x08008000~ 当前是干净的 0xFF，hex 可直接写入
   - 如以后非要重烧 App 而非走 OTA：用 STM32CubeProgrammer（ST 官方工具，支持扇区擦除）
5. 复位后：LED 闪烁 + 串口助手（CH340 那个 COM 口，115200）打印 `[APP] version 1.0.0` → 验证跳转成功

### 步骤 3：制作 v1.1 固件
1. App 工程里把 `APP_VERSION` 改成 `"1.1.0"`（闪灯频率也顺手改一下，更直观）
2. 重新编译，从 `Output/` 目录找到 `.axf` 对应的 `.hex`，或用 `fromelf` 转 bin
   - Keil 命令行：`fromelf --bin -o app_v11.bin Objects/xxx.axf`
   - 或 User 界面勾选 After Build 自动生成 bin

### 步骤 4：启动固件服务器
电脑上新建目录 `firmware/`，放入 `app_v11.bin`，执行：
```bash
cd firmware
python -m http.server 8000
# 服务器地址就是 http://<电脑IP>:8000/app_v11.bin
# 查电脑IP：ipconfig（Windows）
```

### 步骤 5：配置 ESP32 并下载
1. VSCode 打开 `esp32_downloader/` 文件夹（工程已是 ESP-IDF 结构，需用 ESP-IDF 插件）
2. 修改 `main/ota_downloader.c` 顶部三个宏：WiFi 名、密码、固件 URL
3. 状态栏点 🔨 Build → ⚡ Flash → 📺 Monitor（首次编译几分钟）
4. ⚠️ 烧录 + 看日志要插板子上标 `USB` 的口（详见 `esp32_downloader/README_ESPIDF.md`）

### 步骤 6：执行 OTA 升级（演示动作）
1. **按住 STM32 板上用户按键（PA0）→ 按一下复位 → 松开按键**：进入 Bootloader 下载模式（LED 慢闪）
2. 复位 ESP32（或按其 RST）：开始 WiFi 下载 + 分包发送（日志可见进度条）
3. 完成后 STM32 自动：CRC 校验 → App B 复制到 App A → 写配置 → 复位
4. 观察串口打印变成 `[APP] version 1.1.0`，灯频率变化 → **OTA 成功**

## 五、接线（回顾）

| ESP32-C3 | STM32F103ZET6 | 说明 |
|---|---|---|
| GPIO4 (TX) | PA3 (USART2_RX) | 固件数据 |
| GPIO5 (RX) | PA2 (USART2_TX) | ACK |
| GND | GND | 共地 |
| — | PA9/PA10 (USART1) | **勿动，板载 CH340 占用（烧录+日志两用）** |

两板各自 USB 供电。

## 五点五、CH340 + FlyMcu 串口烧录步骤（无 ST-Link 方案）

你板载的 CH340 + BOOT0/复位控制电路支持"一键下载"，配合 **FlyMcu（MCU-ISP）** 使用：

### 准备
1. 下载 FlyMcu（搜"FlyMcu mcuisp"，免安装绿色软件）
2. 板子 USB 连电脑，设备管理器确认 CH340 的 COM 口号
3. Keil 工程 Options → Output 勾 **Create HEX File**，编译出 hex

### FlyMcu 设置
1. 芯片类型：STM32F1（按提示选大容量/高容量）
2. 串口：选 CH340 的 COMx，波特率 **115200**（稳定优先）
3. **勾选 "DTR 的低电平复位，RTS 高电平进 BootLoader"** —— 这就是"一键下载"的核心，板子自动进 ISP 模式
4. 烧录动作（**两个工程分别这样操作**）：
   - **烧 Bootloader（仅此一次）**：点「清除芯片(Z)」→ 选 hex → 「开始编程」
     - 全片擦除 OK，因为这是首次烧录，App 区本来就是空的
   - **烧 App v1.0**：⚠️ **不要点「清除芯片(Z)」**——直接选 hex → 「开始编程」
     - 原因：刚才烧 Bootloader 时已经全片擦过一次，App 区 0x08008000~ 整段是 0xFF，hex 可以直接写入
     - ❌ 千万不要"为保险"再点一次清除芯片（会把 Bootloader 一起干掉）
5. 之后 App 升级一律走 OTA，不再用 FlyMcu 烧 App

### 一键下载失败的兜底（手动进 ISP）
1. 找到板上 BOOT0 跳线/拨码，切到 `1`
2. 按一下复位键
3. FlyMcu 点"开始编程"
4. 烧完把 BOOT0 切回 `0`，再复位

### 日常使用注意
- 烧录时 FlyMcu 占用 COM 口，**烧完记得关掉 FlyMcu 或断开，否则串口助手打不开这个口**
- CH340 是"烧录 + App 日志"两用口：不烧录时随时用串口助手看 App printf
- OTA 进行中也可以插着看日志，互不影响（不同波特率除外——都是 115200，正好）


## 六、常见问题排查

| 现象 | 原因 | 解决 |
|---|---|---|
| ESP32 收不到 ACK | TX/RX 接反 / STM32 没进下载模式 | 交叉换线；按住 KEY 复位进 Bootloader |
| STM32 串口乱码 | 波特率不一致 / 共地没接 | 统一 115200；检查 GND |
| 升级后起不来 | App 的 IROM 没改 / VTOR 没设 | 检查 0x08008000 和 SCB->VTOR |
| 烧 App 后 bootloader 没了 | FlyMcu 点了「清除芯片(Z)」 | 重烧 Bootloader 一次，App 以后走 OTA，别再用 FlyMcu 烧 App |
| FlyMcu 没有「指定地址范围」擦除 | FlyMcu 0.188 已简化 | 见上方烧录流程：烧 Bootloader 才擦，烧 App 不擦 |
| FlyMcu 提示"无法进入 BootLoader" | 一键下载电路没生效 | BOOT0 切 1 + 手动复位再烧；换 USB 线/换 USB 口 |
| CRC 校验失败 | 数据线过长干扰 / 接触不良 | 缩短杜邦线，重插 |
| ESP32 连 WiFi 后复位 | 供电不足 | 换 USB 口/短线，必要时加 470uF 电容 |

## 七、后续迭代路线（简历加分项）

1. **看门狗回滚**：新固件 5s 内未上报心跳 → Bootloader 自动回滚旧版本
2. **断点续传**：包序号持久化到 Config 区，断电后从断点继续
3. **MQTT 云端下发**：替换 HTTP，服务器主动推送升级指令
4. **版本管理**：Config 区记录版本历史，支持降级
