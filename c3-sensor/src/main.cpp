/*
 * ESP32-C3 SuperMini — BLE Server + 三传感器
 * ============================================
 * 架构：C3（采集传感器）──BLE通知──→ T-Watch（显示+上云）
 * 
 * 传感器（共用 I2C 总线，SDA=GPIO5, SCL=GPIO6）：
 *   - BME280：温度、湿度、气压
 *   - MAX30102：心率、血氧
 *   - LTR390：紫外线指数
 *
 * 工作流程：
 *   1. setup(): 初始化 I2C → 依次初始化三个传感器 → 启动 BLE 广播
 *   2. loop(): 每 2 秒读取全部传感器 → JSON 序列化 → BLE Notify 发送
 *
 * 版本：v47（使用 Maxim 官方心率血氧算法 + 3次中值平滑）
 */
#include <Arduino.h>
#include "ble/ble_server.h"
#include "sensors/bme280.h"
#include "sensors/max30102.h"
#include "sensors/ltr390.h"

// BLE 服务 UUID（与 T-Watch 端必须一致）
#define SERVICE_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define CHARACTERISTIC_UUID "b2c3d4e5-f6a7-8901-bcde-f12345678901"

// 创建三个传感器对象
BME280Sensor   bme(5, 6, 0x76);   // 温湿度气压
MAX30102Sensor heart;               // 心率血氧
LTR390Sensor   uv;                  // 紫外线

static int sendCount = 0;           // 已发送数据包计数器
static unsigned long lastPrint = 0; // 上次读取时间

// 扫描 I2C 总线上的所有设备（调试用）
void scanI2C() {
    Serial.println("[I2C] Scanning bus...");
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C] Found device at 0x%02X\n", addr);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== C3 v47 Sensor Hub ===\n");

    Wire.begin(5, 6);
    Wire.setClock(50000);  // I2C 降速至 50kHz，提高稳定性

    scanI2C();  // 调试：打印所有 I2C 设备

    delay(100);
    bme.begin();     // 初始化 BME280
    delay(50);
    heart.begin();   // 初始化 MAX30102
    delay(50);
    uv.begin();      // 初始化 LTR390

    // 启动 BLE 广播，等待 T-Watch 连接
    bleServerInit("C3-Sensor", SERVICE_UUID, CHARACTERISTIC_UUID);
}

void loop() {
    // MAX30102 需要连续采集样本（不等待定时器）
    heart.update();

    // 每 2 秒读取一次完整的传感器数据并发送
    if (millis() - lastPrint >= 2000) {
        lastPrint = millis();

        // 读取 BME280
        float t  = bme.readTemperature();
        float h  = bme.readHumidity();
        float p  = bme.readPressure();

        // 读取 MAX30102
        float bpm  = heart.readHeartRate();
        float spo2 = heart.readSpO2();

        // 读取 LTR390
        uv.update();
        float uvVal = uv.readUV();

        // 串口打印传感器读数
        Serial.println("--- Sensor Readings ---");
        Serial.printf("Temp: %.1f C  Hum: %.0f %%  Press: %.0f hPa  IR=%ld\n",
            t, h, p, heart.readRawIR());
        if (heart.hrValid()) {
            Serial.printf("HR: %.0f bpm (OK)  SpO2: %.0f%%%s  UV: %.1f\n",
                bpm,
                spo2, heart.spo2Valid() ? "(OK)" : "(calculating)",
                uvVal);
        } else {
            Serial.printf("HR: %.0f bpm (waiting)  SpO2: %.0f%%(waiting)  UV: %.1f\n",
                bpm, spo2, uvVal);
        }
        Serial.printf("BLE %s\n", bleDeviceConnected ? "connected" : "waiting for connection...");

        // 如果 BLE 已连接，发送 JSON 数据
        if (bleDeviceConnected) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                "{\"temp\":%.1f,\"hum\":%.0f,\"press\":%.0f,\"bpm\":%.0f,\"spo2\":%.0f,\"uv\":%.0f}",
                t, h, p, bpm, spo2, uvVal);
            bleServerNotify(msg);
            Serial.printf("[%d] Sent: %s\n", sendCount, msg);
            sendCount++;
        }
    }

    // 处理 BLE 断线重连
    bleServerHandleDisconnections();
    delay(10);
}
