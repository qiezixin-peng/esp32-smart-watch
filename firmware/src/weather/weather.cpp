/*
 * 天气模块实现：请求和风天气 API（当前天气+3日预报），绘制图标/温度/湿度
 * 说明：HTTPS 使用 WiFiClientSecure，低内存时可能失败，失败后保留上次数据
 * ================================
 * 本文件阅读要点：
 *  - 数据源：和风天气 API（当前天气 v7/now + 3日预报 v7/3d），返回 JSON，可能 gzip 压缩
 *  - 城市定位：/v2/ip 已废弃(404) → 直接默认肇庆(112.47,23.05)，省一次 HTTPS 握手
 *  - 刷新策略：30 分钟刷新一次；失败 30 秒后重试；失败保留上次数据（wd.valid）
 *  - HTTPS：WiFiClientSecure setInsecure（不校验证书，省内存）；低堆时 SSL 握手可能失败
 * ================================
 */

#include "weather.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp32/rom/miniz.h>

// ===== 和风天气 API 配置 =====
// KEY：和风天气开发者密钥；三个接口：当前天气 / 3日预报 / IP 定位
// 刷新间隔 30 分钟；失败重试间隔 30 秒
#define QWEATHER_KEY "YOUR_QWEATHER_KEY"
#define QWEATHER_NOW     "https://hj693955g7.re.qweatherapi.com/v7/weather/now?location="
#define QWEATHER_3D      "https://hj693955g7.re.qweatherapi.com/v7/weather/3d?location="
#define QWEATHER_GEO_IP  "https://hj693955g7.re.qweatherapi.com/v2/ip"
#define WEATHER_REFRESH_MS  (30 * 60 * 1000)
#define WEATHER_RETRY_MS    (30 * 1000)

#define DEFAULT_LON 112.47
#define DEFAULT_LAT 23.05
#define DEFAULT_CITY "广东省 肇庆市"

// ===== 天气数据状态 =====
// wd：当前天气 + 3 日预报数据；_locationParam：城市坐标"经度,纬度"
// _lastFailTime：上次失败时间（控制 30s 重试）；_geoOk：是否已确定城市
static WeatherData wd;
static String _locationParam = "";
static unsigned long _lastFailTime = 0;
static bool _geoOk = false;

// 作用：初始化天气数据（标记需要刷新）
void weather_init() {
    // 初始化：数据清零、默认肇庆坐标、标记待刷新（valid=false）
    memset(&wd, 0, sizeof(wd));
    wd.valid = false;
    wd.cityName = DEFAULT_CITY;
    wd.lon = DEFAULT_LON; wd.lat = DEFAULT_LAT;
    _locationParam = "";
    _lastFailTime = 0;
    _geoOk = false;
}

WeatherData* weather_getData() { return &wd; }

// 作用：判断是否到刷新时间（默认 30 分钟一次）
bool weather_needRefresh() {
    // 判断是否需要重新拉天气：
    //  - 无数据(valid=false)：失败后 30 秒内不重试（防频繁失败），否则立即刷新
    //  - 有数据：距上次成功刷新超过 30 分钟才刷新
    if (!wd.valid) {
        if (_lastFailTime > 0 && millis() - _lastFailTime < WEATHER_RETRY_MS) return false;
        return true;
    }
    return (millis() - wd.updateTime >= WEATHER_REFRESH_MS);
}

