/**
 * @file    ota_downloader.c
 * @brief   ESP32-C3 OTA 下载器（ESP-IDF 原生版）
 *
 * 流程：
 *   1. 连 WiFi
 *   2. HTTP 流式下载固件（边下载边发送，不占大缓存）
 *   3. 按自定义协议分包发给 STM32（1KB/包，等 ACK，超时重传）
 *   4. 边发送边累计 CRC32，CMD_END 时双方比对
 *
 * 接线（与 Bootloader 的 USART2 对接）：
 *   ESP32 GPIO4 (TX) ──→ STM32 PA3 (USART2_RX)
 *   ESP32 GPIO5 (RX) ←── STM32 PA2 (USART2_TX)
 *   GND ──────────────── GND
 *
 * 引脚分配说明（重要）：
 *   ESP32-C3 的 UART0（串口控制台）默认占 GPIO20(TX)/GPIO21(RX)。
 *   本工程把 UART1 映射到 GPIO4/5，把 UART0 完整让给控制台日志（你熟悉的 COM11）。
 *   这样板子上任意一个 USB 口既能烧录也能看日志，不需要区分"USB/UART"两个口。
 *   杜邦线必须接到 GPIO4/5，而不是默认的 GPIO20/21。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_client.h"

#include "driver/uart.h"
#include "driver/gpio.h"

static const char *TAG = "OTA-DL";

/* ======================= 用户配置区（必改） ======================= */
#define WIFI_SSID     "YOUR_WIFI_SSID"                              /* ← 改成电脑所在的 WiFi 名称 */
#define WIFI_PASS     "YOUR_WIFI_PASSWORD"                       /* ← 改成对应密码            */
#define FIRMWARE_URL  "http://192.168.1.100:8000/app_v11.bin"
/*  ↑ 电脑 IP（之前查到的是 192.168.1.100，请按当前实际网络改成对应值）
    换网络后重新查：cmd 里执行 ipconfig，看「无线局域网适配器 WLAN」的 IPv4 地址。
    ⚠️ ESP32 和电脑必须连【同一个 WiFi】；若该 WiFi 需要网页认证（校园网/酒店网），
       ESP32 连不上 → 改用手机热点，并把电脑也连到该热点，再更新上面的 IP。 */
/* ================================================================= */

/* ---------- STM32 接线与协议参数 ---------- */
/* ESP32 ↔ STM32 通信串口
 * 默认走 UART_NUM_1（GPIO5/4），避开 UART0（GPIO21/20）—— UART0 要留给控制台日志
 * 接线：ESP32 GPIO5(RX) ← STM32 PA2(TX)
 *       ESP32 GPIO4(TX) → STM32 PA3(RX)
 */
#define PIN_STM32_RX     5         /* ESP32 RX ← STM32 PA2 (TX) */
#define PIN_STM32_TX     4         /* ESP32 TX → STM32 PA3 (RX) */
#define UART_PORT        UART_NUM_1
#define UART_STM32_BAUD  115200
// 数据包大小：与 STM32 端 OTA_PACKET_SIZE 保持一致
// 这里从 1024 降到 256，主要原因：
//   1) STM32 端用 DMA+空闲中断接收，115200 波特率下 1024 字节帧(总 1037B)
//      在 50ms 空闲窗口内不一定能攒齐一整帧，容易触发"半帧空闲"导致 CRC 失败
//   2) 256 字节帧总长度仅 269B，DMA 一次性攒齐概率大幅提升
//   3) 总包数从 ~6 包变成 ~22 包，单包耗时更短，整体升级时间仍可接受
#define PACKET_SIZE      256
#define ACK_TIMEOUT_MS   2000
#define MAX_RETRY        5
#define BOOT_WAIT_MS     15000     /* 上电后给用户时间让 STM32 进下载模式（WiFi 连接后开始算）*/
#define BOOT_PROBE_MS    3000      /* 在 BOOT_WAIT_MS 期间，每隔这么多 ms 发一次 CMD_GET_INFO 试探 */
#define BOOT_RETRY_MS    10000     /* 15s 没回应，再延长等这么多 ms（期间继续每 3s 试探）*/

/* ---------- 协议常量（与 ota_config.h 一致） ---------- */
#define CMD_START        0x01
#define CMD_DATA         0x02
#define CMD_ACK          0x03
#define CMD_NACK         0x04
#define CMD_END          0x05
#define CMD_DONE         0x06
#define CMD_GET_INFO     0x07
#define CMD_INFO         0x08
#define ACK_OK           0x00
#define ACK_ERR          0x01

