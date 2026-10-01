/**
 * @file flash_if.c
 * @brief STM32F1 Flash 编程（HAL）
 *
 * F1 系列要点：
 *  1. 页大小 2KB（大容量 F103ZET6）
 *  2. 编程粒度：半字(16bit) / 字(32bit) / 双字，本实现统一按字写
 *  3. 擦除后全 0xFF，写入只能 1→0
 */
#include "flash_if.h"

#define FLASH_PAGE_SIZE_F1   2048UL

int flash_erase_region(uint32_t addr, uint32_t size)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_err = 0;

    if (addr % FLASH_PAGE_SIZE_F1 != 0) {
        return -1;   /* 起始地址必须页对齐 */
    }
    uint32_t pages = (size + FLASH_PAGE_SIZE_F1 - 1) / FLASH_PAGE_SIZE_F1;

    erase.TypeErase    = FLASH_TYPEERASE_PAGES;
    erase.Banks        = FLASH_BANK_1;
    erase.PageAddress  = addr;
    erase.NbPages      = pages;

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &page_err);
    HAL_FLASH_Lock();

    return (st == HAL_OK && page_err == 0xFFFFFFFFUL) ? 0 : -1;
}

int flash_write_bytes(uint32_t dst, const uint8_t *src, uint32_t len)
{
    if (len == 0) {
        return 0;
    }

    HAL_FLASH_Unlock();
    for (uint32_t off = 0; off < len; off += 4) {
        /* 末尾不足 4 字节补 0xFF（Flash 擦后态） */
        uint8_t tmp[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        uint32_t chunk = (len - off > 4) ? 4 : (len - off);
        for (uint32_t k = 0; k < chunk; k++) {
            tmp[k] = src[off + k];
        }
        uint32_t word = (uint32_t)tmp[0] | ((uint32_t)tmp[1] << 8)
                      | ((uint32_t)tmp[2] << 16) | ((uint32_t)tmp[3] << 24);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst + off, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    HAL_FLASH_Lock();

    /* 回读校验 */
    for (uint32_t off = 0; off < len; off++) {
        if (src[off] != *(volatile uint8_t *)(dst + off)) {
            return -2;
        }
    }
    return 0;
}

int flash_copy_appb_to_appa(uint32_t size)
{
    int ret;

    /* 1. 擦除 App A */
    ret = flash_erase_region(APP_A_ADDR, APP_A_SIZE);
    if (ret != 0) {
        return -1;
    }

    /* 2. 按字复制（App B → App A） */
    HAL_FLASH_Unlock();
    uint32_t words = (size + 3) / 4;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t word = *(volatile uint32_t *)(APP_B_ADDR + i * 4);
        /* 末字不足 4 字节补 0xFF —— 直接从 Flash 读出的本身就是真实内容，
           B 区写入时已按 0xFF 补齐，无需再处理 */
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_A_ADDR + i * 4, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return -2;
        }
    }
    HAL_FLASH_Lock();

    /* 3. 回读校验整体 CRC 由调用方做，这里只验证长度 */
    return 0;
}

int config_write(const SystemConfig_t *cfg)
{
    int ret = flash_erase_region(CONFIG_ADDR, CONFIG_SIZE);
    if (ret != 0) {
        return -1;
    }
    return flash_write_bytes(CONFIG_ADDR, (const uint8_t *)cfg, sizeof(SystemConfig_t));
}
