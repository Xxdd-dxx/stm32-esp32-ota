# 双 MCU 无线 OTA 远程升级系统

**STM32F103ZET6 + ESP32-C3** 实现的 IAP 远程固件升级：ESP32-C3 通过 WiFi 从 HTTP 服务器流式拉取固件，经 UART 用自定义二进制协议转发给 STM32，STM32 的 Bootloader 完成暂存、校验、搬移与跳转。

整个升级过程**不触碰运行区**，下载阶段掉电可正常启动原固件。

---

## 硬件

| | 主控 | 通信 | 关键引脚 |
|---|---|---|---|
| 下位机 | STM32F103ZET6（Cortex-M3，512KB Flash / 64KB SRAM，Flash 页 2KB） | USART2 @ 115200 8N1 | PA2=TX、PA3=RX、KEY0=PE4、LED=PB5 |
| 上位机 | ESP32-C3（ESP-IDF v5.4.4 + FreeRTOS） | UART1 | GPIO4=TX→PA3、GPIO5=RX←PA2 |

接线只需 3 根：**GPIO4→PA3、GPIO5←PA2、GND 共地**。

---

## 数据流

```
HTTP 服务器 ──WiFi──> ESP32-C3 ──UART(115200)──> STM32 Bootloader ──> Flash
                     256B 流式分片                 帧解析 + CRC + 写暂存区
                     ACK 超时重传 ×5               ↓
                                              整包校验 → 复制到运行区 → 回读校验 → 复位跳转
```

---

## Flash 分区

| 区域 | 起始地址 | 大小 | 作用 |
|---|---|---|---|
| Bootloader | `0x08000000` | 24 KB | 启动判定、串口下载、Flash 操作 |
| Config | `0x08006000` | 8 KB | 跨复位保存状态（magic / 升级请求 / 复制中标志） |
| App A（运行区） | `0x08008000` | 240 KB | **唯一运行地址**，Bootloader 永远跳这里 |
| App B（暂存区） | `0x08044000` | 240 KB | 新固件下载缓冲，不直接运行 |

Config 区与 App A **必须不同页**：复制前要擦除 App A 整片，若 Config 与之同页，"复制中"标志会被一起擦掉，掉电保护失效。

---

## 通信协议

帧格式（小端）：

```
AA 55 | len(2) | cmd(1) | seq(2) | payload(N) | CRC32(4) | 0D 0A
         ↑ len = 3 + N                          ↑ 覆盖 cmd+seq+payload
```

最小帧 13 字节；DATA 帧 269 字节（256 净荷）；CRC32 多项式 `0xEDB88320`。

| 命令 | 值 | 方向 | 说明 |
|---|---|---|---|
| START | 0x01 | →STM32 | 携带固件大小 + 总包数，收方擦暂存区 |
| DATA | 0x02 | →STM32 | 固件分片，末包可短 |
| ACK | 0x03 | →ESP32 | 携带 seq + 状态，重传 5 次上限 |
| NACK | 0x04 | →ESP32 | 帧损坏 |
| END | 0x05 | →STM32 | 携带发送端整包 CRC32 |
| DONE | 0x06 | →ESP32 | 异步结果通知（后续操作耗时数秒，不能用 ACK） |
| GET_INFO | 0x07 | →STM32 | 探测 STM32 是否进入下载模式 |
| INFO | 0x08 | →ESP32 | 返回 Bootloader 版本 |

**采用长度前缀定界，不是分隔符定界** —— 净荷里出现 `AA 55` 不会误判为帧头，无需字节转义。

---

## 关键设计决策

**1. 自管 DMA + 串口空闲中断，不用 HAL 的 `ReceiveToIdle_DMA`**

HAL 该 API 内部无条件打开 DMA 的 **半传输（HT）中断**，缓冲收满一半（2048→1024）时回调一次。但"缓冲区进度"和"帧边界"是两套语义，那个位置跟帧结束毫无关系 —— 实测现象是**前 3 包成功、第 4 包起全崩**，因为 1024 恰好落在第 4 包中间（3 包 807B、4 包 1076B）。

本项目改用 `HAL_UART_Receive_DMA` 启动后**再关闭 HTIE/TCIE**，在 `USART2_IRQHandler` 里自己判 IDLE：

```c
len = OTA_RX_BUF_SIZE - DMA1_Channel6->CNDTR;   /* 反推这一帧收了多少字节 */
```

中断只置标志不做解析；主循环先 `memcpy` 快照、立刻重启 DMA、再慢慢解析，用双缓冲消除解析期的覆盖竞态。`HAL_DMA_IRQHandler` 保留 —— 传输错误中断 TE 是 HAL 无条件打开的，DMA 全局标志不清会导致中断风暴，只有它会清。

**2. 边收边写 Flash，不在 RAM 里拼包**

