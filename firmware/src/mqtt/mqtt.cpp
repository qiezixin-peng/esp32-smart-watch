#include "mqtt.h"
#include "power/power.h"
#include "../notify/notify.h"
#include "../wallpaper/wallpaper.h"

/* ==========================================================
 * MQTT 模块 — 实现
 * ==========================================================
 * 上报周期：每 40 秒一次（在 loop() 中由定时器触发）
 * 超时策略：TCP 连接限时 5 秒，MQTT 握手自动超时 10 秒
 * 下行：attributes = 指令（beep）；notify = 手机通知（JSON）
 * ========================================================== */

// 传感器数据（定义在 main.cpp，通过 extern 引用）
extern float sensorTemp, sensorHum, sensorPress;
extern float sensorBpm, sensorSpo2, sensorUv;
extern bool  haveSensorData;
extern int   seconds;
// 运动数据（定义在 sport.cpp，经 sport.h 导出）
extern uint32_t sportSteps;
extern float    sportDistanceKm;
extern float    sportCalories;
extern bool     sportAvailable;

// MQTT 消息回调（收到服务器下发的消息时调用）
// 作用：MQTT 收到消息回调（解析通知/壁纸下发）
void mqttCallback(char* topic, byte* payload, unsigned int len) {
    // 通知：ThingsCloud 自定义数据流下发（控制台「设备消息调试」/应用端 API 下发到此主题）
    if (strcmp(topic, "data/notify/set") == 0) {
        char buf[160];
        int n = min((int)len, (int)sizeof(buf) - 1);
        memcpy(buf, payload, n);
        buf[n] = 0;
        notifyParse(buf);
        return;
    }
    // 表盘背景：ThingsCloud 自定义数据流下发（{"url":"https://..."}）
    // wallpaper: ThingsCloud data stream downlink ({"b64":"..."} v2 MQTT image)
    if (strcmp(topic, "data/wallpaper/set") == 0) {
        wallpaperSetPayload(payload, len);
        return;
    }
    // 其他 topic：沿用原指令逻辑（attributes 等）
    char msg[128];
    memcpy(msg, payload, min(len, 127u));
    msg[len] = 0;
    // 服务器下发 "beep" 指令时触发马达震动
    if (strstr(msg, "beep")) {
        extern volatile bool pendingBuzz;
        pendingBuzz = true;
    }
}

// 定时向 MQTT 上传完整传感器数据（含 RSSI 和运行时间）
// 作用：上报传感器数据到云平台（温度/心率/步数等）
void publishSensorData() {
    // 未连接或暂无传感器数据则不发送
    if (!mqttOk || !haveSensorData) return;
    // 运动数据落盘（断电/重启恢复今日步数）
    extern void sport_savePersist();
    sport_savePersist();
    char json[256];
    // 组装 JSON：温湿度/气压/心率/血氧/UV + 信号强度/运行秒数/运动数据
    snprintf(json, sizeof(json),
        "{\"temp\":%.1f,\"hum\":%.0f,\"press\":%.0f,\"bpm\":%.0f,\"spo2\":%.0f,\"uv\":%.0f,\"rssi\":%d,\"uptime\":%d,\"steps\":%u,\"dist\":%.1f,\"cal\":%.1f}",
        sensorTemp, sensorHum, sensorPress, sensorBpm, sensorSpo2, sensorUv, WiFi.RSSI(), seconds,
        (sportAvailable ? sportSteps : 0), (sportAvailable ? sportDistanceKm * 1000.0f : 0.0f), (sportAvailable ? sportCalories : 0.0f));
    mqtt.publish("attributes", json);
    Serial.printf("[MQTT] Sent: %s\n", json);
}

// ===== 诊断：手动 MQTT 握手，直接读服务器 CONNACK 返回码（绕开 PubSubClient 黑盒）=====
// 作用：诊断 MQTT 握手（打印 CONNACK 收发状态）
static void mqttDiagHandshake() {
    if (!WiFi.isConnected()) { Serial.println("[MQTTD] no WiFi"); return; }
    WiFiClient diag;
    if (!diag.connect(mqttIP, MQTT_PORT, 5000)) { Serial.println("[MQTTD] TCP connect FAIL"); return; }
    Serial.println("[MQTTD] TCP OK, sending CONNECT...");
    uint8_t pkt[128];
    int n = 0;
    pkt[n++] = 0x10;          // CONNECT
    int rlPos = n++;          // remaining length placeholder
    pkt[n++] = 0x00; pkt[n++] = 0x04;
    memcpy(pkt+n, "MQTT", 4); n += 4;
    pkt[n++] = 0x04;          // MQTT 3.1.1
    pkt[n++] = 0xC2;          // username + password + clean session
    pkt[n++] = 0x00; pkt[n++] = 0x3C;  // keepalive 60
    const char* cid = MQTT_CLIENT_ID;
    pkt[n++] = 0x00; pkt[n++] = strlen(cid);
    memcpy(pkt+n, cid, strlen(cid)); n += strlen(cid);
    const char* usr = MQTT_USER;
    pkt[n++] = 0x00; pkt[n++] = strlen(usr);
    memcpy(pkt+n, usr, strlen(usr)); n += strlen(usr);
    const char* pwd = MQTT_PASS;
    pkt[n++] = 0x00; pkt[n++] = strlen(pwd);
    memcpy(pkt+n, pwd, strlen(pwd)); n += strlen(pwd);
    pkt[rlPos] = n - rlPos - 1;
    Serial.printf("[MQTTD] CONNECT pkt len=%d\n", n);
    diag.write(pkt, n);
    delay(500);
    uint8_t resp[8] = {0};
    int got = 0;
    while (diag.available() && got < 8) { resp[got++] = diag.read(); }
    if (got >= 4) {
        Serial.printf("[MQTTD] CONNACK type=0x%02X rlen=%u rc=%u (0=ok 1=proto 2=id 3=server 4=auth 5=unauth)\n", resp[0], resp[2], resp[3]);
    } else {
        Serial.printf("[MQTTD] no CONNACK, got=%d bytes\n", got);
    }
    diag.stop();
}

