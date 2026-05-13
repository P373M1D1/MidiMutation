#!/usr/bin/env python3
"""
Generate the C64-inspired bitmap fonts used by the display theme.

The glyph set is authored as an 8x8 pixel alphabet for the most recognizable
upper-case letters and digits, then scaled with nearest-neighbour sampling to
the existing UI cell sizes so the display layout does not need to change.
Any printable ASCII glyphs not explicitly overridden fall back to a compact
conversion of the existing 7x10 bitmap font so the full UI remains renderable.
"""

from __future__ import annotations

from pathlib import Path
import re


ASCII_FIRST = 32
ASCII_LAST = 126
GLYPH_COUNT = ASCII_LAST - ASCII_FIRST + 1
C64_PIXEL_ASPECT_NUM = 5
C64_PIXEL_ASPECT_DEN = 4

ROOT = Path(__file__).resolve().parent.parent
FONTS_C = ROOT / "Drivers" / "ST7796" / "fonts.c"
OUT_DIR = ROOT / "Drivers" / "ST7796"


def glyph(*rows: str) -> list[int]:
    if len(rows) != 8:
        raise ValueError("each glyph must have exactly 8 rows")

    result: list[int] = []
    for row in rows:
        if len(row) != 8:
            raise ValueError(f"glyph row must be 8 columns wide: {row!r}")
        bits = 0
        for ch in row:
            bits <<= 1
            bits |= 1 if ch == "X" else 0
        result.append(bits)
    return result


