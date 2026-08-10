/*
 * 表盘背景模块 — wallpaper.cpp
 * 功能：接收小程序下发背景图（ThingsCloud MQTT base64）→ 解码 JPEG →
 *       3 槽 FIFO 队列保存到 LittleFS + PSRAM 解码缓存，供表盘/背景图预览使用
 * 架构：
 *   - 存储层：LittleFS 3 槽位（wallpaper0/1/2.jpg），FIFO 覆盖最老槽位
 *   - 缓存层：PSRAM 解码缓存，槽位切换直接整屏 pushImage（<10ms）
 *   - 交互层：表盘模式绘制背景图；背景图 tab 预览/启用/关闭
 * 串口标签：[WALL]
 */
#include "wallpaper.h"
#include <LittleFS.h>
#include <Esp.h>
#include <WiFi.h>
#include <FS.h>
#include <TJpg_Decoder.h>
#include <Preferences.h>
#include <esp_heap_caps.h>   // v29u: PSRAM 全屏缓冲(一次性显示)

/* ==========================================================
 * 表盘背景模块 — 实现（v29o: 3 槽 FIFO 队列）
 * 链路：小程序 → 云存储 → 云函数 sendWallpaper（云端下载+base64）
 *       → ThingsCloud MQTT 下发 {"b64":"..."} → 本模块解码存 LittleFS
 * 说明：绕开 ESP32 直连 HTTPS（腾讯云 EdgeOne 对 mbedTLS 的证书协商
 *       导致 ESP32 无法解析 TCB 证书），MQTT 通道天然免 TLS 问题
 *
 * 多槽：/wallpaper_0.jpg ~ /wallpaper_2.jpg；NVS 存递增收图序号做 FIFO，
 *       上传第 4 张时删最老一张。数字表盘只画「当前启用」槽。
 *
 * v29x: 预览顺序 = 上传时间顺序。物理槽位与显示序号解耦：
 *   显示 1/3=最早上传, 3/3=最新上传；上传第 4 张覆盖最老槽后,
 *   原 2/3 变 1/3、3/3 变 2/3、新图成为 3/3（_order 按 seq 升序重建）。
 * ========================================================== */

#define WALLPAPER_TMP    "/wallpaper.tmp"
#define WALLPAPER_NS     "twatch"        // NVS 命名空间（与 watchface_manager 共用）

// LittleFS 是否挂载成功（失败时所有文件操作直接跳过）
static bool _fsOk = false;
static bool _decFailed = false;   // 解码异常标志: 失败后跳过解码, 避免卡死
static bool _decodedOnce = false;  // 是否曾成功解码过(表盘据此决定是否分配快照)
static bool _busy = false;
static unsigned long _lastTryMs = 0;   // 失败重试时间戳
static uint8_t  _retryCount = 0;       // 失败重试次数

// MQTT 收到的图片 payload（base64），对齐云函数 MAX_B64 + 余量
#define WALLPAPER_PAYLOAD_MAX 24000
static uint8_t* _payloadBuf = NULL;
static size_t   _payloadLen = 0;
static bool     _hasPayload = false;

// v29u: 一次性显示——解码到 PSRAM 全屏缓冲, 完成后整屏 pushImage(避免逐块从上往下刷新)
static uint16_t* _fullBuf = NULL;   // 240x240x2 = 112.5KB (PSRAM, 常驻)
static bool      _bufMode = false;  // 当前解码是否走缓冲模式

// 背景平均亮度统计（智能前景色用）
static uint32_t _lumAcc = 0;
static uint32_t _lumCnt = 0;
static uint8_t  _lumAvg = 128;

// ===== v29o: 槽状态 =====
static bool   _slotHas[WALLPAPER_SLOTS] = { false, false, false };   // 文件是否存在
static int32_t _slotSeq[WALLPAPER_SLOTS] = { 0, 0, 0 };              // 收图序号(0=空)
static int8_t  _active = -1;                                         // 启用槽(-1=关闭)
static bool    _activeNeverSet = true;   // 是否从未设置过启用状态（首次上传自动启用, 用户手动关闭后不再自动）

// v29x: 预览顺序 = 上传时间顺序（_order[显示序号] = 物理槽位, seq 升序; _orderLen = 有效槽数）
static int8_t _order[WALLPAPER_SLOTS] = { -1, -1, -1 };
static int    _orderLen = 0;

