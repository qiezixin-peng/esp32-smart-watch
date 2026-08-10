# 修复中文路径下 ld 无法生成 firmware.map 的问题
# 将链接器 map 输出重定向到英文路径 C:/pio_build/firmware.map
Import("env")

try:
    os = __import__("os")
    if not os.path.exists("C:/pio_build"):
        os.makedirs("C:/pio_build", exist_ok=True)

    flags = env.get("LINKFLAGS", [])
    new_flags = []
    for f in flags:
        s = str(f)
        if s.startswith("-Wl,-Map="):
            new_flags.append('-Wl,-Map="C:/pio_build/firmware.map"')
        else:
            new_flags.append(f)
    env.Replace(LINKFLAGS=new_flags)
    print("[MAPFIX] LINKFLAGS map path -> C:/pio_build/firmware.map")
except Exception as e:
    print("[MAPFIX] ERROR: %s" % e)