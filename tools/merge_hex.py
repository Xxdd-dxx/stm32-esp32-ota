#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
merge_hex.py — 合并多个 Intel HEX 文件为一个（地址不重叠时安全）

【为什么需要这个工具】
FlyMcu 的「开始编程」按钮内部会自动执行三段式流程：
    全片擦除 -> 写入 -> 校验
也就是说：即使你不点「清除芯片(Z)」，烧 App 时它也照样把整片 Flash 擦光，
Bootloader（0x08000000）会被一起干掉，芯片复位后找不到入口 -> LED 全灭。

解决方案：把 Bootloader.hex 和 app_demo.hex 合并成一个 hex，
         一次「开始编程」就把两个程序同时写进芯片。

【用法】
    # 方式 1：命令行传参
    python merge_hex.py bootloader.hex app_demo.hex -o merged.hex

    # 方式 2：改下面的 DEFAULT_FILES 后直接运行
    python merge_hex.py

【注意】
- 两个 hex 的地址区间不能重叠（Bootloader 0x08000000~，App 0x08008000~，天然不重叠）
- 合并后的文件在 FlyMcu 里选它，点「开始编程」即可
- 之后 App 版本迭代一律走 ESP32 OTA，不再需要重新合并
"""
import argparse
import sys
from pathlib import Path

# 默认文件列表（按地址从低到高排列）
DEFAULT_FILES = [
    r"bootloader.hex",   # 0x08000000 起
    r"app_demo.hex",     # 0x08008000 起
]
DEFAULT_OUTPUT = "merged.hex"


def merge_hex(paths, output):
    records = []
    total_data = 0

    for path in paths:
        p = Path(path)
        if not p.exists():
            print(f"[ERROR] 文件不存在: {path}")
            return 1

        data_lines = 0
        with p.open("r", encoding="utf-8", errors="ignore") as fp:
            for raw in fp:
                line = raw.strip()
                if not line or not line.startswith(":"):
                    continue
                rtype = line[7:9].upper()
                # 01=EOF 记录，只在最后补一个，这里全部丢弃
                if rtype == "01":
                    continue
                # 05=起始线性地址，与数据落点无关，丢弃更干净
                if rtype == "05":
                    continue
                records.append(line)
                if rtype == "00":
                    data_lines += 1
                    total_data += 1

        print(f"[OK]   {path}: {data_lines} 条数据记录")

    if total_data == 0:
        print("[ERROR] 没读到任何数据记录，检查文件是否是有效的 Intel HEX")
        return 1

    records.append(":00000001FF")           # 结尾补 EOF

    Path(output).write_text("\n".join(records) + "\n", encoding="utf-8")
    print(f"[DONE] 已生成 {output}（{total_data} 条数据记录）")
    print(f"       在 FlyMcu 里选 {output}，点「开始编程」一次烧入")
    return 0


def main():
    ap = argparse.ArgumentParser(description="合并多个 Intel HEX 文件（地址不重叠时安全）")
    ap.add_argument("files", nargs="*", help="要合并的 .hex 文件，按地址从低到高")
    ap.add_argument("-o", "--output", default=DEFAULT_OUTPUT, help="输出文件名")
    args = ap.parse_args()

    paths = args.files if args.files else DEFAULT_FILES
    sys.exit(merge_hex(paths, args.output))


if __name__ == "__main__":
    main()