// v29x: 按 _slotSeq 升序重建预览顺序（最早→最新），空槽排除
// 作用：按上传时间重建「显示序号→物理槽位」映射
static void orderRebuild() {
    // 临时数组：收集所有有图且有序号的槽位
    int8_t tmp[WALLPAPER_SLOTS];
    int n = 0;
    // 第一遍：筛出有图槽位
    for (int i = 0; i < WALLPAPER_SLOTS; i++)
        if (_slotHas[i] && _slotSeq[i] > 0) tmp[n++] = (int8_t)i;
    // 插入排序：按 seq 从小到大（最早上传排最前）
    for (int a = 1; a < n; a++) {
        int8_t v = tmp[a];
        int b = a - 1;
        while (b >= 0 && _slotSeq[tmp[b]] > _slotSeq[v]) { tmp[b + 1] = tmp[b]; b--; }
        tmp[b + 1] = v;
    }
    // 写回显示顺序表，空位填 -1（无图）
    for (int i = 0; i < WALLPAPER_SLOTS; i++) _order[i] = (i < n) ? tmp[i] : (int8_t)-1;
    _orderLen = n;
}

// v29x: 显示序号 idx（0=最早）对应的物理槽位；越界/空返回 -1
// 作用：显示序号 idx 对应的物理槽位（保证 1/3→2/3→3/3 按上传顺序）
int wallpaperOrderedSlot(int idx) {
    if (idx < 0 || idx >= _orderLen) return -1;
    return (int)_order[idx];
}

// ===== v29m: 解码时快照捕获（文字区背景像素, 替代 readRect —— 这块屏读不了）=====
#define WP_SNAP_MAX 3
typedef struct { int16_t x, y, w, h; uint16_t* buf; } WpSnapRegion;
static WpSnapRegion _snapRegions[WP_SNAP_MAX];
static bool _snapFilled[WP_SNAP_MAX] = { false, false, false };

extern TFT_eSPI tft;   // 定义在 main.cpp

// 作用：拼接槽位图片文件路径
static void slotFilePath(int slot, char* out, size_t n) {
    snprintf(out, n, "/wallpaper_%d.jpg", slot);
}

/* ===== NVS 槽状态读写 ===== */
// 作用：从 NVS 读槽位记录（上传序号/启用状态）
static void nvsLoadSlots() {
    Preferences prefs;
    if (!prefs.begin(WALLPAPER_NS, true)) return;
    // 读全局收图序号（只增不减，用于判断新旧）
    int32_t seq = prefs.getInt("bg_seq", 0);
    for (int i = 0; i < WALLPAPER_SLOTS; i++) {
        char key[10];
        snprintf(key, sizeof(key), "bg_seq%d", i);
        _slotSeq[i] = prefs.getInt(key, 0);
        char path[32];
        slotFilePath(i, path, sizeof(path));
        _slotHas[i] = LittleFS.exists(path);
        if (!_slotHas[i]) _slotSeq[i] = 0;   // 文件缺失的槽视为空（下次可被覆盖）
    }
    // 读启用槽（0x7FFFFFFF 表示从未设置过）
    int32_t activeRaw = prefs.getInt("bg_active", 0x7FFFFFFF);
    _activeNeverSet = (activeRaw == 0x7FFFFFFF);
    _active = (activeRaw == 0x7FFFFFFF) ? -1 : (int8_t)activeRaw;
    prefs.end();
    // 校验 active 指向空槽时修正为 -1
    if (_active >= WALLPAPER_SLOTS || (_active >= 0 && !_slotHas[_active])) _active = -1;
    // 旧版单张 /wallpaper.jpg 迁移到 slot0（仅当 3 槽全空且旧图存在）
    if (LittleFS.exists("/wallpaper.jpg") && wallpaperSlotCount() == 0) {
        if (LittleFS.rename("/wallpaper.jpg", "/wallpaper_0.jpg")) {
            _slotHas[0] = true;
            _slotSeq[0] = 1;
            Preferences w;
            if (w.begin(WALLPAPER_NS, false)) {
                w.putInt("bg_seq", 1);
                w.putInt("bg_seq0", 1);
                if (_activeNeverSet) { w.putInt("bg_active", 0); _active = 0; }
                w.end();
            }
            Serial.println("[WALL] migrated old wallpaper.jpg -> slot0");
            orderRebuild();
        }
    }
    Serial.printf("[WALL] slots=%d/%d seq=%d active=%d neverSet=%d\n",
        (int)(_slotHas[0]) + (int)(_slotHas[1]) + (int)(_slotHas[2]), WALLPAPER_SLOTS, (int)seq, (int)_active, (int)_activeNeverSet);
    orderRebuild();   // v29x: 启动后按上传时间排序预览顺序
}

