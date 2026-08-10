#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 TFT_eSPI 全量 .vlw 中文字库（GB2312 6763 汉字 + 符号 + ASCII）
输出:
  src/fonts/font_msyh.vlw   (二进制 .vlw，大端格式)
  src/fonts/font_vlw.cpp    (C 数组，PROGMEM)
  src/fonts/font_vlw.h      (声明，更新注释)
格式依据: TFT_eSPI Extensions/Smooth_font.cpp (readInt32 大端 + 28B/glyph 元数据)
与现有字库对齐参数: size=16, ascent=17, descent=5, anchor='ls'
保留既有修复: U+4E00(一) dY=6 (否则"周一"显示缺字/错位)
用法: python scripts/gen_font_vlw.py
"""
import struct, io, os
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = 'C:/Windows/Fonts/msyh.ttc'
FONT_SIZE = 16
BASE_DIR  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_VLW   = os.path.join(BASE_DIR, 'src', 'fonts', 'font_msyh.vlw')
OUT_CPP   = os.path.join(BASE_DIR, 'src', 'fonts', 'font_vlw.cpp')
OUT_H     = os.path.join(BASE_DIR, 'src', 'fonts', 'font_vlw.h')

def build_charset():
    """ASCII + GB2312 全部字符"""
    chars = []
    seen = set()
    for c in range(0x20, 0x7F):           # ASCII 可打印
        ch = chr(c)
        if ch not in seen:
            seen.add(ch); chars.append(ch)
    # GB2312 区位遍历 0xA1A1 ~ 0xF7FE
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            b = bytes([hi, lo])
            try:
                ch = b.decode('gb2312')
            except UnicodeDecodeError:
                continue
            if ch not in seen:
                seen.add(ch); chars.append(ch)
    return chars

def render_glyph(font, ch):
    """渲染单字符，返回 (h, w, adv, dY, dX, pixels)"""
    img = Image.new('L', (64, 64), 0)
    d = ImageDraw.Draw(img)
    baseline = 40
    d.text((0, baseline), ch, font=font, fill=255, anchor='ls')
    bbox = img.getbbox()
    adv = round(font.getlength(ch))
    if bbox is None:                      # 空白字符（空格等）
        return (0, 0, adv, 0, 0, b'')
    left, top, right, bottom = bbox
    h = bottom - top
    w = right - left
    dY = baseline - top                   # baseline 到字形顶部的距离
    dX = left
    pixels = img.crop((left, top, right, bottom)).tobytes()
    return (h, w, adv, dY, dX, pixels)

def main():
    print('收集字符集...')
    chars = build_charset()
    print(f'字符总数: {len(chars)}')
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    asc, desc = font.getmetrics()
    print(f'字体度量: ascent={asc} descent={desc}')

    glyphs = []   # (unicode, h, w, adv, dY, dX, pixels)
    for ch in chars:
        u = ord(ch)
        h, w, adv, dY, dX, px = render_glyph(font, ch)
        if u == 0x4E00:                    # 保留"一"修复：dY=6
            dY = 6
        glyphs.append((u, h, w, adv, dY, dX, px))

    gCount = len(glyphs)
    header = struct.pack('>6I', gCount, 11, FONT_SIZE, 0, asc, desc)
    meta = b''
    bm = b''
    for (u, h, w, adv, dY, dX, px) in glyphs:
        meta += struct.pack('>7i', u, h, w, adv, dY, dX, 0)
        bm += px
    name1 = b'MicrosoftYaHei'
    name2 = b'MicrosoftYaHei-Regular'
    tail = bytes([len(name1)]) + name1 + b'\x00' + bytes([len(name2)]) + name2 + b'\x01'
    vlw = header + meta + bm + tail

    with open(OUT_VLW, 'wb') as f:
        f.write(vlw)
    print(f'.vlw 写入: {OUT_VLW} ({len(vlw)} bytes)')

    # 生成 C 数组
    lines = []
    lines.append('// Auto-generated .vlw font: Microsoft YaHei 16pt (GB2312 FULL)')
    lines.append(f'// Glyphs: {gCount}, size: {len(vlw)} bytes')
    lines.append('// Single external definition (see font_vlw.h)')
    lines.append('#include <Arduino.h>')
    lines.append(f'extern const uint8_t font_vlw[{len(vlw)}] PROGMEM = {{')
    per = 16
    for i in range(0, len(vlw), per):
        chunk = vlw[i:i+per]
        lines.append('  ' + ', '.join(f'0x{b:02X}' for b in chunk) + ',')
    lines.append('};')
    lines.append('')
    with open(OUT_CPP, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print(f'C 数组写入: {OUT_CPP}')

    with open(OUT_H, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated .vlw font: Microsoft YaHei 16pt (GB2312 FULL)\n')
        f.write(f'// Glyphs: {gCount}, size: {len(vlw)} bytes\n')
        f.write('// Definition lives in font_vlw.cpp\n')
        f.write('#ifndef FONT_VLW_H\n#define FONT_VLW_H\n#include <Arduino.h>\n')
        f.write('extern const uint8_t font_vlw[];\n#endif\n')
    print(f'头文件写入: {OUT_H}')

if __name__ == '__main__':
    main()
