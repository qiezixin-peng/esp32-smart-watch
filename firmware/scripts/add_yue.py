# -*- coding: utf-8 -*-
import io, struct, re, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from PIL import Image, ImageDraw, ImageFont

SRC_CPP = r"C:\Users\且自信\Desktop\Workspace\project-002-ESP32智能手表项目调研\firmware_T-Watch\src\fonts\font_vlw_big.cpp"
SRC_H   = r"C:\Users\且自信\Desktop\Workspace\project-002-ESP32智能手表项目调研\firmware_T-Watch\src\fonts\font_vlw_big.h"

with io.open(SRC_CPP, "r", encoding="utf-8", newline="") as f:
    text = f.read()
m = re.search(r"\{([^}]*)\}", text, re.S)
hexes = re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))
data = bytes(int(h, 16) for h in hexes)

gCount, version, fontSize, mboxY, ascent, descent = struct.unpack_from(">6I", data, 0)
table_off = 24
bmp_base = table_off + gCount * 28

# 解析字形表
glyphs = []
off = table_off
for i in range(gCount):
    u, h, w, adv, dy, dx, pad = struct.unpack_from(">7I", data, off)
    off += 28
    glyphs.append((u, h, w, adv, dy, dx, pad))
# 位图顺序
bmp_parts = []
bmp_off = bmp_base
for u, h, w, adv, dy, dx, pad in glyphs:
    bmp_parts.append(data[bmp_off:bmp_off+h*w])
    bmp_off += h * w
old_bmp_total = sum(len(p) for p in bmp_parts)

# 渲染「月」(U+6708)
font = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 32)
img = Image.new("L", (200, 200), 0)
ImageDraw.Draw(img).text((2, 2), "月", font=font, fill=255)
bbox = img.getbbox()  # (left, top, right, bottom)
crop = img.crop(bbox)
alpha = bytes(crop.tobytes())
h, w = crop.size[1], crop.size[0]
adv = int(round(font.getlength("月")))
dY = 36 - bbox[1]
dX = bbox[0] - 2
print(f"月: bbox={bbox} h={h} w={w} adv={adv} dY={dY} dX={dX} alpha_len={len(alpha)}")

# 校验与同字族参考一致
new_glyph = (0x6708, h, w, adv, dY, dX, 0)
print("新字形:", new_glyph)

# 重建: 头 + 字形表(旧+新) + 位图(旧+新) + 尾部
head = struct.pack(">6I", gCount + 1, version, fontSize, mboxY, ascent, descent)
new_table = b""
for g in glyphs:
    new_table += struct.pack(">7I", *g)
new_table += struct.pack(">7I", *new_glyph)
new_bmp = b"".join(bmp_parts) + alpha
tail = data[bmp_base + old_bmp_total:]   # 尾部 18 字节
newdata = head + new_table + new_bmp + tail
print("新 gCount:", gCount+1, "新总大小:", len(newdata))

# 生成 cpp 文本
lines = []
lines.append("// Auto-generated .vlw font: Microsoft YaHei 32pt (subset)")
lines.append(f"// Glyphs: {gCount+1}, size: {len(newdata)} bytes")
lines.append("// Single external definition (see font_vlw_big.h)")
lines.append("#include <Arduino.h>")
lines.append(f"extern const uint8_t font_vlw_big[{len(newdata)}] PROGMEM = {{")
for i in range(0, len(newdata), 16):
    chunk = newdata[i:i+16]
    hexstr = ", ".join(f"0x{b:02X}" for b in chunk)
    lines.append("  " + hexstr + ",")
lines.append("};")
out_cpp = "\n".join(lines) + "\n"

with io.open(SRC_CPP, "w", encoding="utf-8", newline="") as f:
    f.write(out_cpp)

# 更新 .h 注释
with io.open(SRC_H, "r", encoding="utf-8", newline="") as f:
    htext = f.read()
htext = re.sub(r"// Glyphs: \d+, size: \d+ bytes", f"// Glyphs: {gCount+1}, size: {len(newdata)} bytes", htext)
with io.open(SRC_H, "w", encoding="utf-8", newline="") as f:
    f.write(htext)

print("已写入 font_vlw_big.cpp + font_vlw_big.h")