Bootloader 编译后 RAM 占用 5408 B，其中接收双缓冲就占 4096 B（75.7%）—— 64KB SRAM 根本装不下 240KB 镜像。所以接收端只维护一个写游标 `received`，每帧直接落到 `APP_B_ADDR + received`，落完就已经是完整镜像。

由此产生三条硬约束：顺序闸门（`seq == expect_seq` 才写）、重传幂等（重复包不写也不推进游标）、长度取自帧内字段而非常量（末包是 228B）。

**3. 复制到运行区，而不是双 Bank 指针切换**

App 的链接基址固定在 `0x08008000`（见 `stm32_app/MDK-ARM/app_demo/app_demo.sct`）。若直接跳到暂存区运行，中断向量表和所有绝对地址全部错位。双 Bank 切换要求固件位置无关或编译两份，复杂度高一个量级。

本项目让**运行地址始终唯一**：下载到暂存区 → 校验 → 复制到运行区 → 复位。

**4. 掉电保护：复制前先立标志**

复制的第一步是把 App A 整片擦成 `0xFF`，中途掉电会留下半个固件，而 `AppLooksValid()` 只看头 8 字节（栈顶 + 复位向量）很可能误判为有效。所以：

```c
config_write(&busy);              /* magic + ota_request=1 + CONFIG_FLAG_COPYING */
flash_copy_appb_to_appa(size);    /* 擦 A 整片 + 逐字复制 */
/* 回读 CRC 通过后才写最终 cfg，清标志 */
```

下次上电 `ShouldEnterDownload()` 见到标志就强制重进下载模式重来，绝不跳入半个固件。

---

## 三重校验

| 层级 | 时机 | 校验对象 |
|---|---|---|
| 帧级 CRC32 | 每帧进门 | `cmd + seq + payload` |
| 整包 CRC32 | `CMD_END` | 接收端流式累计值 vs 发送端值（CRC 线性性使分段累计等价于整体计算） |
| 落盘回读 | 复制完成后 | 从 `APP_A_ADDR` 回读 `fw_total` 字节重算 CRC |

---

## 目录结构

```
.
├── stm32_bootloader/        Keil 工程（Core + Drivers + MDK-ARM）
├── stm32_app/               Keil 工程，链接地址 0x08008000
├── esp32_downloader/        ESP-IDF 工程
│   └── main/ota_downloader.c
├── tools/                   merge_hex.py、测试固件 app_v11.bin
├── data_lab/               8 个配套练习（字节序/CRC32/组帧解析/DMA+IDLE 模拟/Flash 打包）
└── docs/                    操作手册、CubeMX 配置说明、协议编写步骤
```

---

## 快速开始

**1. 改配置**（`esp32_downloader/main/ota_downloader.c` 顶部三个宏）：

```c
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASS     "YOUR_WIFI_PASSWORD"
#define FIRMWARE_URL  "http://192.168.1.100:8000/app_v11.bin"
```

**2. 烧录 STM32**：先用 CubeProgrammer 烧 `bootloader.hex`（`0x08000000`），再用 Keil 打开 `stm32_app` 编译下载。

**3. 起 HTTP 服务**：

```bash
cd tools && python -m http.server 8000
```

**4. 进下载模式**：按住 KEY0 再复位 STM32（LED 200ms 慢闪即就绪）。

**5. 烧录 ESP32**：

```bash
cd esp32_downloader
build.cmd build
build.cmd flash COM11      # 改成你的串口号
```

完整步骤见 [`docs/STEPS.md`](docs/STEPS.md)。

---

## 实测数据

| 项目 | 数值 |
|---|---|
| 测试固件 | 5604 B / 22 包 |
| 传输阶段 | ~1 s（115200bps，含 22 次 ACK 往返） |
| 端到端 | 6～11 s |
| 瓶颈 | 两次整片 240KB 擦除（`CMD_START` 擦暂存区、`CMD_END` 擦运行区） |
| Bootloader 体积 | ROM 11460 B（预留 24KB），RAM 5408 B |

---

## 已知限制

诚实列出来，这些是下一步要做的事：

- **擦除粒度粗**：不管固件多大都擦满 240KB。改为按 `(size + 2047) / 2048` 擦页后，5604B 固件只需 3 页而非 120 页，端到端可进 1.5 s。
- **ESP32 不自动重试**：`CMD_DONE` 超时后 `app_main` 进入空转，掉电恢复需手动按 ESP32 的 RST。
- **配置区字段未接线**：`appA_valid / appA_size / appA_crc / appA_version` 目前只写不读，启动时不做固件完整性自检。
- **NACK 未生效**：ESP32 侧未处理 `CMD_NACK`，收到后仍等满 2 秒才重传。
- **无看门狗**：下载模式下无超时兜底。
- **未实现**：加密签名、差分升级、断点续传、版本回滚。

---

## License

MIT
