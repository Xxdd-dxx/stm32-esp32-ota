/**
 * @file flash_if.h
 * @brief Flash 分区操作：擦除 / 按字写入 / App B→A 复制 / 配置区写入
 */
#ifndef FLASH_IF_H
#define FLASH_IF_H

#include "ota_config.h"

/* 擦除指定地址区间的页（F1 页大小 2KB） */
int flash_erase_region(uint32_t addr, uint32_t size);

/**
 * @brief  从任意字节地址写入数据（内部按 4 字节对齐补 0xFF）
 * @note   dst 必须落在可编程区；len 不足 4 的倍数时末尾补 0xFF
 */
int flash_write_bytes(uint32_t dst, const uint8_t *src, uint32_t len);

/* 将 App B 区固件复制到 App A 区（先擦后拷），size 为固件字节数 */
int flash_copy_appb_to_appa(uint32_t size);

/* 写入 System Config 区（擦除整块后写入） */
int config_write(const SystemConfig_t *cfg);

#endif /* FLASH_IF_H */