// 作用：保存当前启用槽位
static void nvsSaveActive(int8_t slot) {
    Preferences prefs;
    if (prefs.begin(WALLPAPER_NS, false)) {
        prefs.putInt("bg_active", slot);
        prefs.end();
    }
}

// 收一张新图后调用：分配槽位（覆盖最老）并更新 NVS
// 作用：分配新槽位（满 3 张删最旧，FIFO 队列）
static int nvsAllocateSlot() {
    Preferences prefs;
    bool ok = prefs.begin(WALLPAPER_NS, false);
    // 全局序号 +1：新图成为最新一张
    int32_t seq = ok ? prefs.getInt("bg_seq", 0) + 1 : 1;
    // 找空槽；无空槽则找序号最小（最老）的覆盖
    int target = -1;
    int32_t minSeq = INT32_MAX;
    for (int i = 0; i < WALLPAPER_SLOTS; i++) {
        if (!_slotHas[i]) { target = i; break; }
        if (_slotSeq[i] < minSeq) { minSeq = _slotSeq[i]; target = i; }
    }
    if (target < 0) target = 0;
    // 删除目标槽旧文件
    char path[32];
    slotFilePath(target, path, sizeof(path));
    if (LittleFS.exists(path)) LittleFS.remove(path);
    _slotHas[target] = false;
    // 写 NVS：全局序号 + 目标槽序号
    if (ok) {
        prefs.putInt("bg_seq", seq);
        char key[10];
        snprintf(key, sizeof(key), "bg_seq%d", target);
        prefs.putInt(key, seq);
        prefs.end();
    }
    _slotSeq[target] = seq;
    Serial.printf("[WALL] allocate slot=%d seq=%d\n", target, (int)seq);
    return target;
}

/* TJpgDec 输出回调：逐块推送到屏幕 */
// 作用：JPEG 解码回调：把解码出的像素直接输出到屏幕
static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= tft.height()) return false;  // stop decoding if image runs off bottom of screen
    if (_bufMode && _fullBuf) {
        // 一次性显示模式: 块数据拷入 PSRAM 全屏缓冲, 解码完成后统一 pushImage(整屏)
        for (int16_t yy = 0; yy < h; yy++) {
            int16_t sy = y + yy;
            if (sy >= 240) break;
            int16_t n = min((int16_t)w, (int16_t)(240 - x));
            if (n <= 0) break;
            memcpy(&_fullBuf[(size_t)sy * 240 + x], &bitmap[(size_t)yy * w], (size_t)n * 2);
        }
    } else {
        tft.pushImage(x, y, w, h, bitmap);
    }
    // 累计亮度（RGB565 -> 灰度近似，0-255），用于智能前景色
    uint32_t sum = 0;
    uint32_t n = (uint32_t)w * h;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t p = bitmap[i];
        sum += ((((p >> 11) & 0x1F) << 3) + (((p >> 5) & 0x3F) << 2) + ((p & 0x1F) << 3)) / 3;
    }
    _lumAcc += sum;
    _lumCnt += n;
    // v29m: 快照捕获 —— 把文字区像素直接从解码数据拷入快照（不读屏, 颜色绝对正确）
    for (int s = 0; s < WP_SNAP_MAX; s++) {
        if (!_snapRegions[s].buf) continue;
        int16_t ix0 = max(x, _snapRegions[s].x);
        int16_t iy0 = max(y, _snapRegions[s].y);
        int16_t ix1 = min((int16_t)(x + w), (int16_t)(_snapRegions[s].x + _snapRegions[s].w));
        int16_t iy1 = min((int16_t)(y + h), (int16_t)(_snapRegions[s].y + _snapRegions[s].h));
        if (ix1 <= ix0 || iy1 <= iy0) continue;
        _snapFilled[s] = true;
        for (int16_t yy = iy0; yy < iy1; yy++) {
            uint16_t* dst = &_snapRegions[s].buf[(yy - _snapRegions[s].y) * _snapRegions[s].w + (ix0 - _snapRegions[s].x)];
            const uint16_t* src = &bitmap[(yy - y) * w + (ix0 - x)];
            memcpy(dst, src, (size_t)(ix1 - ix0) * 2);
        }
    }
    return true;
}

