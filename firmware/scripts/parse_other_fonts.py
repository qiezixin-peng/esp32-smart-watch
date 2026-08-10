# -*- coding: utf-8 -*-
import io, struct, re, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
def parse(path):
    with io.open(path, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    m = re.search(r"\{([^}]*)\}", text, re.S)
    hexes = re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))
    data = bytes(int(h,16) for h in hexes)
    gCount, version, fontSize, mboxY, ascent, descent = struct.unpack_from(">6I", data, 0)
    bmp_base = 24 + gCount*28
    off = 24
    glyphs = []
    for i in range(gCount):
        u, h, w, adv, dy, dx, pad = struct.unpack_from(">7I", data, off)
        off += 28
        glyphs.append((u, h, w, adv, dy, dx))
    print(f"=== {path.split(chr(92))[-1]}: gCount={gCount} size={len(data)} Ascent={ascent} Descent={descent} ===")
    for u,h,w,adv,dy,dx in glyphs:
        ch = chr(u) if 32<=u<0x20000 else "?"
        print(f"  U+{u:04X} {ch!r} h={h} w={w} adv={adv} dY={dy} dX={dx}")
    return glyphs, ascent, descent
for p in [r"C:\Users\且自信\Desktop\Workspace\project-002-ESP32智能手表项目调研\firmware_T-Watch\src\fonts\font_vlw_time.cpp",
          r"C:\Users\且自信\Desktop\Workspace\project-002-ESP32智能手表项目调研\firmware_T-Watch\src\fonts\font_vlw_date.cpp"]:
    parse(p)
