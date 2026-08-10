#ifndef LTR390_H
#define LTR390_H

#include <Arduino.h>
#include <Adafruit_LTR390.h>

/*
 * LTR390Sensor 类 — 紫外线(UV)传感器
 * ====================================
 * 功能：读取 LTR390 紫外光强度，转换为 UV Index
 * I2C 地址：0x53（共用 GPIO5=SDA, GPIO6=SCL）
 *
 * UV Index 计算公式：
 *   UV Index = readUVS() / 2300.0
 *   室内通常 0~0.5，阴天约 1~2，晴天可达 5~11
 *
 * 使用说明：
 *   LTR390Sensor uv;     // 创建对象
 *   uv.begin();           // 初始化
 *   uv.update();          // 检查并更新数据
 *   float val = uv.readUV();  // 获取 UV Index
 *
 * 注意事项：
 * - update() 应定期调用（如每 1~2 秒）
 * - 只有 newDataAvailable() 为 true 时才会更新
 * - 配置为 UVS 模式 + 3倍增益 + 16位分辨率
 */
class LTR390Sensor {
public:
    LTR390Sensor() : _ready(false), _uv(0), _raw(0) {}

    // 初始化传感器，配置 UV 模式
    bool begin() {
        _ready = _sensor.begin();
        if (_ready) {
            _sensor.setMode(LTR390_MODE_UVS);    // 设置为紫外模式
            _sensor.setGain(LTR390_GAIN_3);       // 3倍增益
            _sensor.setResolution(LTR390_RESOLUTION_16BIT); // 16位分辨率
            Serial.println("[LTR390] initialized OK");
        } else {
            Serial.println("[LTR390] initialization FAILED");
        }
        return _ready;
    }

    // 轮询更新 UV 数据（有新的数据时才更新）
    void update() {
        if (!_ready) return;
        if (_sensor.newDataAvailable()) {
            _raw = _sensor.readUVS();
            _uv = (float)_raw / 2300.0f;  // 原始值转 UV Index
        }
    }

    float readUV() const { return _uv; }       // 获取 UV Index
    uint32_t readRaw() const { return _raw; }   // 获取原始计数值
    bool  isReady() const { return _ready; }    // 检查传感器状态

private:
    Adafruit_LTR390 _sensor;  // Adafruit 库的 LTR390 对象
    bool  _ready;      // 初始化标志
    float _uv;         // UV Index 值
    uint32_t _raw;     // 原始计数（用于调试）
};

#endif