// 作用：挂载 LittleFS，恢复壁纸状态
void wallpaperInit() {
    _fsOk = LittleFS.begin();
    if (!_fsOk) {
        // 首次使用/分区损坏：自动格式化一次再挂载
        Serial.println("[WALL] LittleFS mount FAIL, formatting...");
        if (LittleFS.format()) {
            _fsOk = LittleFS.begin();
            Serial.printf("[WALL] format done, mount %s\n", _fsOk ? "OK" : "FAIL");
        } else {
            Serial.println("[WALL] format FAIL");
            return;
        }
    }
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);  // RGB565 big-endian -> little-endian, otherwise color channels scrambled (flower screen)
    TJpgDec.setCallback(tftOutput);
    nvsLoadSlots();
    for (int i = 0; i < WALLPAPER_SLOTS; i++) {
        if (_slotHas[i]) {
            char path[32];
            slotFilePath(i, path, sizeof(path));
            uint16_t w = 0, h = 0;
            TJpgDec.getFsJpgSize(&w, &h, path, LittleFS);
            Serial.printf("[WALL] slot%d size %ux%u\n", i, w, h);
        }
    }
}

// MQTT 回调：保存收到的图片 payload（base64 JSON）
// 作用：接收 MQTT 下发的 base64 图片数据
void wallpaperSetPayload(const byte* payload, unsigned int len) {
    if (!payload || len == 0) return;
    if (len >= WALLPAPER_PAYLOAD_MAX) {
        Serial.printf("[WALL] payload too big %u\n", len);
        return;
    }
    if (!_payloadBuf) {
        _payloadBuf = (uint8_t*)malloc(WALLPAPER_PAYLOAD_MAX);
        if (!_payloadBuf) { Serial.println("[WALL] payload buf OOM"); return; }
    }
    _decFailed = false;   // 新图片到达: 重置解码失败标志, 允许重新尝试
    _decodedOnce = false;
    memcpy(_payloadBuf, payload, len);
    _payloadLen = len;
    _hasPayload = true;
    Serial.printf("[WALL] got payload %u bytes\n", len);
}

// base64 字符 → 6bit 值（无效返回 0xFF）
// 作用：base64 字符转数值
static uint8_t b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 0xFF;
}

// 流式 base64 解码写入文件（避免大内存缓冲）
// 作用：base64 解码并写入 LittleFS 文件
static bool base64ToFile(const char* b64, size_t len, File& f) {
    // 流式解码：边读边写文件，不整图缓存（省内存）
    uint32_t acc = 0;
    int quad = 0;
    size_t written = 0;
    uint8_t out[3];
    for (size_t i = 0; i < len; i++) {
        char c = b64[i];
        if (c == '=') break;               // padding 结束
        uint8_t v = b64val(c);
        if (v == 0xFF) continue;           // 忽略无效字符
        acc = (acc << 6) | v;
        quad++;
        if (quad == 4) {
            out[0] = (acc >> 16) & 0xFF;
            out[1] = (acc >> 8) & 0xFF;
            out[2] = acc & 0xFF;
            f.write(out, 3);
            written += 3;
            quad = 0; acc = 0;
        }
    }
    if (quad == 2) {                       // 2 字符 → 1 字节
        out[0] = (acc >> 4) & 0xFF;
        f.write(out, 1);
        written += 1;
    } else if (quad == 3) {                // 3 字符 → 2 字节
        out[0] = (acc >> 10) & 0xFF;
        out[1] = (acc >> 2) & 0xFF;
        f.write(out, 2);
        written += 2;
    }
    return written > 0;
}

