#!/usr/bin/env python3
"""Report erased blocks in a saved SPI NOR partition dump; never write flash."""

import argparse
import hashlib
import json
import os
import stat
import sys
from pathlib import Path


def integer(value):
    try:
        return int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("use decimal or 0x-prefixed integer") from exc


def analyze(data, base, block_size):
    erased = b"\xff" * block_size
    ranges = []
    start = None
    for offset in range(0, len(data), block_size):
        if data[offset:offset + block_size] == erased:
            if start is None:
                start = offset
        elif start is not None:
            ranges.append((start, offset))
            start = None
    if start is not None:
        ranges.append((start, len(data)))

    tail = ranges[-1][0] if ranges and ranges[-1][1] == len(data) else len(data)

    def describe(begin, end):
        return {
            "partition_start": begin,
            "partition_end_exclusive": end,
            "nor_start": base + begin,
            "nor_end_exclusive": base + end,
            "bytes": end - begin,
        }

    return {
        "size_bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "base_offset": base,
        "erase_size": block_size,
        "erased_ranges": [describe(begin, end) for begin, end in ranges],
        "erased_tail": describe(tail, len(data)),
        "interpretation": "0xff blocks are erased candidates, not proven free space",
    }


def display_range(item):
    print(
        f"区画内 [0x{item['partition_start']:06x}, "
        f"0x{item['partition_end_exclusive']:06x})  "
        f"NOR全体 [0x{item['nor_start']:06x}, "
        f"0x{item['nor_end_exclusive']:06x})  "
        f"{item['bytes'] // 1024} KiB"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path, help="regular file captured from MTD")
    parser.add_argument("--base-offset", type=integer, default=0x210000)
    parser.add_argument("--erase-size", type=integer, default=0x1000)
    parser.add_argument("--expected-size", type=integer, default=0x5f0000)
    parser.add_argument("--json", action="store_true", help="print evidence as JSON")
    args = parser.parse_args()

    if args.base_offset < 0 or args.erase_size <= 0 or args.expected_size <= 0:
        parser.error("base must be nonnegative; erase size and expected size positive")
    if args.expected_size % args.erase_size or args.base_offset % args.erase_size:
        parser.error("expected size and base offset must be erase-block aligned")

    try:
        if not stat.S_ISREG(args.dump.stat().st_mode):
            parser.error("input must be a saved regular file, not a device or pipe")
        with args.dump.open("rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode):
                parser.error("input must be a saved regular file, not an MTD device")
            if info.st_size != args.expected_size:
                parser.error(
                    f"dump size {info.st_size:#x} differs from expected "
                    f"{args.expected_size:#x}; truncated/wrong partition?"
                )
            data = stream.read(args.expected_size + 1)
        if len(data) != args.expected_size:
            parser.error("dump changed or was truncated during reading")
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    result = analyze(data, args.base_offset, args.erase_size)
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print(f"size: {result['size_bytes']} bytes; SHA-256: {result['sha256']}")
        print("全バイト0xffの4 KiBブロック連続範囲:" if args.erase_size == 4096
              else f"全バイト0xffの{args.erase_size}-byteブロック連続範囲:")
        for item in result["erased_ranges"]:
            display_range(item)
        if not result["erased_ranges"]:
            print("なし")
        print("\n末尾の消去済み領域:")
        display_range(result["erased_tail"])
        print("\n注意: 消去状態は、予約・更新用途のない空き領域の証明ではありません。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