/* ======================= 小工具 ======================= */

/* 毫秒级时间戳（开机起算） */
static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ======================= CRC32（与 STM32 一致） ======================= */
static const uint32_t s_crc_table[256] = {
    0x00000000UL, 0x77073096UL, 0xEE0E612CUL, 0x990951BAUL, 0x076DC419UL, 0x706AF48FUL, 0xE963A535UL, 0x9E6495A3UL,
    0x0EDB8832UL, 0x79DCB8A4UL, 0xE0D5E91EUL, 0x97D2D988UL, 0x09B64C2BUL, 0x7EB17CBDUL, 0xE7B82D07UL, 0x90BF1D91UL,
    0x1DB71064UL, 0x6AB020F2UL, 0xF3B97148UL, 0x84BE41DEUL, 0x1ADAD47DUL, 0x6DDDE4EBUL, 0xF4D4B551UL, 0x83D385C7UL,
    0x136C9856UL, 0x646BA8C0UL, 0xFD62F97AUL, 0x8A65C9ECUL, 0x14015C4FUL, 0x63066CD9UL, 0xFA0F3D63UL, 0x8D080DF5UL,
    0x3B6E20C8UL, 0x4C69105EUL, 0xD56041E4UL, 0xA2677172UL, 0x3C03E4D1UL, 0x4B04D447UL, 0xD20D85FDUL, 0xA50AB56BUL,
    0x35B5A8FAUL, 0x42B2986CUL, 0xDBBBC9D6UL, 0xACBCF940UL, 0x32D86CE3UL, 0x45DF5C75UL, 0xDCD60DCFUL, 0xABD13D59UL,
    0x26D930ACUL, 0x51DE003AUL, 0xC8D75180UL, 0xBFD06116UL, 0x21B4F4B5UL, 0x56B3C423UL, 0xCFBA9599UL, 0xB8BDA50FUL,
    0x2802B89EUL, 0x5F058808UL, 0xC60CD9B2UL, 0xB10BE924UL, 0x2F6F7C87UL, 0x58684C11UL, 0xC1611DABUL, 0xB6662D3DUL,
    0x76DC4190UL, 0x01DB7106UL, 0x98D220BCUL, 0xEFD5102AUL, 0x71B18589UL, 0x06B6B51FUL, 0x9FBFE4A5UL, 0xE8B8D433UL,
    0x7807C9A2UL, 0x0F00F934UL, 0x9609A88EUL, 0xE10E9818UL, 0x7F6A0DBBUL, 0x086D3D2DUL, 0x91646C97UL, 0xE6635C01UL,
    0x6B6B51F4UL, 0x1C6C6162UL, 0x856530D8UL, 0xF262004EUL, 0x6C0695EDUL, 0x1B01A57BUL, 0x8208F4C1UL, 0xF50FC457UL,
    0x65B0D9C6UL, 0x12B7E950UL, 0x8BBEB8EAUL, 0xFCB9887CUL, 0x62DD1DDFUL, 0x15DA2D49UL, 0x8CD37CF3UL, 0xFBD44C65UL,
    0x4DB26158UL, 0x3AB551CEUL, 0xA3BC0074UL, 0xD4BB30E2UL, 0x4ADFA541UL, 0x3DD895D7UL, 0xA4D1C46DUL, 0xD3D6F4FBUL,
    0x4369E96AUL, 0x346ED9FCUL, 0xAD678846UL, 0xDA60B8D0UL, 0x44042D73UL, 0x33031DE5UL, 0xAA0A4C5FUL, 0xDD0D7CC9UL,
    0x5005713CUL, 0x270241AAUL, 0xBE0B1010UL, 0xC90C2086UL, 0x5768B525UL, 0x206F85B3UL, 0xB966D409UL, 0xCE61E49FUL,
    0x5EDEF90EUL, 0x29D9C998UL, 0xB0D09822UL, 0xC7D7A8B4UL, 0x59B33D17UL, 0x2EB40D81UL, 0xB7BD5C3BUL, 0xC0BA6CADUL,
    0xEDB88320UL, 0x9ABFB3B6UL, 0x03B6E20CUL, 0x74B1D29AUL, 0xEAD54739UL, 0x9DD277AFUL, 0x04DB2615UL, 0x73DC1683UL,
    0xE3630B12UL, 0x94643B84UL, 0x0D6D6A3EUL, 0x7A6A5AA8UL, 0xE40ECF0BUL, 0x9309FF9DUL, 0x0A00AE27UL, 0x7D079EB1UL,
    0xF00F9344UL, 0x8708A3D2UL, 0x1E01F268UL, 0x6906C2FEUL, 0xF762575DUL, 0x806567CBUL, 0x196C3671UL, 0x6E6B06E7UL,
    0xFED41B76UL, 0x89D32BE0UL, 0x10DA7A5AUL, 0x67DD4ACCUL, 0xF9B9DF6FUL, 0x8EBEEFF9UL, 0x17B7BE43UL, 0x60B08ED5UL,
    0xD6D6A3E8UL, 0xA1D1937EUL, 0x38D8C2C4UL, 0x4FDFF252UL, 0xD1BB67F1UL, 0xA6BC5767UL, 0x3FB506DDUL, 0x48B2364BUL,
    0xD80D2BDAUL, 0xAF0A1B4CUL, 0x36034AF6UL, 0x41047A60UL, 0xDF60EFC3UL, 0xA867DF55UL, 0x316E8EEFUL, 0x4669BE79UL,
    0xCB61B38CUL, 0xBC66831AUL, 0x256FD2A0UL, 0x5268E236UL, 0xCC0C7795UL, 0xBB0B4703UL, 0x220216B9UL, 0x5505262FUL,
    0xC5BA3BBEUL, 0xB2BD0B28UL, 0x2BB45A92UL, 0x5CB36A04UL, 0xC2D7FFA7UL, 0xB5D0CF31UL, 0x2CD99E8BUL, 0x5BDEAE1DUL,
    0x9B64C2B0UL, 0xEC63F226UL, 0x756AA39CUL, 0x026D930AUL, 0x9C0906A9UL, 0xEB0E363FUL, 0x72076785UL, 0x05005713UL,
    0x95BF4A82UL, 0xE2B87A14UL, 0x7BB12BAEUL, 0x0CB61B38UL, 0x92D28E9BUL, 0xE5D5BE0DUL, 0x7CDCEFB7UL, 0x0BDBDF21UL,
    0x86D3D2D4UL, 0xF1D4E242UL, 0x68DDB3F8UL, 0x1FDA836EUL, 0x81BE16CDUL, 0xF6B9265BUL, 0x6FB077E1UL, 0x18B74777UL,
    0x88085AE6UL, 0xFF0F6A70UL, 0x66063BCAUL, 0x11010B5CUL, 0x8F659EFFUL, 0xF862AE69UL, 0x616BFFD3UL, 0x166CCF45UL,
    0xA00AE278UL, 0xD70DD2EEUL, 0x4E048354UL, 0x3903B3C2UL, 0xA7672661UL, 0xD06016F7UL, 0x4969474DUL, 0x3E6E77DBUL,
    0xAED16A4AUL, 0xD9D65ADCUL, 0x40DF0B66UL, 0x37D83BF0UL, 0xA9BCAE53UL, 0xDEBB9EC5UL, 0x47B2CF7FUL, 0x30B5FFE9UL,
    0xBDBDF21CUL, 0xCABAC28AUL, 0x53B39330UL, 0x24B4A3A6UL, 0xBAD03605UL, 0xCDD70693UL, 0x54DE5729UL, 0x23D967BFUL,
    0xB3667A2EUL, 0xC4614AB8UL, 0x5D681B02UL, 0x2A6F2B94UL, 0xB40BBE37UL, 0xC30C8EA1UL, 0x5A05DF1BUL, 0x2D02EF8DUL
};

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    while (len--) {
        crc = (crc >> 8) ^ s_crc_table[(crc ^ *data++) & 0xFFUL];
    }
    return ~crc;
}