// 连接到 MQTT 服务器（非阻塞 + 超时控制）
// 作用：连接 MQTT（带内存保护，堆不足时跳过保命）
bool connectMQTT() {
    if (!WiFi.isConnected()) return false;
    // v29l: 失败退避升级 —— 服务器无响应时，退避时间逐级拉长，不再每 30 秒阻塞主循环
    // 阶梯：连续失败第1次后 30s，第2次后 60s，第3次起 180s；连接成功后清零
    // 上次连接失败时间（配合退避阶梯）
    static unsigned long lastFailMs = 0;
    static int failCount = 0;
    unsigned long backoffMs = (failCount <= 1) ? 30000UL : (failCount == 2 ? 60000UL : 180000UL);
    if (lastFailMs != 0 && millis() - lastFailMs < backoffMs) {
        return false;  // v29l: 退避期内静默跳过，避免阻塞主循环
    }

    // 堆保护：壁纸大包需要 24KB MQTT buffer，仅首次分配一次。
    // 断线重连时若反复 setBufferSize(24576) 会 delete+new 24KB，
    // 在低堆(BLE+WiFi+已分配buffer)下 malloc 失败 → 空指针崩溃 → 主循环卡死 → 黑屏无法唤醒。
    // v29f: 余量从 20000 降到 8000 —— 开机 ~43KB 堆能分配 24KB buffer（43KB>32.6KB）；
    // 低堆仍跳过重连，保命优先（抬手/双击亮屏等关键逻辑绝不因 MQTT 卡死）。
    bool needBigBuf = (mqtt.getBufferSize() < 24576);
    int freeHeap = (int)ESP.getFreeHeap();
    if (freeHeap < (needBigBuf ? 24576 : 0) + 8000) {
        Serial.printf("[MQTT] skipped (heap low %d)\n", freeHeap);
        return false;
    }

    // DNS 解析（仅首次执行）
    if (!mqttIPResolved) {
        mqttIPResolved = true;
        if (!WiFi.hostByName(MQTT_HOST, mqttIP)) {
            Serial.println("[MQTT] DNS resolution failed");
            return false;
        }
        Serial.printf("[MQTT] Server IP: %s\n", mqttIP.toString().c_str());
    }

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    if (mqtt.getBufferSize() < 24576) {
        if (!mqtt.setBufferSize(24576)) { // wallpaper base64 large packet (default 256 too small)
            Serial.println("[MQTT] buffer alloc FAIL");
            return false;
        }
    }
    mqtt.setKeepAlive(60);
    mqtt.setSocketTimeout(5);   // v29u: CONNACK 等待 5 秒（实测 ThingsCloud 服务器握手响应 3s+，2s 会误杀连接导致收不到下行通知）

    wifiClient.stop();
    delay(50);

    // TCP 连接限时 5 秒
    if (!wifiClient.connect(mqttIP, MQTT_PORT, 5000)) {
        mqttLastRc = -7;
        mqttOk = false;
        Serial.printf("[MQTT] TCP connect FAIL to %s:%d (heap=%u)\n", mqttIP.toString().c_str(), MQTT_PORT, ESP.getFreeHeap());
        return false;
    }
    Serial.println("[MQTT] TCP connected OK");

    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
        mqtt.subscribe("attributes");
        mqtt.subscribe("data/notify/set");   // 通知：自定义数据流下发主题
        mqtt.subscribe("data/wallpaper/set"); // 表盘背景：自定义数据流下发主题
        mqtt.publish("attributes", "{\"online\":1}");
        mqttOk = true;
        failCount = 0;   // v29l: 连接成功，清零连续失败计数
        Serial.println("[MQTT] Connected");
        return true;
    }

    mqttLastRc = mqtt.state();
    mqttOk = false;
    lastFailMs = millis();   // 记录失败时间（配合开头退避）
    if (failCount < 100) failCount++;   // v29l: 连续失败计数，决定退避阶梯
    Serial.printf("[MQTT] Connect failed rc=%d (wifiClient.connected=%d) fail#%d\n", mqttLastRc, wifiClient.connected(), failCount);
    // v29l: 手动握手诊断限频 120 秒一次（诊断已确认服务器零响应，无需每次失败都跑）
    {
        static unsigned long lastDiagMs = 0;
        if (millis() - lastDiagMs > 120000) { lastDiagMs = millis(); mqttDiagHandshake(); }
    }
    wifiClient.stop();
    return false;
}