// 处理 MQTT 图片 payload：提取 b64 → 解码 → 写临时文件 → JPEG 校验 → 入队
// 作用：处理待写壁纸：解码→存入槽位→标记表盘需重绘
static bool processPayload() {
    const char* s = (const char*)_payloadBuf;
    // 手动提取 {"b64":"..."} 中的 base64（省 RAM，不做 JSON 解析）
    const char* p = strstr(s, "\"b64\":\"");
    if (!p) { Serial.println("[WALL] no b64 field"); return false; }
    p += 7;
    size_t b64len = 0;
    const char* end = p;
    while (end < s + _payloadLen && *end != '"') { end++; b64len++; }
    if (b64len == 0) { Serial.println("[WALL] empty b64"); return false; }

    File tmp = LittleFS.open(WALLPAPER_TMP, "w");
    if (!tmp) { Serial.println("[WALL] open tmp FAIL"); return false; }
    bool ok = base64ToFile(p, b64len, tmp);
    tmp.close();
    if (!ok) { Serial.println("[WALL] decode empty"); LittleFS.remove(WALLPAPER_TMP); return false; }

    // JPEG 魔数 + 大小校验
    File chk = LittleFS.open(WALLPAPER_TMP, "r");
    uint8_t magic[2] = { 0, 0 };
    size_t size = 0;
    if (chk) { chk.read(magic, 2); size = chk.size(); chk.close(); }
    if (size < 100 || magic[0] != 0xFF || magic[1] != 0xD8) {
        Serial.printf("[WALL] not JPEG size=%u magic=%02X%02X\n", (unsigned)size, magic[0], magic[1]);
        LittleFS.remove(WALLPAPER_TMP);
        return false;
    }
    // 入队：分配槽位（覆盖最老）→ 重命名临时文件到目标槽
    int target = nvsAllocateSlot();
    char path[32];
    slotFilePath(target, path, sizeof(path));
    if (!LittleFS.rename(WALLPAPER_TMP, path)) {
        LittleFS.remove(WALLPAPER_TMP);
        _slotHas[target] = false;
        return false;
    }
    _slotHas[target] = true;
    orderRebuild();   // v29x: 上传新图后重排预览顺序（新图排最新）
    // 仅「从未设置过启用状态」时自动启用新图（首次上传即可见）；用户手动关闭后不再自动启用
    if (_activeNeverSet) {
        _active = (int8_t)target;
        _activeNeverSet = false;
        nvsSaveActive(_active);
    }
    Serial.printf("[WALL] saved %u bytes to slot%d (b64 %u)\n", (unsigned)size, target, (unsigned)b64len);
    return true;
}

// 作用：主循环里处理壁纸下载（避免阻塞主线程）
void wallpaperProcessPending() {
    if (_busy || !_fsOk) return;
    if (_lastTryMs && millis() - _lastTryMs < 15000) return;  // 失败 15s 后重试

    if (!_hasPayload) return;
    _busy = true;
    _lastTryMs = millis();
    bool ok = processPayload();
    _busy = false;
    if (ok) {
        _hasPayload = false;
        _payloadLen = 0;
        if (_payloadBuf) { free(_payloadBuf); _payloadBuf = NULL; }  // v29f: 处理完释放 24KB
        _retryCount = 0;
        extern volatile bool pendingBuzz;      // 马达提示
        pendingBuzz = true;
        extern void wf_invalidateCache();      // 表盘重绘
        wf_invalidateCache();
        Serial.println("[WALL] updated via MQTT, watchface invalidated");
    } else {
        _retryCount++;
        Serial.printf("[WALL] fail retry=%u/3\n", _retryCount);
        if (_retryCount >= 3) {
            _hasPayload = false;
            _payloadLen = 0;
            if (_payloadBuf) { free(_payloadBuf); _payloadBuf = NULL; }  // v29f: 放弃也释放
            _retryCount = 0;
            Serial.println("[WALL] give up after 3 retries");
        }
    }
}

// 作用：是否已有可用壁纸
bool wallpaperHas() {
    return _fsOk && _active >= 0 && _active < WALLPAPER_SLOTS && _slotHas[_active];
}

