#!/usr/bin/env python3
"""提取源文件中的非 ASCII 字符，供 LVGL 字体生成使用。"""

import argparse
from collections import Counter
from pathlib import Path
import sys
import unicodedata


def read_sources(paths):
    """读取全部 UTF-8 源文件，返回合并文本。"""
    contents = []
    for path in paths:
        try:
            contents.append(Path(path).read_text(encoding="utf-8"))
        except FileNotFoundError:
            raise ValueError(f"找不到文件：{path}")
        except UnicodeDecodeError:
            raise ValueError(f"无法以 UTF-8 解码文件：{path}")
        except OSError as error:
            raise ValueError(f"无法读取文件 {path}：{error}")
    return "".join(contents)


def collect_non_ascii(content):
    """按 Unicode 码点返回唯一非 ASCII 字符及其出现次数。"""
    counter = Counter(char for char in content if ord(char) > 127)
    return sorted(counter, key=ord), counter


def print_character_analysis(characters, counter):
    """输出面向人工阅读的字符分析。"""
    print("=" * 60)
    print("LVGL 字体非 ASCII 字符分析")
    print("=" * 60)
    print(f"\n发现 {len(characters)} 个唯一非 ASCII 字符")
    print("\n完整字符列表：")
    print("-" * 30)
    print("字符：" + "".join(characters))
    print("\n字符详情：")
    print("-" * 30)
    print(f"{'字符':<4} {'码点':<8} {'名称':<40} {'次数':<6}")
    for char in characters:
        print(f"'{char}'  U+{ord(char):04X}   {unicodedata.name(char, '未知'):<40} {counter[char]:<6}")
    print("\nLVGL 字体转换器字符参数：")
    print("-" * 30)
    print("".join(characters))
    print("\n分析完成。")


def parse_args():
    parser = argparse.ArgumentParser(description="提取源文件中的非 ASCII 字符")
    parser.add_argument("paths", nargs="+", help="要扫描的 UTF-8 源文件")
    parser.add_argument(
        "--symbols-only",
        action="store_true",
        help="仅输出按码点排序的唯一非 ASCII 字符",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    try:
        content = read_sources(args.paths)
    except ValueError as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1

    characters, counter = collect_non_ascii(content)
    if args.symbols_only:
        print("".join(characters))
    else:
        print_character_analysis(characters, counter)
    return 0


if __name__ == "__main__":
    sys.exit(main())