/* Gzip 解压（手动处理 tinfl） */
// 作用：解压和风 API 返回的 gzip 数据（ESP32 ROM 自带 tinfl 算法）
static String decompressGzip(const String& input) {
    int len = input.length();
    // gzip 最小也要 18 字节（头 10 + 尾 8），太短直接放弃
    if (len < 18) return "";

    const uint8_t* src = (const uint8_t*)input.c_str();
    if (src[0] != 0x1F || src[1] != 0x8B) { return input; }
    // 魔数 1F 8B = gzip 格式；不是 gzip 就直接返回原文

    int hdr = 10;
    // 解析 gzip 头部：跳过可选字段（FEXTRA 扩展/FNAME 文件名/FCOMMENT 注释/FHCRC 校验）
    uint8_t flags = src[3];
    if (flags & 0x04) { int xlen = src[hdr] | (src[hdr+1]<<8); hdr += 2 + xlen; }
    if (flags & 0x08) { while (hdr < len && src[hdr] != 0) hdr++; hdr++; }
    if (flags & 0x10) { while (hdr < len && src[hdr] != 0) hdr++; hdr++; }
    if (flags & 0x02) { hdr += 2; }
    if (hdr >= len - 8) return "";

    int deflateLen = len - hdr - 8;
    // deflate 数据长度 = 总长 - 头部 - 尾部 8 字节(CRC32+长度)
    size_t outCap = (deflateLen * 4) + 2048;
    // 输出缓冲区按 4 倍估算（文本压缩比通常 >4），失败返回空串
    uint8_t* out = (uint8_t*)malloc(outCap);
    if (!out) { Serial.println("[GZIP] malloc failed"); return ""; }

    tinfl_decompressor* decomp = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
    if (!decomp) { free(out); return ""; }
    tinfl_init(decomp);

    size_t inSize = deflateLen;
    size_t outSize = outCap;
    tinfl_status st = tinfl_decompress(decomp, src + hdr, &inSize, out, out, &outSize, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    // 调用 ESP32 ROM 内置 tinfl 解压；解压完成后 outSize 是实际长度

    if (st != TINFL_STATUS_DONE) { Serial.printf("[GZIP] tinfl error %d (%u->%u)\n", st, inSize, outSize); free(out); free(decomp); return ""; }

    String result((const char*)out, outSize);
    free(out); free(decomp);
    Serial.printf("[GZIP] Decompressed %d -> %u bytes\n", deflateLen, (unsigned)outSize);
    return result;
}

// 作用：HTTP GET 请求，返回响应正文
static String httpGet(const String& url) {
    // HTTP GET 通用封装：HTTPS 用 setInsecure(不验证证书)，超时 8 秒
    // 返回值：响应正文（自动解 gzip）；失败返回空串
    if (WiFi.status() != WL_CONNECTED) { Serial.println("[HTTP] No WiFi"); return ""; }
    HTTPClient http; WiFiClientSecure client;
    if (url.startsWith("https://")) { client.setInsecure(); http.begin(client, url); }
    else { http.begin(url); }
    http.addHeader("X-QW-Api-Key", QWEATHER_KEY);
    http.setTimeout(8000);
    int code = http.GET();
    // 发出请求；非 200 视为失败（如 404=定位接口已废弃）
    Serial.printf("[HTTP] GET %s -> %d\n", url.substring(0, 55).c_str(), code);
    if (code != 200) { http.end(); return ""; }
    String body = http.getString(); http.end();
    if (body.length() >= 2 && (uint8_t)body[0] == 0x1F && (uint8_t)body[1] == 0x8B) {
        // 响应体是 gzip：解压后替换原文，供 JSON 解析使用
        Serial.println("[GZIP] Detected gzip, decompressing...");
        String decompressed = decompressGzip(body);
        if (decompressed.length() > 0) { body = decompressed; }
    }
    Serial.printf("[HTTP] body %d bytes\n", body.length());
    return body;
}

// 作用：按 IP 定位城市（该接口已废弃，失败则用默认城市）
static bool geoLocate() {
    // IP 定位城市（已废弃接口，仅作兼容保留）：成功后写入城市名/坐标/查询参数
    // 请求失败（404/超时）直接放弃，天气入口会自动用默认城市
    String resp = httpGet(String(QWEATHER_GEO_IP));
    if (resp.isEmpty()) return false;
    JsonDocument doc;
    if (deserializeJson(doc, resp)) { Serial.println("[WTH] Geo JSON fail"); return false; }
    if (strcmp(doc["code"], "200") != 0) return false;
    float lat = doc["lat"] | 0.0f, lon = doc["lon"] | 0.0f;
    if (lat == 0 && lon == 0) return false;
    String adm1 = doc["adm1"] | "", adm2 = doc["adm2"] | "", name = doc["name"] | "";
    wd.cityName = adm1 + " " + adm2;
    if (!name.isEmpty() && !adm2.endsWith(name)) wd.cityName += " " + name;
    wd.lat = lat; wd.lon = lon;
    char buf[32]; snprintf(buf, sizeof(buf), "%.2f,%.2f", lon, lat);
    _locationParam = String(buf);
    Serial.printf("[WTH] Geo OK: %s (%s)\n", wd.cityName.c_str(), _locationParam.c_str());
    return true;
}

// 作用：拉取当前天气（温度/天气代码）
static bool fetchNow() {
    // 拉当前天气：温度/体感/湿度/天气描述/图标代码/风向风速
    String url = String(QWEATHER_NOW) + _locationParam;
    String resp = httpGet(url);
    if (resp.isEmpty()) return false;
    JsonDocument doc;
    if (deserializeJson(doc, resp)) { Serial.println("[WTH] Now JSON fail"); return false; }
    if (strcmp(doc["code"], "200") != 0) { Serial.printf("[WTH] Now code=%s\n", doc["code"].as<const char*>()); return false; }
    JsonObject now = doc["now"];
    // now 对象里是实时天气字段（和风 v7 格式：temp/text/icon/humidity/windDir/windSpeed）
    wd.temp = atof(now["temp"].as<const char*>());
    wd.feelsLike = atof(now["feelsLike"].as<const char*>());
    wd.humidity = atoi(now["humidity"].as<const char*>());
    wd.weatherText = now["text"].as<String>();
    if (wd.weatherText.isEmpty()) wd.weatherText = "未知";
    wd.weatherIcon = atoi(now["icon"].as<const char*>());
    wd.windDir = now["windDir"].as<String>();
    wd.windSpeed = now["windSpeed"].as<String>();
    Serial.printf("[WTH] Now: %.0fC %s\n", wd.temp, wd.weatherText.c_str());
    return true;
}

// 作用：拉取 3 日预报
static bool fetchForecast() {
    // 拉 3 日预报：每天日期/白天天气/图标/最高最低温
    String url = String(QWEATHER_3D) + _locationParam;
    String resp = httpGet(url);
    if (resp.isEmpty()) return false;
    JsonDocument doc;
    if (deserializeJson(doc, resp)) return false;
    if (strcmp(doc["code"], "200") != 0) return false;
    JsonArray daily = doc["daily"].as<JsonArray>();
    // daily 数组：和风按天返回，最多取前 3 天
    int count = min((int)daily.size(), 3);
    for (int i = 0; i < count; i++) {
        JsonObject day = daily[i];
        wd.forecast[i].date = day["fxDate"].as<String>();
        wd.forecast[i].textDay = day["textDay"].as<String>();
        wd.forecast[i].iconDay = atoi(day["iconDay"].as<const char*>());
        wd.forecast[i].tempMax = atof(day["tempMax"].as<const char*>());
        wd.forecast[i].tempMin = atof(day["tempMin"].as<const char*>());
    }
    Serial.printf("[WTH] Forecast: %d days\n", count);
    return true;
}

// 作用：天气总入口：拉当前天气+预报，成功置 valid 标志
bool weather_fetch() {
    // 天气总入口：先确定城市，再拉当前+预报，全部成功后置 valid 并记录刷新时间
    Serial.println("[WTH] === Fetch start ===");
    if (WiFi.status() != WL_CONNECTED) { Serial.println("[WTH] No WiFi"); return false; }
    // 没 WiFi 直接失败：不浪费时间尝试（保留上次数据）
    // v29z: 跳过 geo IP 定位（/v2/ip 接口已废弃 404，白费一次 HTTPS SSL 握手）
    // 低堆下连续两次 SSL 握手必失败（-32512/-10368，见踩坑日志#48），直接默认肇庆省一次握手
    if (!_geoOk) {
    // v29z：跳过 geo 定位——/v2/ip 接口已废弃(404)，白费一次 HTTPS 握手
    // 低堆下连续两次 SSL 握手必失败（-32512/-10368，见踩坑日志#48）→ 直接默认肇庆
        _locationParam = "112.47,23.05";
        wd.cityName = DEFAULT_CITY; wd.lat = DEFAULT_LAT; wd.lon = DEFAULT_LON;
        _geoOk = true;
        Serial.println("[WTH] Use default: 肇庆 (skip geo, save one SSL handshake)");
    }
    if (!fetchNow()) { Serial.println("[WTH] fetchNow FAILED"); _lastFailTime = millis(); return false; }
    fetchForecast();
    wd.valid = true; wd.updateTime = millis(); _lastFailTime = 0;
    Serial.println("[WTH] === Fetch OK ===");
    return true;
}

// 作用：按天气代码画对应图标（太阳/月亮/云/雨/雪/雾）
void weather_drawIcon(TFT_eSPI* tft, int iconCode, int x, int y, int size) {
    // 天气图标绘制：按和风 icon 代码分类手绘（圆/线/矩形组成），
    // 覆盖晴/多云/阴/雨/雪/雾/夜晚；未知代码画"?"
    int cx = x + size / 2, cy = y + size / 2, r = size * 0.35;
    // 以图标区域中心为基准画，r=半径（size 的 35%），所有元素相对 cx/cy 偏移
    // v29aa: 覆盖和风全部 icon 代码（含夜晚 150~153、雾/霾 500~515）
    bool isSunny     = (iconCode == 100 || iconCode == 150);          // 晴（白天/夜晚）
    bool isCloudy    = (iconCode >= 101 && iconCode <= 103) || (iconCode >= 151 && iconCode <= 153); // 多云/少云/晴间多云
    bool isOvercast  = (iconCode == 104);                              // 阴
    bool isRain      = (iconCode >= 300 && iconCode <= 399);           // 雨
    bool isSnow      = (iconCode >= 400 && iconCode <= 499);           // 雪
    bool isFog       = (iconCode >= 500 && iconCode <= 515);           // 薄雾/雾/霾/扬沙/浮尘/沙尘暴
    bool isNight     = (iconCode >= 150 && iconCode <= 153);           // 夜晚代码
    // 夜晚(150~153)：晴天画月亮、多云画月亮+云；白天画太阳+光芒

    if (isSunny) {
    // 晴：白天=黄色圆+8 条光芒；夜晚=白月亮+偏移深色圆切出月牙+星星
        if (isNight) {
            // 夜晚晴空：月亮（白圆 + 偏移深色圆切出月牙）
            tft->fillCircle(cx, cy, r, TFT_WHITE);
            tft->fillCircle(cx + r * 0.5, cy - r * 0.3, r * 0.8, TFT_BLACK);
            tft->fillCircle(cx - r * 1.2, cy - r * 0.9, 2, TFT_WHITE);
            tft->fillCircle(cx - r * 1.5, cy + r * 0.6, 2, TFT_WHITE);
            tft->fillCircle(cx + r * 1.3, cy + r * 0.8, 2, TFT_WHITE);
        } else {
            tft->fillCircle(cx, cy, r, TFT_YELLOW);
            for (int a = 0; a < 360; a += 30) {
                float rad = a * (PI / 180.0);
                tft->drawLine(cx+cos(rad)*r*1.3, cy+sin(rad)*r*1.3, cx+cos(rad)*r*1.9, cy+sin(rad)*r*1.9, TFT_YELLOW);
            }
        }
    } else if (isCloudy) {
    // 多云：太阳/月亮 + 两团灰色云
        if (isNight) {
            // 夜晚多云：月亮 + 云
            tft->fillCircle(cx - r * 0.8, cy - r * 0.4, r * 0.45, TFT_WHITE);
            tft->fillCircle(cx - r * 0.55, cy - r * 0.6, r * 0.35, TFT_BLACK); // 切出月牙
            tft->fillCircle(cx, cy, r * 0.8, TFT_DARKGREY);
            tft->fillCircle(cx - r * 0.5, cy - r * 0.3, r * 0.5, TFT_DARKGREY);
        } else {
            tft->fillCircle(cx - r/2, cy - r/3, r*0.7, TFT_YELLOW);
            for (int a = 0; a < 360; a += 45) {
                float rad = a * (PI / 180.0);
                tft->drawLine(cx-r/2+cos(rad)*r*0.9, cy-r/3+sin(rad)*r*0.9, cx-r/2+cos(rad)*r*1.4, cy-r/3+sin(rad)*r*1.4, TFT_YELLOW);
            }
            tft->fillCircle(cx, cy, r*0.8, TFT_DARKGREY);
            tft->fillCircle(cx - r*0.5, cy - r*0.3, r*0.5, TFT_DARKGREY);
        }
    } else if (isOvercast) {
    // 阴：三团灰云压一条横线
        tft->fillCircle(cx, cy, r*0.8, TFT_DARKGREY);
        tft->fillCircle(cx - r*0.5, cy - r*0.3, r*0.6, TFT_DARKGREY);
        tft->fillCircle(cx + r*0.5, cy - r*0.2, r*0.5, TFT_DARKGREY);
        tft->fillRect(cx - r*0.8, cy - r*0.1, r*1.6, r*0.4, TFT_DARKGREY);
    } else if (isFog) {
    // 雾/霾：灰云 + 三条浅蓝横线（表示雾气分层）
        // 雾/霾：云 + 三条横线
        tft->fillCircle(cx, cy - r * 0.4, r * 0.7, TFT_DARKGREY);
        tft->fillCircle(cx - r * 0.5, cy - r * 0.6, r * 0.5, TFT_DARKGREY);
        tft->fillCircle(cx + r * 0.5, cy - r * 0.5, r * 0.5, TFT_DARKGREY);
        tft->fillRect(cx - r * 0.7, cy - r * 0.2, r * 1.4, r * 0.35, TFT_DARKGREY);
        tft->setTextColor(TFT_SKYBLUE, TFT_BLACK);
        tft->drawLine(cx - r * 0.9, cy + r * 0.2, cx + r * 0.9, cy + r * 0.2, TFT_SKYBLUE);
        tft->drawLine(cx - r * 0.7, cy + r * 0.5, cx + r * 0.7, cy + r * 0.5, TFT_SKYBLUE);
        tft->drawLine(cx - r * 0.5, cy + r * 0.8, cx + r * 0.5, cy + r * 0.8, TFT_SKYBLUE);
    } else if (isRain) {
    // 雨：灰云 + 4 条青色斜线（雨滴）
        tft->fillCircle(cx, cy, r*0.8, TFT_DARKGREY);
        tft->fillCircle(cx - r*0.5, cy - r*0.3, r*0.6, TFT_DARKGREY);
        tft->fillCircle(cx + r*0.5, cy - r*0.2, r*0.5, TFT_DARKGREY);
        tft->fillRect(cx - r*0.8, cy - r*0.1, r*1.6, r*0.4, TFT_DARKGREY);
        for (int i=0;i<4;i++) tft->drawLine(cx-r*0.7+i*r*0.5,cy+r*0.3, cx-r*0.7+i*r*0.5-2,cy+r*0.7, TFT_CYAN);
    } else if (isSnow) {
    // 雪：灰云 + 4 组白色小圆（雪花）
        tft->fillCircle(cx, cy, r*0.8, TFT_DARKGREY);
        tft->fillCircle(cx - r*0.5, cy - r*0.3, r*0.6, TFT_DARKGREY);
        tft->fillCircle(cx + r*0.5, cy - r*0.2, r*0.5, TFT_DARKGREY);
        tft->fillRect(cx - r*0.8, cy - r*0.1, r*1.6, r*0.4, TFT_DARKGREY);
        for (int i=0;i<4;i++) { tft->fillCircle(cx-r*0.7+i*r*0.5,cy+r*0.4,2,TFT_WHITE); tft->fillCircle(cx-r*0.7+i*r*0.5-2,cy+r*0.7,2,TFT_WHITE); }
    } else {
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        tft->drawString("?", cx, cy - 4);
    // 未知天气代码：画问号兜底
    }
}

// 作用：把毫秒差格式化成「x分钟前」
static String timeAgo(unsigned long ms) {
    // 把"上次更新时间"格式化成"x分钟前"（天气页底部显示）
    int m = (millis() - ms) / 60000;
    return (m < 1) ? "刚刚" : String(m) + "分钟前";
}

// 作用：绘制天气页：地址/当前温度/图标/3日预报
void weather_drawPage(TFT_eSPI* tft) {
    // 天气页整页绘制：无数据显示"正在获取…"；有数据按区块布局：
    // 城市名 → 图标+温度+描述 → 风向/湿度 → 分隔线 → 3 日预报(日期/图标/描述/温度范围) → 更新时间
    tft->fillScreen(TFT_BLACK);
    if (!wd.valid) {
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        tft->drawString("正在获取天气数据…", 30, 110, 2);
    // 数据无效（还没拉到或失败）：显示加载提示，不画空数据
        return;
    }

    char buf[20];
    int y = 2;

    // ═══ 城市名 ═══
    // 顶部：城市名（如"广东省 肇庆市"）
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->drawString(wd.cityName, 5, y, 2);
    y += 18;

    // ═══ 图标 + 温度 + 天气描述（同一行） ═══
    // 第一行内容：左侧天气图标，中间大字温度+手画°C，右侧黄色天气描述
    weather_drawIcon(tft, wd.weatherIcon, 5, y, 50);

    // 温度数字（font 7 高48px，顶部对齐图标上缘，确保不越界）
    // font7 高度 48px，y+13 下移使温度在图标垂直居中
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.0f", wd.temp);
    int tempX = 65, tempY = y + 13;  // 温度下移8px
    tft->drawString(buf, tempX, tempY, 7);
    int tw = tft->textWidth(buf, 7);

    // 手画 ° 圆圈
    // 温度单位：手画圆圈 + "C"（内置字体无 ° 字形）
    tft->drawCircle(tempX + tw + 5, tempY + 2, 3, TFT_WHITE);
    tft->drawString("C", tempX + tw + 12, tempY, 7);

    // 天气描述放在温度块右侧，留足空间
    int descX = tempX + tw + 45;   // 在 "°C" 之后
    tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    tft->drawString(wd.weatherText, descX, tempY + 1, 2);

    y += 40 ;   // 图标间距（font 7 刚好在图标内，不会越界）

    // ═══ 风向 / 湿度 标签 ═══
    // 第二行：灰色标签"风向""湿度"，各占一列
    tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft->drawString("风向", 5, y, 1);
    tft->drawString("湿度", 130, y, 1);
    y += 20;  // 显著间距→值

    // ═══ 风向 / 湿度 值 ═══
    // 标签下一行：白色具体数值（风向+风速 / 湿度%）
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->drawString(wd.windDir + " " + wd.windSpeed, 5, y, 1);
    snprintf(buf, sizeof(buf), "%d%%", wd.humidity);
    tft->drawString(buf, 130, y, 1);
    y += 26;  // 间距→分隔线

    // ═══ 分隔线 ═══
    // 与预报区之间画一条深灰分隔线
    tft->drawLine(0, y, 239, y, TFT_DARKGREY);
    y += 6;

    // ═══ 3天预报日期 ═══
    // 3 天预报三列（每列宽 78）：日期去掉年份只留"月/日"
    for (int i = 0; i < 3; i++) {
        int x = i * 78 + 3;
        tft->setTextColor(TFT_CYAN, TFT_BLACK);
        String d = wd.forecast[i].date;
        if (d.length() >= 5) { d = d.substring(5); d.replace('-', '/'); }
        tft->drawString(d, x + 8, y, 2);
    }
    y += 18;

    // ═══ 预报图标 ═══
    // 每列小图标（28px）
    for (int i = 0; i < 3; i++) {
        int x = i * 78 + 3;
        weather_drawIcon(tft, wd.forecast[i].iconDay, x + 15, y, 28);
    }
    y += 28 + 4;

    // ═══ 预报天气描述 ═══
    // 每列天气文字（如"多云""小雨"）
    for (int i = 0; i < 3; i++) {
        int x = i * 78 + 3;
        tft->setTextColor(TFT_YELLOW, TFT_BLACK);
        tft->drawString(wd.forecast[i].textDay, x + 12, y, 1);
    }
    y += 26;

    // ═══ 温度范围：min-max（用 - 保证底部对齐） ═══
    // 每列最低温~最高温（如 23~31），"~"稍上移保证视觉对齐
    for (int i = 0; i < 3; i++) {
        int x = i * 78 + 3;
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        char lb[8], rb[8];
        snprintf(lb, sizeof(lb), "%.0f", wd.forecast[i].tempMin);
        snprintf(rb, sizeof(rb), "%.0f", wd.forecast[i].tempMax);
        tft->drawString(lb, x + 12, y, 1);
        int lw = tft->textWidth(lb, 1);
        tft->drawString("~", x + 12 + lw, y - 4, 1);
        int tw = tft->textWidth("~", 1);
        tft->drawString(rb, x + 12 + lw + tw, y, 1);
    }
    y += 34;

    // ═══ 更新时间 ═══
    // 页脚：上次成功更新距今"x分钟前"
    tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft->drawString("更新 " + timeAgo(wd.updateTime), 5, y, 1);
}