// 内部：解码绘制指定槽（负责亮度统计）
// 作用：把某槽位壁纸绘制到屏幕
static void drawSlot(int slot) {
    char path[32];
    slotFilePath(slot, path, sizeof(path));
    if (!LittleFS.exists(path)) return;
    _lumAcc = 0; _lumCnt = 0;
    // v29u: 解码到 PSRAM 全屏缓冲, 完成后整屏一次性 pushImage(切换背景图不从上往下刷新)
    _bufMode = false;
    if (!_fullBuf) {
        _fullBuf = (uint16_t*)heap_caps_malloc((size_t)240 * 240 * 2, MALLOC_CAP_SPIRAM);
        if (_fullBuf) Serial.println("[WALL] full-buffer mode (PSRAM 112KB)");
    }
    if (_fullBuf) _bufMode = true;
    TJpgDec.drawFsJpg(0, 0, path, LittleFS);
    if (_bufMode) {
        _bufMode = false;
        if (_lumCnt) tft.pushImage(0, 0, 240, 240, _fullBuf);   // 整屏一次性显示
    }
    if (_lumCnt) { _lumAvg = (uint8_t)(_lumAcc / _lumCnt); _decodedOnce = true; }
    else { _decFailed = true; Serial.printf("[WALL] slot%d decode produced no output, disabled\n", slot); }
    Serial.printf("[WALL] brightness=%u heap=%u\n", _lumAvg, ESP.getFreeHeap());
}

// 作用：绘制当前启用的壁纸
void wallpaperDraw(TFT_eSPI* tft) {
    if (!wallpaperHas()) return;
    if (_decFailed) return;   // 该背景文件已确认解码异常: 跳过, 画黑底, 不卡死
    if (ESP.getFreeHeap() < 40000) {
        Serial.printf("[WALL] skip decode (heap low %u)\n", ESP.getFreeHeap());
        return;
    }
    drawSlot(_active);
}

// 作用：绘制指定槽位壁纸（预览页用）
void wallpaperDrawSlot(TFT_eSPI* tft, int slot) {
    if (slot < 0 || slot >= WALLPAPER_SLOTS) return;
    if (!_slotHas[slot]) return;
    if (_decFailed) return;
    if (ESP.getFreeHeap() < 40000) {
        Serial.printf("[WALL] skip slot%d decode (heap low %u)\n", slot, ESP.getFreeHeap());
        return;
    }
    drawSlot(slot);
}

// 作用：该槽位是否存有图片
bool wallpaperSlotExists(int slot) {
    return slot >= 0 && slot < WALLPAPER_SLOTS && _slotHas[slot];
}

// 作用：已存壁纸数量
int wallpaperSlotCount() {
    int c = 0;
    for (int i = 0; i < WALLPAPER_SLOTS; i++) if (_slotHas[i]) c++;
    return c;
}

// 作用：当前启用槽位号
int wallpaperGetActive() {
    return (int)_active;
}

// 作用：设置启用槽位
void wallpaperSetActive(int slot) {
    if (slot < -1 || slot >= WALLPAPER_SLOTS) return;
    if (slot >= 0 && !_slotHas[slot]) return;   // 空槽不允许启用
    _active = (int8_t)slot;
    nvsSaveActive(_active);
    extern void wf_invalidateCache();
    wf_invalidateCache();
    Serial.printf("[WALL] active -> %d\n", (int)_active);
}

// 背景平均亮度（0-255）
// 作用：壁纸亮度（暗色图自动提亮文字可读性）
uint8_t wallpaperGetBrightness() {
    return _lumAvg;
}

// 背景图是否已成功解码过（表盘据此决定是否可分配快照; 黑底模式零快照）
// 作用：最近一次解码是否成功
bool wallpaperDecodedOk() {
    return _decodedOnce;
}

// 作用：保存文字区域背景快照（文字更新时恢复，不破坏背景图）
void wallpaperSnapSet(int slot, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* buf) {
    if (slot < 0 || slot >= WP_SNAP_MAX) return;
    _snapRegions[slot].x = x; _snapRegions[slot].y = y;
    _snapRegions[slot].w = w; _snapRegions[slot].h = h;
    _snapRegions[slot].buf = buf;
    _snapFilled[slot] = false;
}
// 作用：清除快照
void wallpaperSnapReset() {
    for (int i = 0; i < WP_SNAP_MAX; i++) {
        _snapRegions[i].buf = NULL;
        _snapFilled[i] = false;
    }
}
// 作用：该槽位快照是否已捕获
bool wallpaperSnapCaptured(int slot) {
    if (slot < 0 || slot >= WP_SNAP_MAX) return false;
    return _snapFilled[slot];
}
