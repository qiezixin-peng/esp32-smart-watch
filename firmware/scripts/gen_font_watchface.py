#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成表盘用半透明 vlw 平滑字体 (大端, TFT_eSPI 兼容)
- font_vlw_time: 48pt 时间数字 0123456789: 
- font_vlw_date: 28pt 日期数字 0123456789- 
笔画透明度 = TEXT_ALPHA/255 (生成时写入字形像素, 运行时走官方抗锯齿混合)
用法: python scripts/gen_font_watchface.py
"""
import struct, os
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = 'C:/Windows/Fonts/msyh.ttc'
TEXT_ALPHA = 0.70          # 笔画不透明度: 70%
BASE_DIR  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR   = os.path.join(BASE_DIR, 'src', 'fonts')

def render_glyph(font, ch):
    """渲染单字符, 返回 (h, w, adv, dY, dX, pixels)"""
    img = Image.new('L', (128, 128), 0)
    d = ImageDraw.Draw(img)
    baseline = 100
    d.text((0, baseline), ch, font=font, fill=255, anchor='ls')
    bbox = img.getbbox()
    adv = round(font.getlength(ch))
    if bbox is None:
        return (0, 0, adv, 0, 0, b'')
    left, top, right, bottom = bbox
    h = bottom - top
    w = right - left
    dY = baseline - top
    dX = left
    px = img.crop((left, top, right, bottom)).tobytes()
    # alpha 增益: 让笔画中心也半透明, 背景透出
    px = bytes([int(v * TEXT_ALPHA) for v in px])
    return (h, w, adv, dY, dX, px)

def build_font(name, size, charset, cname, title):
    font = ImageFont.truetype(FONT_PATH, size)
    asc, desc = font.getmetrics()
    glyphs = []
    for ch in charset:
        u = ord(ch)
        h, w, adv, dY, dX, px = render_glyph(font, ch)
        glyphs.append((u, h, w, adv, dY, dX, px))
    gCount = len(glyphs)
    header = struct.pack('>6I', gCount, asc + desc, size, 0, asc, desc)
    meta = b''
    bm = b''
    for (u, h, w, adv, dY, dX, px) in glyphs:
        meta += struct.pack('>7i', u, h, w, adv, dY, dX, 0)
        bm += px
    name1 = b'MicrosoftYaHei'
    name2 = b'MicrosoftYaHei-Regular'
    tail = bytes([len(name1)]) + name1 + b'\x00' + bytes([len(name2)]) + name2 + b'\x01'
    vlw = header + meta + bm + tail

    with open(os.path.join(OUT_DIR, name + '.vlw'), 'wb') as f:
        f.write(vlw)

    lines = []
    lines.append('// Auto-generated .vlw font: ' + title)
    lines.append('// Glyphs: %d, size: %d bytes' % (gCount, len(vlw)))
    lines.append('// Single external definition (see %s.h)' % cname)
    lines.append('#include <Arduino.h>')
    lines.append('extern const uint8_t %s[%d] PROGMEM = {' % (cname, len(vlw)))
    for i in range(0, len(vlw), 16):
        chunk = vlw[i:i+16]
        lines.append('  ' + ', '.join('0x%02X' % b for b in chunk) + ',')
    lines.append('};')
    lines.append('')
    with open(os.path.join(OUT_DIR, cname + '.cpp'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    with open(os.path.join(OUT_DIR, cname + '.h'), 'w', encoding='utf-8') as f:
        f.write('// Auto-generated .vlw font: ' + title + '\n')
        f.write('// Glyphs: %d, size: %d bytes\n' % (gCount, len(vlw)))
        f.write('// Definition lives in %s.cpp\n' % cname)
        f.write('#ifndef %s_H\n#define %s_H\n#include <Arduino.h>\n' % (cname.upper(), cname.upper()))
        f.write('extern const uint8_t %s[];\n#endif\n' % cname)

    print('== %s (%dpt, alpha=%.2f) ==' % (name, size, TEXT_ALPHA))
    print('  glyphs=%d  vlw=%d bytes  asc=%d desc=%d' % (gCount, len(vlw), asc, desc))
    for (u, h, w, adv, dY, dX, _) in glyphs:
        print('  U+%04X %r h=%d w=%d adv=%d dY=%d dX=%d' % (u, chr(u), h, w, adv, dY, dX))
    return vlw

if __name__ == '__main__':
    os.makedirs(OUT_DIR, exist_ok=True)
    build_font('font_vlw_time', 48, '0123456789: ', 'font_vlw_time', 'Microsoft YaHei 48pt (time digits, alpha 0.70)')
    build_font('font_vlw_date', 28, '0123456789- ', 'font_vlw_date', 'Microsoft YaHei 28pt (date digits, alpha 0.70)')