C64_OVERRIDES: dict[str, list[int]] = {
    " ": glyph(
        "........",
        "........",
        "........",
        "........",
        "........",
        "........",
        "........",
        "........",
    ),
    "!": glyph(
        "..XX....",
        "..XX....",
        "..XX....",
        "..XX....",
        "..XX....",
        "........",
        "..XX....",
        "........",
    ),
    "-": glyph(
        "........",
        "........",
        "........",
        ".XXXX...",
        "........",
        "........",
        "........",
        "........",
    ),
    ".": glyph(
        "........",
        "........",
        "........",
        "........",
        "........",
        "........",
        "..XX....",
        "........",
    ),
    "/": glyph(
        ".....X..",
        "....X...",
        "....X...",
        "...X....",
        "..X.....",
        ".X......",
        ".X......",
        "........",
    ),
    ":": glyph(
        "........",
        "..XX....",
        "........",
        "........",
        "..XX....",
        "........",
        "........",
        "........",
    ),
    "?": glyph(
        ".XXXX...",
        "X....X..",
        ".....X..",
        "....X...",
        "...X....",
        "........",
        "...X....",
        "........",
    ),
    "0": glyph(
        "..XX....",
        ".X..X...",
        ".X.XX...",
        ".XX.X...",
        ".X..X...",
        ".X..X...",
        "..XX....",
        "........",
    ),
    "1": glyph(
        "...X....",
        "..XX....",
        ".X.X....",
        "...X....",
        "...X....",
        "...X....",
        ".XXXXX..",
        "........",
    ),
    "2": glyph(
        "..XXX...",
        ".X...X..",
        ".....X..",
        "...XX...",
        "..X.....",
        ".X......",
        ".XXXXX..",
        "........",
    ),
    "3": glyph(
        ".XXXX...",
        ".....X..",
        "....X...",
        "...XX...",
        ".....X..",
        ".X...X..",
        "..XXX...",
        "........",
    ),
    "4": glyph(
        "....X...",
        "...XX...",
        "..X.X...",
        ".X..X...",
        ".XXXXX..",
        "....X...",
        "....X...",
        "........",
    ),
    "5": glyph(
        ".XXXXX..",
        ".X......",
        ".XXXX...",
        ".....X..",
        ".....X..",
        ".X...X..",
        "..XXX...",
        "........",
    ),
    "6": glyph(
        "..XXX...",
        ".X......",
        ".XXXX...",
        ".X...X..",
        ".X...X..",
        ".X...X..",
        "..XXX...",
        "........",
    ),
    "7": glyph(
        ".XXXXX..",
        ".....X..",
        "....X...",
        "...X....",
        "..X.....",
        "..X.....",
        "..X.....",
        "........",
    ),
    "8": glyph(
        "..XXX...",
        ".X...X..",
        ".X...X..",
        "..XXX...",
        ".X...X..",
        ".X...X..",
        "..XXX...",
        "........",
    ),
    "9": glyph(
        "..XXX...",
        ".X...X..",
        ".X...X..",
        "..XXXX..",
        ".....X..",
        "....X...",
        "..XX....",
        "........",
    ),
    "A": glyph(
        "..XX....",
        ".X..X...",
        ".X..X...",
        ".XXXX...",
        ".X..X...",
        ".X..X...",
        ".X..X...",
        "........",
    ),
    "B": glyph(
        ".XXX....",
        ".X..X...",
        ".X..X...",
        ".XXX....",
        ".X..X...",
        ".X..X...",
        ".XXX....",
        "........",
    ),
    "C": glyph(
        "..XXX...",
        ".X...X..",
        ".X......",
        ".X......",
        ".X......",
        ".X...X..",
        "..XXX...",
        "........",
    ),
    "D": glyph(
        ".XXX....",
        ".X..X...",
        ".X...X..",
        ".X...X..",
        ".X...X..",
        ".X..X...",
        ".XXX....",
        "........",
    ),
    "E": glyph(
        ".XXXX...",
        ".X......",
        ".X......",
        ".XXX....",
        ".X......",
        ".X......",
        ".XXXX...",
        "........",
    ),
    "F": glyph(
        ".XXXX...",
        ".X......",
        ".X......",
        ".XXX....",
        ".X......",
        ".X......",
        ".X......",
        "........",
    ),
    "G": glyph(
        "..XXX...",
        ".X...X..",
        ".X......",
        ".X.XX...",
        ".X..X...",
        ".X..X...",
        "..XXX...",
        "........",
    ),
    "H": glyph(
        ".X..X...",
        ".X..X...",
        ".X..X...",
        ".XXXX...",
        ".X..X...",
        ".X..X...",
        ".X..X...",
        "........",
    ),
    "I": glyph(
        ".XXXX...",
        "..XX....",
        "..XX....",
        "..XX....",
        "..XX....",
        "..XX....",
        ".XXXX...",
        "........",
    ),
    "J": glyph(
        "...XXX..",
        "....X...",
        "....X...",
        "....X...",
        "....X...",
        ".X..X...",
        "..XX....",
        "........",
    ),
    "K": glyph(
        ".X..X...",
        ".X.X....",
        ".XX.....",
        ".XX.....",
        ".X.X....",
        ".X..X...",
        ".X..X...",
        "........",
    ),
    "L": glyph(
        ".X......",
        ".X......",
        ".X......",
        ".X......",
        ".X......",
        ".X......",
        ".XXXX...",
        "........",
    ),
    "M": glyph(
        ".X...X..",
        ".XX.XX..",
        ".X.X.X..",
        ".X.X.X..",
        ".X...X..",
        ".X...X..",
        ".X...X..",
        "........",
    ),
    "N": glyph(
        ".X...X..",
        ".XX..X..",
        ".XX..X..",
        ".X.X.X..",
        ".X..XX..",
        ".X..XX..",
        ".X...X..",
        "........",
    ),
    "O": glyph(
        "..XX....",
        ".X..X...",
        ".X..X...",
        ".X..X...",
        ".X..X...",
        ".X..X...",
        "..XX....",
        "........",
    ),
    "P": glyph(
        ".XXX....",
        ".X..X...",
        ".X..X...",
        ".XXX....",
        ".X......",
        ".X......",
        ".X......",
        "........",
    ),
    "Q": glyph(
        "..XX....",
        ".X..X...",
        ".X..X...",
        ".X..X...",
        ".X.XX...",
        ".X..X...",
        "..XXX...",
        "........",
    ),
    "R": glyph(
        ".XXX....",
        ".X..X...",
        ".X..X...",
        ".XXX....",
        ".X.X....",
        ".X..X...",
        ".X..X...",
        "........",
    ),
    "S": glyph(
        "..XXX...",
        ".X...X..",
        ".X......",
        "..XX....",
        "....X...",
        ".X...X..",
        "..XXX...",
        "........",
    ),
    "T": glyph(
        ".XXXXX..",
        "...X....",
        "...X....",
        "...X....",
        "...X....",
        "...X....",
        "...X....",
        "........",
    ),
    "U": glyph(
        ".X..X...",
        ".X..X...",
        ".X..X...",
        ".X..X...",
        ".X..X...",
        ".X..X...",
        "..XX....",
        "........",
    ),
    "V": glyph(
        ".X...X..",
        ".X...X..",
        ".X...X..",
        ".X...X..",
        "..X.X...",
        "..X.X...",
        "...X....",
        "........",
    ),
    "W": glyph(
        ".X...X..",
        ".X...X..",
        ".X...X..",
        ".X.X.X..",
        ".X.X.X..",
        ".XX.XX..",
        ".X...X..",
        "........",
    ),
    "X": glyph(
        ".X...X..",
        "..X.X...",
        "...X....",
        "...X....",
        "...X....",
        "..X.X...",
        ".X...X..",
        "........",
    ),
    "Y": glyph(
        ".X...X..",
        "..X.X...",
        "...X....",
        "...X....",
        "...X....",
        "...X....",
        "...X....",
        "........",
    ),
    "Z": glyph(
        ".XXXXX..",
        "....X...",
        "...X....",
        "..X.....",
        ".X......",
        ".X......",
        ".XXXXX..",
        "........",
    ),
}