/* ======================= UART（与 STM32 通信） ======================= */

static void uart_init_stm32(void)
{
    const uart_config_t uc = {
        .baud_rate  = UART_STM32_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 4096, 4096, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uc));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, PIN_STM32_TX, PIN_STM32_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

/* ======================= 协议帧收发 ======================= */

/* 发送一帧（阻塞发送，等 TX 完成） */
static void send_frame(uint8_t cmd, uint16_t seq, const uint8_t *payload, uint16_t plen)
{
    static uint8_t out[PACKET_SIZE + 32];
    uint16_t flen = (uint16_t)(3 + plen);

    out[0] = 0xAA; out[1] = 0x55;
    out[2] = (uint8_t)(flen & 0xFF);
    out[3] = (uint8_t)(flen >> 8);
    out[4] = cmd;
    out[5] = (uint8_t)(seq & 0xFF);
    out[6] = (uint8_t)(seq >> 8);
    if (plen > 0 && payload) {
        memcpy(&out[7], payload, plen);
    }
    uint32_t crc = crc32_update(0, &out[4], flen);
    out[7 + plen]  = (uint8_t)(crc & 0xFF);
    out[8 + plen]  = (uint8_t)((crc >> 8)  & 0xFF);
    out[9 + plen]  = (uint8_t)((crc >> 16) & 0xFF);
    out[10 + plen] = (uint8_t)((crc >> 24) & 0xFF);
    out[11 + plen] = 0x0D;
    out[12 + plen] = 0x0A;

    uart_write_bytes(UART_PORT, (const char *)out, (size_t)(13 + plen));
    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(1000));
}

