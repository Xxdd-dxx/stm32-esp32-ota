# -*- coding: utf-8 -*-
"""
data_lab 自测脚本（只给我自己用，验证脚手架本身没写错）。
把每个练习的 TODO 填上参考实现，编译运行，检查是否 ALL PASS。
生成的文件都是 _t_ 开头的临时文件，跑完自动删。

用法： python _selftest.py
"""
import io
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

PATCHES = {
    "ex01_byteorder.c": [
        ("void put_u16(uint8_t *p, uint16_t v)\n{\n    /* 你的代码 */\n    (void)p; (void)v;\n}",
         "void put_u16(uint8_t *p, uint16_t v)\n{\n    p[0] = (uint8_t)(v & 0xFF);\n    p[1] = (uint8_t)(v >> 8);\n}"),
        ("uint16_t get_u16(const uint8_t *p)\n{\n    /* 你的代码 */\n    (void)p;\n    return 0;\n}",
         "uint16_t get_u16(const uint8_t *p)\n{\n    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));\n}"),
        ("void put_u32(uint8_t *p, uint32_t v)\n{\n    /* 你的代码 */\n    (void)p; (void)v;\n}",
         "void put_u32(uint8_t *p, uint32_t v)\n{\n    p[0]=(uint8_t)(v & 0xFF); p[1]=(uint8_t)((v>>8)&0xFF);\n    p[2]=(uint8_t)((v>>16)&0xFF); p[3]=(uint8_t)((v>>24)&0xFF);\n}"),
        ("uint32_t get_u32(const uint8_t *p)\n{\n    /* 你的代码 */\n    (void)p;\n    return 0;\n}",
         "uint32_t get_u32(const uint8_t *p)\n{\n    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)\n         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);\n}"),
    ],
    "ex02_crc32.c": [
        ("uint32_t crc32_bitwise(uint32_t crc, const uint8_t *d, size_t n)\n{\n    /* 你的代码 */\n    (void)crc; (void)d; (void)n;\n    return 0;\n}",
         "uint32_t crc32_bitwise(uint32_t crc, const uint8_t *d, size_t n)\n{\n"
         "    crc = ~crc;\n"
         "    while (n--) {\n"
         "        crc ^= *d++;\n"
         "        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (POLY & (uint32_t)(-(int32_t)(crc & 1)));\n"
         "    }\n"
         "    return ~crc;\n}"),
        ("void crc32_build_table(uint32_t *tbl)\n{\n    /* 你的代码 */\n    (void)tbl;\n}",
         "void crc32_build_table(uint32_t *tbl)\n{\n"
         "    for (uint32_t i = 0; i < 256; i++) {\n"
         "        uint32_t c = i;\n"
         "        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (POLY & (uint32_t)(-(int32_t)(c & 1)));\n"
         "        tbl[i] = c;\n"
         "    }\n}"),
        ("    /* 你的代码（用上面那个 tbl） */\n    (void)crc; (void)d; (void)n;\n    return 0;\n}",
         "    crc = ~crc;\n    while (n--) crc = (crc >> 8) ^ tbl[(crc ^ *d++) & 0xFFu];\n    return ~crc;\n}"),
    ],
    "ex03_frame_build.c": [
        ("size_t frame_build(uint8_t *out, size_t out_cap,\n                   uint8_t cmd, uint16_t seq,\n                   const uint8_t *payload, uint16_t plen)\n{\n    /* 你的代码 */\n    (void)out; (void)out_cap; (void)cmd; (void)seq; (void)payload; (void)plen;\n    return 0;\n}",
         "size_t frame_build(uint8_t *out, size_t out_cap,\n                   uint8_t cmd, uint16_t seq,\n                   const uint8_t *payload, uint16_t plen)\n{\n"
         "    if (!out) return 0;\n"
         "    if (plen > MAX_PAYLOAD) return 0;\n"
         "    size_t total = (size_t)FRAME_OVERHEAD + plen;\n"
         "    if (out_cap < total) return 0;\n"
         "    uint16_t flen = (uint16_t)(3 + plen);\n"
         "    out[0] = 0xAA; out[1] = 0x55;\n"
         "    out[2] = (uint8_t)(flen & 0xFF);\n"
         "    out[3] = (uint8_t)(flen >> 8);\n"
         "    out[4] = cmd;\n"
         "    out[5] = (uint8_t)(seq & 0xFF);\n"
         "    out[6] = (uint8_t)(seq >> 8);\n"
         "    if (plen && payload) memcpy(&out[7], payload, plen);\n"
         "    uint32_t crc = crc32_fast(0, &out[4], flen);\n"
         "    out[7 + plen]  = (uint8_t)(crc & 0xFF);\n"
         "    out[8 + plen]  = (uint8_t)((crc >> 8) & 0xFF);\n"
         "    out[9 + plen]  = (uint8_t)((crc >> 16) & 0xFF);\n"
         "    out[10 + plen] = (uint8_t)((crc >> 24) & 0xFF);\n"
         "    out[11 + plen] = 0x0D;\n"
         "    out[12 + plen] = 0x0A;\n"
         "    return total;\n}"),
    ],
    "ex04_frame_parse.c": [
        ("int frame_parse(const uint8_t *buf, size_t len, OtaFrame_t *out)\n{\n    /* 你的代码 */\n    (void)buf; (void)len; (void)out;\n    return ERR_NULL;\n}",
         "int frame_parse(const uint8_t *buf, size_t len, OtaFrame_t *out)\n{\n"
         "    if (!buf || !out) return ERR_NULL;\n"
         "    if (len < 13) return ERR_SHORT;\n"
         "    if (buf[0] != 0xAA || buf[1] != 0x55) return ERR_HEADER;\n"
         "    uint16_t flen = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8));\n"
         "    if (flen < 3) return ERR_LEN;\n"
         "    if ((size_t)10 + flen > len) return ERR_LEN;\n"
         "    uint32_t calc = crc32_fast(0, &buf[4], flen);\n"
         "    uint32_t recv = (uint32_t)buf[4 + flen] | ((uint32_t)buf[5 + flen] << 8)\n"
         "                  | ((uint32_t)buf[6 + flen] << 16) | ((uint32_t)buf[7 + flen] << 24);\n"
         "    if (calc != recv) return ERR_CRC;\n"
         "    if (buf[8 + flen] != 0x0D || buf[9 + flen] != 0x0A) return ERR_TAIL;\n"
         "    out->cmd         = buf[4];\n"
         "    out->seq         = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));\n"
         "    out->payload     = &buf[7];\n"
         "    out->payload_len = (uint16_t)(flen - 3);\n"
         "    return FRAME_OK;\n}"),
    ],
    "ex05_framer_sm.c": [
        ("int framer_feed(uint8_t b)\n{\n    /* 你的代码 */\n    (void)b;\n    return FRAMER_MORE;\n}",
         "int framer_feed(uint8_t b)\n{\n"
         "    if (s_total == 0) {\n"
         "        if (s_n == 0) {\n"
         "            if (b == 0xAA) s_buf[s_n++] = b;\n"
         "            return FRAMER_MORE;\n"
         "        }\n"
         "        if (s_n == 1) {\n"
         "            if (b == 0x55)      s_buf[s_n++] = b;\n"
         "            else if (b == 0xAA) s_buf[0] = 0xAA;\n"
         "            else                s_n = 0;\n"
         "            return FRAMER_MORE;\n"
         "        }\n"
         "        s_buf[s_n++] = b;\n"
         "        if (s_n < 4) return FRAMER_MORE;\n"
         "        uint16_t flen = (uint16_t)(s_buf[2] | ((uint16_t)s_buf[3] << 8));\n"
         "        if (flen < 3 || flen > (FRAMER_BUF_SIZE - 10)) { s_n = 0; return FRAMER_BAD_LEN; }\n"
         "        s_total = (uint32_t)(10 + flen);\n"
         "        return FRAMER_MORE;\n"
         "    }\n"
         "    s_buf[s_n++] = b;\n"
         "    if (s_n < s_total) return FRAMER_MORE;\n"
         "    uint16_t flen = (uint16_t)(s_total - 10);\n"
         "    uint32_t calc = crc32_fast(0, &s_buf[4], flen);\n"
         "    uint32_t recv = (uint32_t)s_buf[4+flen] | ((uint32_t)s_buf[5+flen] << 8)\n"
         "                  | ((uint32_t)s_buf[6+flen] << 16) | ((uint32_t)s_buf[7+flen] << 24);\n"
         "    int rc;\n"
         "    if (calc != recv) rc = FRAMER_BAD_CRC;\n"
         "    else if (s_buf[8+flen] != 0x0D || s_buf[9+flen] != 0x0A) rc = FRAMER_BAD_TAIL;\n"
         "    else rc = FRAMER_FRAME;\n"
         "    if (rc != FRAMER_FRAME) { s_n = 0; s_total = 0; }\n"
         "    return rc;\n}"),
        ("void framer_reset(void)\n{\n    /* 你的代码：s_n = 0; s_total = 0; */\n}",
         "void framer_reset(void)\n{\n    s_n = 0;\n    s_total = 0;\n}"),
    ],
    "ex06_dma_idle_sim.c": [
        ("static void on_ht_event(void)\n{\n    /* 你的代码（一行） */\n}",
         "static void on_ht_event(void)\n{\n    deliver(g_rx, hw_received());\n}"),
        ("static void on_idle_event(void)\n{\n    /* 你的代码 */\n}",
         "static void on_idle_event(void)\n{\n"
         "    deliver(g_rx, hw_received());\n"
         "    if (g_rx->restart_per_frame) {\n"
         "        hw_dma_start();\n"
         "        g_rx->last_delivered = 0;\n"
         "    }\n}"),
    ],
    "ex07_volatile.c": [
        ("int wait_plain(void)\n{\n    /* 你的代码 */\n    return 0;\n}",
         "int wait_plain(void)\n{\n    while (!g_flag_plain) { }\n    return g_flag_plain;\n}"),
        ("int wait_vol(void)\n{\n    /* 你的代码 */\n    return 0;\n}",
         "int wait_vol(void)\n{\n    while (!g_flag_vol) { }\n    return g_flag_vol;\n}"),
    ],
    "ex08_flash_pack.c": [
        ("size_t pack_words(const uint8_t *src, size_t len, uint32_t *dst)\n{\n    /* 你的代码 */\n    (void)src; (void)len; (void)dst;\n    return 0;\n}",
         "size_t pack_words(const uint8_t *src, size_t len, uint32_t *dst)\n{\n"
         "    if (len == 0) return 0;\n"
         "    if (!src || !dst) return 0;\n"
         "    size_t words = (len + 3) / 4;\n"
         "    for (size_t i = 0; i < words; i++) {\n"
         "        uint8_t t[4] = {0xFF, 0xFF, 0xFF, 0xFF};\n"
         "        size_t chunk = (len - i * 4 > 4) ? 4 : (len - i * 4);\n"
         "        for (size_t k = 0; k < chunk; k++) t[k] = src[i * 4 + k];\n"
         "        dst[i] = (uint32_t)t[0] | ((uint32_t)t[1] << 8)\n"
         "               | ((uint32_t)t[2] << 16) | ((uint32_t)t[3] << 24);\n"
         "    }\n"
         "    return words;\n}"),
        ("void unpack_words(const uint32_t *src, size_t len, uint8_t *dst)\n{\n    /* 你的代码 */\n    (void)src; (void)len; (void)dst;\n}",
         "void unpack_words(const uint32_t *src, size_t len, uint8_t *dst)\n{\n"
         "    for (size_t i = 0; i < len; i++) dst[i] = (uint8_t)((src[i / 4] >> (8 * (i % 4))) & 0xFF);\n}"),
    ],
}