def parse_font7x10_fallback() -> dict[str, list[int]]:
    text = FONTS_C.read_text(encoding="utf-8")
    match = re.search(
        r"static const uint16_t Font7x10_Table\[] = \{(.*?)\n\};\n\nFontDef Font_7x10",
        text,
        re.S,
    )
    if not match:
        raise RuntimeError("could not locate Font7x10_Table in fonts.c")

    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)\s*,", match.group(1))]
    if len(values) != GLYPH_COUNT * 10:
        raise RuntimeError(f"unexpected Font7x10 glyph table length: {len(values)}")

    glyphs: dict[str, list[int]] = {}
    for glyph_index in range(GLYPH_COUNT):
        rows16 = values[glyph_index * 10:(glyph_index + 1) * 10]
        src_rows = [((row >> 9) & 0x7F) for row in rows16]
        out_rows: list[int] = []
        for y in range(8):
            src_y = min(9, int(((y + 0.5) * 10) / 8))
            row7 = src_rows[src_y]
            out_rows.append((row7 << 1) & 0xFF)

        glyphs[chr(ASCII_FIRST + glyph_index)] = out_rows
    return glyphs


def build_c64_alphabet() -> dict[str, list[int]]:
    glyphs = parse_font7x10_fallback()
    glyphs.update(C64_OVERRIDES)

    for upper in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        glyphs[upper.lower()] = C64_OVERRIDES[upper]

    return glyphs


def scale_bitmap(src_rows: list[int], src_w: int, src_h: int, dst_w: int, dst_h: int) -> list[int]:
    scaled: list[int] = []
    for y in range(dst_h):
        src_y = min(src_h - 1, (y * src_h) // dst_h)
        src_row = src_rows[src_y]
        out_row = 0
        for x in range(dst_w):
            src_x = min(src_w - 1, (x * src_w) // dst_w)
            if src_row & (1 << (src_w - 1 - src_x)):
                out_row |= 1 << (dst_w - 1 - x)
        scaled.append(out_row)
    return scaled


def compose_font_rows(src_rows: list[int], cell_w: int, cell_h: int, pad_x: int, pad_y: int) -> list[int]:
    inner_w = cell_w - (pad_x * 2)
    inner_h = cell_h - (pad_y * 2)
    target_h = (inner_w * C64_PIXEL_ASPECT_NUM + (C64_PIXEL_ASPECT_DEN // 2)) // C64_PIXEL_ASPECT_DEN
    if inner_w <= 0 or inner_h <= 0:
        raise ValueError("invalid cell size/padding combination")

    if target_h <= 0:
        raise ValueError("invalid scaled glyph height")

    if target_h > inner_h:
        target_h = inner_h

    pad_y = (cell_h - target_h) // 2

    scaled = scale_bitmap(src_rows, 8, 8, inner_w, target_h)
    out_rows = [0] * cell_h

    for y, row_bits in enumerate(scaled):
        dest_y = pad_y + y
        packed = 0
        for x in range(inner_w):
            if row_bits & (1 << (inner_w - 1 - x)):
                packed |= 1 << (31 - (pad_x + x))
        out_rows[dest_y] = packed

    return out_rows


def write_font_file(file_name: str, struct_name: str, cell_w: int, cell_h: int, pad_x: int, pad_y: int, glyphs: dict[str, list[int]]) -> None:
    table_name = f"{struct_name}_Table"
    lines: list[str] = [
        f"/* {file_name}",
        " * C64-inspired bitmap font for ST7796 driver.",
        " * AUTO-GENERATED by tools/gen_c64_theme_font.py - DO NOT EDIT.",
        f" * Cell size : {cell_w} x {cell_h} px   Base grid: 8 x 8 px",
        " */",
        "",
        '#include "fonts.h"',
        "",
        f"static const uint32_t {table_name}[] = {{",
    ]

    for code in range(ASCII_FIRST, ASCII_LAST + 1):
        ch = chr(code)
        rows = compose_font_rows(glyphs[ch], cell_w, cell_h, pad_x, pad_y)
        lines.append(f"    /* 0x{code:02X}  {ch!r} */")
        for index in range(0, len(rows), 8):
            chunk = ", ".join(f"0x{value:08X}U" for value in rows[index:index + 8])
            lines.append(f"    {chunk},")

    lines.extend([
        "};",
        "",
        f"FontDef32 {struct_name} = {{",
        f"    .width  = {cell_w}U,",
        f"    .height = {cell_h}U,",
        f"    .data   = {table_name},",
        "};",
        "",
    ])

    (OUT_DIR / file_name).write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    glyphs = build_c64_alphabet()
    write_font_file("font_c64_8x21.c", "Font_C64_8x21", 8, 21, 0, 2, glyphs)
    write_font_file("font_c64_15x35.c", "Font_C64_15x35", 15, 35, 1, 2, glyphs)
    write_font_file("font_c64_23x49.c", "Font_C64_23x49", 23, 49, 1, 3, glyphs)
    print("Generated C64 theme fonts:")
    print("  Drivers/ST7796/font_c64_8x21.c")
    print("  Drivers/ST7796/font_c64_15x35.c")
    print("  Drivers/ST7796/font_c64_23x49.c")


if __name__ == "__main__":
    main()