/* 帧解析结果 */
typedef struct {
    uint8_t  cmd;
    uint16_t seq;
    uint16_t plen;
    uint8_t  payload[64];
} parsed_frame_t;

/* 从 UART1 读一帧（带超时，滑动找帧头） */
static bool read_frame(parsed_frame_t *fr, uint32_t timeout_ms)
{
    uint8_t  buf[128];
    uint32_t n  = 0;
    uint32_t t0 = now_ms();

    while ((uint32_t)(now_ms() - t0) < timeout_ms) {
        uint8_t ch;
        /* 一次读 1 字节；有数据立刻返回，没数据最多等 5ms */
        if (uart_read_bytes(UART_PORT, &ch, 1, pdMS_TO_TICKS(5)) != 1) {
            continue;
        }
        buf[n++] = ch;

        if (n < 13) {
            continue;
        }

        /* 帧头不对 → 整体左移一格，继续找 0xAA */
        if (buf[0] != 0xAA || buf[1] != 0x55) {
            memmove(buf, buf + 1, --n);
            continue;
        }

        uint16_t flen  = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8));
        uint32_t total = (uint32_t)(2 + 2 + flen + 4 + 2);
        if (flen < 3 || total > sizeof(buf)) {
            memmove(buf, buf + 1, --n);
            continue;
        }
        if (n < total) {
            continue;               /* 还没收够，继续读 */
        }

        uint32_t crc_calc = crc32_update(0, &buf[4], flen);
        uint32_t crc_recv = (uint32_t)buf[4 + flen]
                          | ((uint32_t)buf[5 + flen] << 8)
                          | ((uint32_t)buf[6 + flen] << 16)
                          | ((uint32_t)buf[7 + flen] << 24);

        if (crc_calc == crc_recv && buf[8 + flen] == 0x0D && buf[9 + flen] == 0x0A) {
            fr->cmd  = buf[4];
            fr->seq  = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
            fr->plen = (uint16_t)(flen - 3);
            uint16_t cp = (fr->plen > sizeof(fr->payload)) ? sizeof(fr->payload) : fr->plen;
            memcpy(fr->payload, &buf[7], cp);
            return true;
        }

        /* CRC 错 → 丢弃这帧重来 */
        memmove(buf, buf + 1, --n);
    }
    return false;
}

/* 发送并等 ACK（带重传）。返回 ACK 状态，-1 失败 */
static int send_and_wait_ack(uint8_t cmd, uint16_t seq, const uint8_t *payload, uint16_t plen)
{
    for (int retry = 0; retry < MAX_RETRY; retry++) {
        send_frame(cmd, seq, payload, plen);

        parsed_frame_t fr;
        if (read_frame(&fr, ACK_TIMEOUT_MS) && fr.cmd == CMD_ACK) {
            uint16_t ack_seq = (uint16_t)(fr.payload[0] | ((uint16_t)fr.payload[1] << 8));
            uint8_t  status  = fr.payload[2];
            if (ack_seq == seq) {
                return status;
            }
            /* ACK 序号不匹配（旧 ACK 迟到）→ 重发 */
        }
        ESP_LOGW(TAG, "[RETRY] cmd=0x%02X seq=%u try %d/%d",
                 (unsigned)cmd, (unsigned)seq, retry + 1, MAX_RETRY);
    }
    return -1;
}

/* ======================= WiFi ======================= */