def main():
    gcc = "gcc"
    all_ok = True
    for fname, patches in PATCHES.items():
        src = io.open(os.path.join(HERE, fname), encoding="utf-8").read()
        for old, new in patches:
            if old not in src:
                print(f"  !! {fname}: 找不到要替换的片段（TODO 文本变了？）")
                print("     ---- 片段开头 ----")
                print("     " + old.splitlines()[0])
                all_ok = False
                continue
            src = src.replace(old, new, 1)

        tmp_c = os.path.join(HERE, "_t_" + fname)
        tmp_e = os.path.join(HERE, "_t_" + os.path.splitext(fname)[0])
        io.open(tmp_c, "w", encoding="utf-8").write(src)

        r = subprocess.run([gcc, "-Wall", "-Wextra", "-O2", "-I", HERE,
                            "-o", tmp_e, tmp_c],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  !! {fname} 编译失败：")
            print("     " + "\n     ".join(r.stderr.strip().splitlines()[:8]))
            all_ok = False
            continue
        warn = [l for l in r.stderr.splitlines() if "warning" in l]
        if warn:
            print(f"  ~~ {fname} 有警告：")
            for l in warn[:5]:
                print("     " + l)

        try:
            r2 = subprocess.run([tmp_e], capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            print(f"  !! {fname} 运行超时")
            all_ok = False
            continue

        out = r2.stdout
        res = "ALL PASS" if "ALL PASS" in out else "FAIL"
        flag = "[OK]  " if res == "ALL PASS" and r2.returncode == 0 else "[BAD] "
        if res != "ALL PASS" or r2.returncode != 0:
            all_ok = False
        # 抓失败行
        fails = [l.strip() for l in out.splitlines() if "[FAIL]" in l]
        print(f"  {flag}{fname:26s} rc={r2.returncode}  {res}")
        for l in fails[:6]:
            print(f"        {l}")

        os.remove(tmp_c)
        if os.path.exists(tmp_e):
            os.remove(tmp_e)
        if os.path.exists(tmp_e + ".exe"):
            os.remove(tmp_e + ".exe")

    print()
    print("=" * 60)
    print(" 全部通过" if all_ok else " 有失败项，见上")
    print("=" * 60)
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
