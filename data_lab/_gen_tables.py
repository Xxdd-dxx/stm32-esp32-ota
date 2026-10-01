# -*- coding: utf-8 -*-
"""
生成 data_lab 用的两个头文件：
  1) crc32_table.h   —— 256 项 CRC32 查表（多项式 0xEDB88320，与项目一致）
  2) test_vectors.h  —— 预先算好的合法帧字节流（练习 3/4/5/6 的输入）
另外打印几个 CRC 参考值，用来写进 ex02 的断言里。

运行：
  python _gen_tables.py
"""
import os
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
POLY = 0xEDB88320


def crc32_bitwise(data: bytes, crc: int = 0) -> int:
    """按位实现，与 C 端 ota_crc32_update 完全同源，用来交叉验证。"""
    crc = (~crc) & 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (POLY & -(crc & 1))
            crc &= 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def build_table():
    tbl = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ (POLY & -(c & 1))
        tbl.append(c & 0xFFFFFFFF)
    return tbl


# ---------------------------------------------------------------- 帧组装
OVERHEAD = 13   # AA 55 | len(2) | cmd(1) | seq(2) | crc(4) | 0D 0A


def build_frame(cmd: int, seq: int, payload: bytes) -> bytes:
    flen = 3 + len(payload)
    body = bytes([cmd]) + seq.to_bytes(2, "little") + payload
    crc = crc32_bitwise(body)
    return (b"\xAA\x55" + flen.to_bytes(2, "little") + body
            + crc.to_bytes(4, "little") + b"\x0D\x0A")


def hex_array(name: str, data: bytes, per_line: int = 12) -> str:
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
    lines.append("};")
    return "\n".join(lines)


# ---------------------------------------------------------------- 1. 表
tbl = build_table()
with open(os.path.join(HERE, "crc32_table.h"), "w", encoding="utf-8") as f:
    f.write("/* 本文件由 _gen_tables.py 自动生成，不要手改。\n"
            " * CRC32 查表（多项式 0xEDB88320 = zlib / PKZIP / 项目 ota_crc32_update 同款）\n"
            " * 用法： crc = crc32_fast(0, buf, len);\n"
            " */\n")
    f.write("#ifndef CRC32_TABLE_H\n#define CRC32_TABLE_H\n\n")
    f.write("#include <stdint.h>\n#include <stddef.h>\n\n")
    f.write("static const uint32_t CRC32_TABLE[256] = {\n")
    for i in range(0, 256, 4):
        f.write("    " + " ".join(f"0x{tbl[i + j]:08X}u," for j in range(4)) + "\n")
    f.write("};\n\n")
    f.write("/* 流式可续算：先传 0，之后把上一次的返回值接着传 */\n"
            "static inline uint32_t crc32_fast(uint32_t crc, const uint8_t *d, size_t n)\n"
            "{\n"
            "    crc = ~crc;\n"
            "    while (n--) crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ *d++) & 0xFFu];\n"
            "    return ~crc;\n"
            "}\n\n")
    f.write("#endif /* CRC32_TABLE_H */\n")

# ---------------------------------------------------------------- 2. 测试向量
ACK = build_frame(0x03, 1, bytes([0x01, 0x00, 0x00]))
START = build_frame(0x01, 0, (5604).to_bytes(4, "little") + (0x11223344).to_bytes(4, "little"))
INFO = build_frame(0x08, 0, b"boot v1.0")
DATA_PAYLOAD = bytes((i & 0xFF) for i in range(256))
DATA = build_frame(0x02, 0, DATA_PAYLOAD)
END = build_frame(0x05, 22, (5604).to_bytes(4, "little") + crc32_bitwise(DATA_PAYLOAD).to_bytes(4, "little"))

