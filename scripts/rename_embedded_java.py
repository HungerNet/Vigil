#!/usr/bin/env python3
"""Rename embedded Java class constants in generated C++ headers."""

from __future__ import annotations

import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ROOT / "java_src"
ARRAY = re.compile(
    r"(static const unsigned char\s+)(\w+)(\[\]\s*=\s*\{)(.*?)(\};)",
    re.DOTALL,
)
BYTE = re.compile(r"0x([0-9A-Fa-f]{2})")
ENTRY_SIZE = {
    3: 4,
    4: 4,
    5: 8,
    6: 8,
    7: 2,
    8: 2,
    9: 4,
    10: 4,
    11: 4,
    12: 4,
    15: 3,
    16: 2,
    17: 4,
    18: 4,
    19: 2,
    20: 2,
}
REPLACEMENTS = (
    (b"FRENCH" + b"IFY", b"VIGIL"),
    (b"French" + b"ify", b"Vigil"),
    (b"french" + b"ify", b"vigil"),
)


def rename_class_constants(data: bytes) -> bytes:
    if data[:4] != b"\xCA\xFE\xBA\xBE":
        return data

    constant_count = struct.unpack_from(">H", data, 8)[0]
    offset = 10
    pool = bytearray()
    index = 1
    while index < constant_count:
        tag = data[offset]
        offset += 1
        pool.append(tag)
        if tag == 1:
            length = struct.unpack_from(">H", data, offset)[0]
            offset += 2
            value = data[offset : offset + length]
            offset += length
            for old, new in REPLACEMENTS:
                value = value.replace(old, new)
            pool.extend(struct.pack(">H", len(value)))
            pool.extend(value)
        else:
            size = ENTRY_SIZE.get(tag)
            if size is None:
                raise ValueError(f"Unsupported Java constant-pool tag {tag}")
            pool.extend(data[offset : offset + size])
            offset += size
            if tag in (5, 6):
                index += 1
        index += 1

    return data[:10] + bytes(pool) + data[offset:]


def rename_header(path: Path) -> bool:
    text = path.read_bytes().decode("utf-8")
    newline = "\r\n" if "\r\n" in text else "\n"
    changed = False
    length_updates: dict[str, int] = {}

    def replace_array(match: re.Match[str]) -> str:
        nonlocal changed
        original = bytes(int(value, 16) for value in BYTE.findall(match.group(4)))
        renamed = rename_class_constants(original)
        if renamed == original:
            return match.group(0)
        changed = True
        lines = [
            "    " + ", ".join(f"0x{value:02X}" for value in renamed[start : start + 16]) + ","
            for start in range(0, len(renamed), 16)
        ]
        length_updates[match.group(2) + "_len"] = len(renamed)
        return (
            match.group(1)
            + match.group(2)
            + match.group(3)
            + newline
            + newline.join(lines)
            + newline
            + match.group(5)
        )

    result = ARRAY.sub(replace_array, text)
    if changed:
        for length_name, length in length_updates.items():
            result, count = re.subn(
                rf"(static const unsigned int {re.escape(length_name)}\s*=\s*)\d+",
                rf"\g<1>{length}",
                result,
            )
            if count != 1:
                raise ValueError(f"Expected one length constant for {length_name} in {path}")
        path.write_bytes(result.encode("utf-8"))
    return changed


def main() -> None:
    changed = [path for path in HEADERS.rglob("*.h") if rename_header(path)]
    print(f"Updated {len(changed)} embedded Java header(s)")


if __name__ == "__main__":
    main()