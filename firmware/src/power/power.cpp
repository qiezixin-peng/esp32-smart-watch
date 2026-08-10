/*
 * 电源模块实现：AXP202 电源管理（电池电量/充电状态/屏幕背光开关）
 */

#include "power.h"
#include <Preferences.h>
#include <TFT_eSPI.h>
extern TFT_eSPI tft;   // 定义在 main.cpp

/* ===== 屏幕电源控制（息屏/亮屏）===== */
// T-Watch V3：屏幕背光/供电由 AXP202 LDO2 提供（GPIO12 只调亮度，拉低无法熄屏）
// 息屏 = LDO2 断电（屏幕黑），亮屏 = LDO2 恢复供电 + 重绘
// 当前是否处于息屏状态（防重复执行）
static bool _screenOff = false;

/* ===== 自动息屏开关（NVS 持久化，默认开）===== */
static bool _autoSleep = true;

// 作用：息屏（关闭背光供电 LDO2）
void screenOff() {
    if (_screenOff) return;
    // v29j: 不再切断 LDO2 —— FT6336 触摸由 LDO2 供电，断电后双击唤醒失效（实测息屏后触摸无响应）。
    // 改用 LCD SLEEP IN + 背光拉低：视觉全黑，触摸保持可用，功耗主要省在 LCD 睡眠 + 背光关。
    digitalWrite(TFT_BL, LOW);   // 背光拉低
    tft.writecommand(0x10);      // LCD SLEEP IN
    delay(5);
    _screenOff = true;
    Serial.println("[PWR] Screen OFF (SLEEP IN, LDO2 kept for touch)");
}

// 作用：亮屏
void screenOn() {
    if (!_screenOff) return;
    // v29j: LDO2 从未切断；恢复 LCD + 背光
    tft.writecommand(0x11);      // LCD SLEEP OUT
    delay(120);
    digitalWrite(TFT_BL, HIGH);  // 背光拉高
    _screenOff = false;
    Serial.println("[PWR] Screen ON");
}

bool isScreenOff() { return _screenOff; }

// 作用：初始化自动息屏参数
void autoSleepInit() {
    Preferences prefs;
    if (prefs.begin("twatch", true)) {
        _autoSleep = prefs.getBool("autosleep", true);
        prefs.end();
    }
    Serial.printf("[PWR] AutoSleep %s\n", _autoSleep ? "ON" : "OFF");
}

bool autoSleepEnabled() { return _autoSleep; }

// 作用：开/关自动息屏
void autoSleepSet(bool on) {
    _autoSleep = on;
    Preferences prefs;
    if (prefs.begin("twatch", false)) {
        prefs.putBool("autosleep", _autoSleep);
        prefs.end();
    }
    Serial.printf("[PWR] AutoSleep -> %s\n", _autoSleep ? "ON" : "OFF");
}

// 作用：读 AXP202 电源芯片寄存器
uint8_t readAXP(uint8_t reg) {
    Wire1.beginTransmission(AXP_ADDR);
    Wire1.write(reg);
    Wire1.endTransmission(false);
    // 请求读取 1 字节寄存器
    Wire1.requestFrom((int)AXP_ADDR, (int)1);
    return Wire1.read();
}

// 作用：写 AXP202 寄存器
void writeAXP(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(AXP_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

// 作用：LDO2 供电开关（控制屏幕背光）
void setLDO2(bool on) {
    // AXP202：0x12 是 LDO2/3/4+DC2/3 输出控制；LDO2 使能位 = bit2（1<<AXP202_LDO2）
    // 0x28 是 LDO2/4 电压选择，不能用来开关（官方库 setPowerOutPut 证实）
    uint8_t v = readAXP(0x12);
    writeAXP(0x12, on ? (v | 0x04) : (v & ~0x04));
}

// 设置 LDO2 电压 1800~3300 mV，步长100mV，同时开启输出
// 作用：调节背光电压（亮度）
void setLDO2Voltage(int mV) {
    if (mV < 1800) mV = 1800;
    if (mV > 3300) mV = 3300;
    uint8_t code = (mV - 1800) / 100;  // 电压码 0~15
    uint8_t v = readAXP(0x28) & 0xF8;  // 保留 bit[7:3]，清除 bit[2:0]
    v |= code;                          // 设置电压
    v |= 0x08;                          // 开启 LDO2
    writeAXP(0x28, v);
}

// 作用：马达震动 ms 毫秒
void buzz(int ms) {
    digitalWrite(MOTOR, HIGH);
    delay(ms);
    digitalWrite(MOTOR, LOW);
}

// 作用：读取电池电量百分比
int getBatteryPercent() {
    // 电池电压高字节（0x78 寄存器）
    uint8_t hl = readAXP(0x78);
    uint8_t ll = readAXP(0x79);
    // 拼接 12 位电压值（每单位 1.1mV）
    int voltage = ((hl << 4) | (ll & 0x0F)) * 1.1;
    // 满电阈值 4200mV → 100%
    if (voltage >= 4200) return 100;
    if (voltage <= 3300) return 0;
    return (voltage - 3300) * 100 / (4200 - 3300);
}

// 作用：是否在充电
bool isCharging() {
    return (readAXP(0x00) & 0x10) != 0;
}