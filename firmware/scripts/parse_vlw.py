# -*- coding: utf-8 -*-
import io, struct, re, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
src = r"C:\Users\且自信\Desktop\Workspace\project-002-ESP32智能手表项目调研\firmware_T-Watch\src\fonts\font_vlw_big.cpp"
with io.open(src, "r", encoding="utf-8", newline="") as f:
    text = f.read()
m = re.search(r"\{([^}]*)\}", text, re.S)
hexes = re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))
data = bytes(int(h, 16) for h in hexes)

gCount, version, fontSize, mboxY, ascent, descent = struct.unpack_from(">6I", data, 0)
print(f"HEAD gCount={gCount} version=0x{version:x} fontSize={fontSize} mboxY={mboxY} Ascent={ascent} Descent={descent}")
table_off = 24
bmp_base = table_off + gCount * 28
print("字形表偏移:", table_off, "位图起始:", bmp_base)
off = table_off
glyphs = []
bmp_total = 0
for i in range(gCount):
    unicode, height, width, gxAdv, dY, dX, pad = struct.unpack_from(">7I", data, off)
    off += 28
    glyphs.append((unicode, height, width, gxAdv, dY, dX))
    bmp_total += height * width
print("位图总字节:", bmp_total, "位图结束:", bmp_base + bmp_total, "剩余:", len(data) - (bmp_base + bmp_total))
for i, g in enumerate(glyphs):
    unicode, height, width, gxAdv, dY, dX = g
    ch = chr(unicode) if 32 <= unicode < 0x20000 else "?"
    print(f"i={i:2d} U+{unicode:04X} ch={ch!r} h={height:3d} w={width:3d} adv={gxAdv:3d} dY={dY:3d} dX={dX:3d}")
tail = bmp_base + bmp_total
ln = data[tail]; name = data[tail+1:tail+1+ln].decode("ascii", "replace")
off2 = tail+1+ln; ln2 = data[off2]; name2 = data[off2+1:off2+1+ln2].decode("ascii", "replace")
off3 = off2+1+ln2; flag = data[off3]
print(f"TAIL fontname(len={ln})={name!r} postscript(len={ln2})={name2!r} flag={flag} total={off3+1} == {len(data)}: {off3+1==len(data)}")