#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t s_wifi_eg;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi OK, IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_init_sta(void)
{
    s_wifi_eg = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid,     WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password) - 1);
    /* 接受任意加密方式（含开放、WPA2、WPA3），避免因阈值卡住连不上 */
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting WiFi: %s ...", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/* ======================= HTTP 流式下载 + 转发 ======================= */

static bool run_ota(void)
{
    /* ---- 打开 HTTP 连接 ---- */
    ESP_LOGI(TAG, "Downloading: %s", FIRMWARE_URL);

    esp_http_client_config_t http_cfg = {
        .url                = FIRMWARE_URL,
        .timeout_ms         = 15000,
        .keep_alive_enable  = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return false;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP connect failed (check IP / server running)");
        esp_http_client_cleanup(client);
        return false;
    }

    /* ⚠️ 顺序很重要：必须先 fetch_headers() 把响应头读进来，
     * 再调 get_status_code() 才有效。
     * 反过来写的话 get_status_code() 会返回 0（头还没读），
     * 于是明明 HTTP 是 200 也被判成 "Bad HTTP response"。
     * （第一次踩坑现场：status=0 但 size=5604 —— size 正确恰恰证明响应是好的）*/
    int64_t content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status=%d, size=%lld", status, (long long)content_len);

    /* size <= 0 才是真失败；status 允许为空(0) —— 有些简易服务器不返回状态行 */
    if (content_len <= 0) {
        ESP_LOGE(TAG, "Bad HTTP response. status=%d size=%lld",
                 status, (long long)content_len);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP status=%d (not 200), but size=%lld looks OK -> continue",
                 status, (long long)content_len);
    }

    uint32_t fw_size    = (uint32_t)content_len;
    uint16_t total_pkts = (uint16_t)((fw_size + PACKET_SIZE - 1) / PACKET_SIZE);

    /* ---- CMD_START：把固件大小和包数告诉 STM32 ---- */
    uint8_t start_pl[6];
    start_pl[0] = (uint8_t)(fw_size & 0xFF);
    start_pl[1] = (uint8_t)((fw_size >> 8) & 0xFF);
    start_pl[2] = (uint8_t)((fw_size >> 16) & 0xFF);
    start_pl[3] = (uint8_t)((fw_size >> 24) & 0xFF);
    start_pl[4] = (uint8_t)(total_pkts & 0xFF);
    start_pl[5] = (uint8_t)(total_pkts >> 8);

    if (send_and_wait_ack(CMD_START, 0, start_pl, 6) != ACK_OK) {
        ESP_LOGE(TAG, "CMD_START not acked. Is STM32 in bootloader mode?");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    ESP_LOGI(TAG, "OTA session started (%u packets, %lu bytes)",
             (unsigned)total_pkts, (unsigned long)fw_size);

    /* ---- 流式分包发送 ---- */
    uint8_t *pkt    = malloc(PACKET_SIZE);
    if (pkt == NULL) {
        ESP_LOGE(TAG, "malloc failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    uint32_t sent         = 0;
    uint16_t seq          = 0;
    uint32_t crc          = 0;
    uint32_t last_progress = 0;
    bool     ok           = true;

    while (sent < fw_size) {
        int want = (int)((fw_size - sent > PACKET_SIZE) ? PACKET_SIZE : (fw_size - sent));

        /* 从 HTTP 流里凑满 want 字节 */
        int     got    = 0;
        uint32_t t_read = now_ms();
        while (got < want) {
            int rd = esp_http_client_read(client, (char *)pkt + got, want - got);
            if (rd > 0) {
                got += rd;
                t_read = now_ms();
            } else if (esp_http_client_is_complete_data_received(client)) {
                break;                          /* 流结束 */
            } else if ((uint32_t)(now_ms() - t_read) > 10000) {
                ESP_LOGE(TAG, "HTTP stream timeout at %lu/%lu",
                         (unsigned long)sent, (unsigned long)fw_size);
                ok = false;
                break;
            } else {
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
        if (!ok || got <= 0) {
            break;
        }

        /* 边发边累计 CRC */
        crc = crc32_update(crc, pkt, (size_t)got);

        if (send_and_wait_ack(CMD_DATA, seq, pkt, (uint16_t)got) != ACK_OK) {
            ESP_LOGE(TAG, "Packet %u failed after %d retries. ABORT.",
                     (unsigned)seq, MAX_RETRY);
            ok = false;
            break;
        }

        sent += (uint32_t)got;
        seq++;

        /* 进度：每 10% 打一行 */
        if (sent - last_progress >= fw_size / 10 || sent >= fw_size) {
            last_progress = sent;
            ESP_LOGI(TAG, "Progress: %lu/%lu (%lu%%)",
                     (unsigned long)sent, (unsigned long)fw_size,
                     (unsigned long)((uint64_t)sent * 100 / fw_size));
        }
    }

    free(pkt);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!ok) {
        return false;
    }

    ESP_LOGI(TAG, "All packets sent. CRC32 = 0x%08lX", (unsigned long)crc);

    /* ---- CMD_END：带上累计 CRC，让 STM32 校验 ---- */
    uint8_t end_pl[4];
    end_pl[0] = (uint8_t)(crc & 0xFF);
    end_pl[1] = (uint8_t)((crc >> 8) & 0xFF);
    end_pl[2] = (uint8_t)((crc >> 16) & 0xFF);
    end_pl[3] = (uint8_t)((crc >> 24) & 0xFF);
    send_frame(CMD_END, 0, end_pl, 4);

    parsed_frame_t fr;
    if (read_frame(&fr, 5000) && fr.cmd == CMD_DONE) {
        if (fr.payload[0] == ACK_OK) {
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "  OTA SUCCESS! STM32 rebooting to new app");
            ESP_LOGI(TAG, "========================================");
            return true;
        }
        ESP_LOGE(TAG, "OTA FAILED: CRC mismatch on STM32 side");
        return false;
    }

    ESP_LOGW(TAG, "No CMD_DONE received (STM32 may still be busy)");
    return false;
}

/* ======================= 入口 ======================= */

void app_main(void)
{
    /* 0. NVS（WiFi 驱动依赖它存 PHY 校准数据） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 1. UART1 → STM32 */
    uart_init_stm32();

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "=== ESP32-C3 OTA Downloader (ESP-IDF) ===");

    /* 2. 连 WiFi */
    if (!wifi_init_sta()) {
        ESP_LOGE(TAG, "WiFi connect FAILED. Check SSID/PASS. Rebooting in 5s...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    /* 3. 等 STM32 进入下载模式
     *
     * Bootloader 进入下载模式后会立刻把 LED 切成 200ms 慢闪，但它不主动往串口发任何东西。
     * 所以这里用"周期性发送 CMD_GET_INFO + 读回 CMD_INFO"来探测：
     *   - 收到 → STM32 已就绪，立即开始 OTA
     *   - 超时 → 继续等下一轮，避免 STM32 还没复位就被超时甩掉
     */
    ESP_LOGI(TAG, "--------------------------------------------");
    ESP_LOGI(TAG, "Make sure STM32 is in BOOTLOADER mode:");
    ESP_LOGI(TAG, "  press RST, then press KEY0(PE4) within 3s");
    ESP_LOGI(TAG, "  (LED0 blinking slowly 200ms = ready)");
    ESP_LOGI(TAG, "--------------------------------------------");

    uint32_t wait_start = now_ms();
    uint32_t last_probe = 0;
    bool     got_reply  = false;

    while ((uint32_t)(now_ms() - wait_start) < BOOT_WAIT_MS + BOOT_RETRY_MS) {
        if ((uint32_t)(now_ms() - last_probe) >= BOOT_PROBE_MS) {
            last_probe = now_ms();
            ESP_LOGI(TAG, "[probe] sending CMD_GET_INFO ...");
            send_frame(CMD_GET_INFO, 0, NULL, 0);
        }

        parsed_frame_t fr;
        if (read_frame(&fr, 200)) {
            if (fr.cmd == CMD_INFO) {
                char ver[33] = {0};
                uint16_t cp = (fr.plen > 32) ? 32 : fr.plen;
                memcpy(ver, fr.payload, cp);
                ESP_LOGI(TAG, "STM32 confirmed in bootloader mode (got '%s')", ver);
                got_reply = true;
                break;
            }
            /* 探测期间也可能意外收到别的命令帧（噪声） → 直接丢弃 */
        }
    }

    if (!got_reply) {
        ESP_LOGE(TAG, "STM32 never responded to CMD_GET_INFO within %u ms",
                 (unsigned)(BOOT_WAIT_MS + BOOT_RETRY_MS));
        ESP_LOGE(TAG, "Check: 1) STM32 in download mode?  2) dupont wires on GPIO4/5?  3) baud 115200?");
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }

    /* 4. 下载并转发 */
    run_ota();

    /* 5. 一次性任务，之后空闲。要再升一次，按板上 RST 键复位 ESP32 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
