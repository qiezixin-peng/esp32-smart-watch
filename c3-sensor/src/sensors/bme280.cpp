#include "bme280.h"

/*
 * BME280Sensor 实现
 * ====================
 * 使用 Adafruit BME280 库驱动传感器。
 * 构造函数保存引脚配置，begin() 执行实际初始化。
 */

// 构造函数：保存 I2C 引脚和地址
BME280Sensor::BME280Sensor(uint8_t sda, uint8_t scl, uint8_t addr)
    : _sda(sda), _scl(scl), _addr(addr), _ok(false) {}

// 初始化：启动 Wire，尝试连接 BME280
bool BME280Sensor::begin() {
    Wire.begin(_sda, _scl);
    _ok = _bme.begin(_addr);
    if (_ok) {
        Serial.println("[BME280] initialized OK");         // 初始化成功
    } else {
        Serial.println("[BME280] initialization FAILED!"); // 初始化失败
    }
    return _ok;
}

// 读取温度（摄氏度）
float BME280Sensor::readTemperature() {
    return _bme.readTemperature();
}

// 读取湿度（百分比）
float BME280Sensor::readHumidity() {
    return _bme.readHumidity();
}

// 读取气压，Adafruit 库返回 Pa，除 100 转成 hPa
float BME280Sensor::readPressure() {
    return _bme.readPressure() / 100.0F;
}
