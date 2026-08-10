#include "touch.h"

// 全局触摸可用标志
bool touchAvailable = false;

/* ==========================================================
 * 触摸模块 — 实现（匹配官方 FocalTech_Library 实现）
 * FT6336 寄存器操作通过 I2C（Wire, SDA=23, SCL=32）
 * 官方库：github.com/lewisxhe/FocalTech_Library
 * ==========================================================
 * 关键区别：
 *   1. 不执行 Wire.end() 毁灭性恢复
 *   2. readBytes 只返回 I2C 是否成功
 *   3. getPoint() 读取 0x02 寄存器 5 字节 (状态 + 坐标)
 * ========================================================== */

// 读取 n 字节（完全匹配官方库实现）
// 作用：I2C 读 FT6336 触摸芯片寄存器
static bool readBytesFT(uint8_t reg, uint8_t *data, uint8_t nbytes) {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(reg);
    Wire.endTransmission();                  // 发送 STOP
    // 请求读取 n 字节寄存器
    Wire.requestFrom((int)TOUCH_ADDR, nbytes);
    uint8_t index = 0;
    while (Wire.available() && index < nbytes) {
        data[index++] = Wire.read();
    }
    return (nbytes == index);                // 返回实际读取长度是否匹配
}

// 写入 n 字节
// 作用：I2C 写 FT6336 寄存器
static bool writeBytesFT(uint8_t reg, uint8_t *data, uint8_t nbytes) {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(reg);
    for (uint8_t i = 0; i < nbytes; i++) {
        Wire.write(data[i]);
    }
    return (Wire.endTransmission() == 0);
}

// 读单字节
// 作用：读一个 8 位寄存器
static uint8_t readReg8(uint8_t reg) {
    uint8_t val = 0;
    readBytesFT(reg, &val, 1);
    return val;
}

// 写单字节
// 作用：写一个 8 位寄存器
static void writeReg8(uint8_t reg, uint8_t val) {
    writeBytesFT(reg, &val, 1);
}

// ==================== 初始化 ====================
// 1. 初始化 I2C 总线 (Wire, SDA=23, SCL=32)
// 2. 检测 FT6336 是否在线
// 作用：初始化触摸屏（检测 FT6336 并复位）
bool initTouch() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    delay(5);

    pinMode(TOUCH_INT, INPUT);

    // 探测设备（匹配官方 probe() 实现）
    Wire.beginTransmission(TOUCH_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[TOUCH] FT6336 not found on I2C bus!");
        touchAvailable = false;
        return false;
    }

    // 读取设备模式确认通信
    uint8_t mode = readReg8(0x00);
    Serial.printf("[TOUCH] FT6336 found, mode=0x%02X\n", mode);

    // 设置触摸阈值
    writeReg8(0x80, 22);

    Serial.println("[TOUCH] FT6336 initialized OK");
    touchAvailable = true;
    return true;
}

// ==================== 获取触摸点 ====================
// 读取 0x02 寄存器 5 字节：状态(1) + XH(1) + XL(1) + YH(1) + YL(1)
// 完全匹配官方库 FocalTech_Class::getPoint()
// 作用：读取当前触摸点坐标
bool readTouch(TouchPoint_t *tp) {
    if (!touchAvailable) return false;

    uint8_t buf[5];
    memset(tp, 0, sizeof(TouchPoint_t));

    if (!readBytesFT(0x02, buf, 5)) {
        return false;  // I2C 通信失败
    }

    // buf[0] = 触摸点数 (0, 1, 2)
    if (buf[0] == 0 || buf[0] > 2) {
        return false;  // 无有效触摸
    }

    // 提取事件类型 (bit[7:6] = 触摸事件)
    uint8_t event = (buf[1] >> 6) & 0x03;
    if (event == TOUCH_EVENT_RELEASE || event == TOUCH_EVENT_NONE) {
        return false;  // 抬起事件不处理
    }

    // 计算 12 位坐标
    tp->x = ((uint16_t)(buf[1] & 0x0F) << 8) | buf[2];  // X[11:8] + X[7:0]
    // FT6336 坐标系与 TFT 屏幕成 180° 翻转，需要转换
    tp->x = 240 - tp->x;
    tp->y = ((uint16_t)(buf[3] & 0x0F) << 8) | buf[4];  // Y[11:8] + Y[7:0]
    tp->y = 240 - tp->y;

    if (tp->x > 240) tp->x = 240;
    if (tp->y > 240) tp->y = 240;

    tp->event = event;
    tp->touched = true;

    return true;
}

// ==================== 简易接口 ====================
// 作用：返回是否有触摸，并输出坐标
bool getTouch(uint16_t *x, uint16_t *y) {
    TouchPoint_t tp;
    if (readTouch(&tp)) {
        *x = tp.x;
        *y = tp.y;
        return true;
    }
    return false;
}