with open(os.path.join(HERE, "test_vectors.h"), "w", encoding="utf-8") as f:
    f.write("/* 本文件由 _gen_tables.py 自动生成，不要手改。\n"
            " * 预先算好的\"合法帧\"字节流 —— 练习 3/4/5/6 的目标与输入。\n"
            " * 每一帧都满足：AA 55 | flen(LE) | cmd | seq(LE) | payload | CRC32(LE) | 0D 0A\n"
            " */\n")
    f.write("#ifndef TEST_VECTORS_H\n#define TEST_VECTORS_H\n\n")
    f.write("#include <stdint.h>\n#include <string.h>\n\n")
    f.write('#include "crc32_table.h"\n\n')
    f.write(f"#define TV_OVERHEAD    {OVERHEAD}\n\n")
    f.write(hex_array("TV_ACK", ACK) + "\n\n")
    f.write(hex_array("TV_START", START) + "\n\n")
    f.write(hex_array("TV_INFO", INFO) + "\n\n")
    f.write(hex_array("TV_END", END) + "\n\n")
    f.write(f"/* DATA 帧：payload[i] = (uint8_t)i，共 {len(DATA_PAYLOAD)} 字节 */\n")
    f.write(f"#define TV_DATA_PLEN      {len(DATA_PAYLOAD)}\n")
    f.write(f"#define TV_DATA_FLEN      {3 + len(DATA_PAYLOAD)}\n")
    f.write(f"#define TV_DATA_FRAME_LEN {len(DATA)}\n")
    data_body = bytes([0x02]) + (0).to_bytes(2, "little") + DATA_PAYLOAD
    f.write(f"/* TV_DATA_CRC32 = 纯 payload 的 CRC（CMD_END 里传的就是这个）*/\n")
    f.write(f"#define TV_DATA_CRC32        0x{crc32_bitwise(DATA_PAYLOAD):08X}u\n")
    f.write(f"#define TV_END_FW_CRC32      0x{crc32_bitwise(DATA_PAYLOAD):08X}u\n")
    f.write(f"/* TV_DATA_FRAME_CRC32 = 帧内那 4 字节，覆盖 cmd+seq+payload（共 {len(data_body)}B）*/\n")
    f.write(f"#define TV_DATA_FRAME_CRC32  0x{crc32_bitwise(data_body):08X}u\n\n")
    f.write("static inline void tv_fill_data_payload(uint8_t *p)\n{\n"
            "    for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;\n}\n\n")
    f.write("""/* 测试用的最小组帧器 —— 练习 3 的参考答案，先自己写一版再来对。
 * 练习 4/5/6 需要它来"造"输入，所以放在这里给你直接用。
 * 返回整帧长度（13 + plen）。调用者自己保证 out 够大。 */
static inline size_t tv_build(uint8_t *out, uint8_t cmd, uint16_t seq,
                              const uint8_t *payload, uint16_t plen)
{
    uint16_t flen = (uint16_t)(3 + plen);
    out[0] = 0xAA; out[1] = 0x55;
    out[2] = (uint8_t)(flen & 0xFF);
    out[3] = (uint8_t)(flen >> 8);
    out[4] = cmd;
    out[5] = (uint8_t)(seq & 0xFF);
    out[6] = (uint8_t)(seq >> 8);
    if (plen && payload) memcpy(&out[7], payload, plen);
    uint32_t crc = crc32_fast(0, &out[4], flen);
    out[7 + plen]  = (uint8_t)(crc & 0xFF);
    out[8 + plen]  = (uint8_t)((crc >> 8) & 0xFF);
    out[9 + plen]  = (uint8_t)((crc >> 16) & 0xFF);
    out[10 + plen] = (uint8_t)((crc >> 24) & 0xFF);
    out[11 + plen] = 0x0D;
    out[12 + plen] = 0x0A;
    return (size_t)(13 + plen);
}

#endif /* TEST_VECTORS_H */
""")

# ---------------------------------------------------------------- 3. 参考值
print("=" * 62)
print("ex02 断言用的参考值（直接抄进 C 文件）")
print("=" * 62)
for name, data in [("空串", b""),
                   ("\"123456789\"", b"123456789"),
                   ("\"The quick brown fox jumps over the lazy dog\"",
                    b"The quick brown fox jumps over the lazy dog"),
                   ("DATA payload 0..255 (256B)", DATA_PAYLOAD),
                   ("\"boot v1.0\"", b"boot v1.0")]:
    print(f"  {name:46s} 0x{crc32_bitwise(data):08X}u   (zlib={zlib.crc32(data) & 0xFFFFFFFF:08X})")

print()
print("=" * 62)
print("帧长度速查")
print("=" * 62)
for name, fr in [("ACK  payload=3", ACK), ("START payload=8", START),
                 ("INFO payload=9", INFO), ("DATA payload=256", DATA),
                 ("END  payload=8", END)]:
    print(f"  {name:18s} flen={int.from_bytes(fr[2:4],'little'):3d}  整帧={len(fr):3d}B")

print()
print("生成的字节流（供人工核对，前 24 字节）")
for name, fr in [("ACK", ACK), ("START", START), ("INFO", INFO)]:
    print(f"  {name:6s}: " + " ".join(f"{b:02X}" for b in fr))

assert crc32_bitwise(b"123456789") == 0xCBF43926, "CRC 实现与标准不一致！"
print("\n[OK] 自检通过：CRC32(\"123456789\") == 0xCBF43926")
