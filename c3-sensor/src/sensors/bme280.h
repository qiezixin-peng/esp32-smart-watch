#ifndef BME280_H
#define BME280_H

#include <Arduino.h>
#include <Adafruit_BME280.h>

/*
 * BME280Sensor 类 — 温湿度气压传感器
 * =========================================
 * 功能：读取 BME280 的温度、湿度、大气压强
 * I2C 地址：0x76（默认）
 * 接线：SDA=GPIO5, SCL=GPIO6（ESP32-C3）
 *
 * 使用说明：
 *   BME280Sensor bme(5, 6, 0x76);  // 创建对象
 *   bme.begin();                     // 初始化（串口输出结果）
 *   float t = bme.readTemperature(); // 读取温度
 *   float h = bme.readHumidity();    // 读取湿度
 *   float p = bme.readPressure();    // 读取气压（hPa）
 *
 * 注意事项：
 * - begin() 必须在 Wire.begin() 之后调用
 * - readPressure() 返回的是百帕(hPa)，不是帕(Pa)
 */
class BME280Sensor {
public:
    // 构造函数：指定 I2C 引脚和地址
    BME280Sensor(uint8_t sda = 5, uint8_t scl = 6, uint8_t addr = 0x76);
    
    // 初始化传感器，返回 true 表示成功
    bool begin();
    
    // 读取温度（摄氏度）
    float readTemperature();
    
    // 读取湿度（百分比）
    float readHumidity();
    
    // 读取气压（hPa，百帕）
    float readPressure();
    
    // 检查传感器是否初始化成功
    bool isReady() const { return _ok; }

private:
    Adafruit_BME280 _bme;  // Adafruit 库的 BME280 对象
    uint8_t _sda, _scl, _addr;  // I2C 引脚和地址
    bool _ok;  // 初始化标志
};

#endif
