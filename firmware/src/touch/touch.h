#ifndef TOUCH_MODULE_H
#define TOUCH_MODULE_H

#include <Arduino.h>
#include <Wire.h>

/* ==========================================================
 * 触摸模块 — FT6336 电容触摸控制器驱动
 * 支持手势识别：滑动/点击
 * ==========================================================
 * T-Watch V3 使用 FT6336（兼容 FT5206）I2C 电容触摸控制器
 * I2C 地址: 0x38, SDA=GPIO23, SCL=GPIO32, INT=GPIO38
 *
 * 手势定义：
 *   GESTURE_TAP     — 点击
 *   GESTURE_SWIPE_L — 右→左滑动（进入主界面）
 *   GESTURE_SWIPE_R — 左→右滑动（返回表盘）
 * ========================================================== */

/* ===== 引脚定义 ===== */
#define TOUCH_SDA   23
#define TOUCH_SCL   32
#define TOUCH_INT   38
#define TOUCH_ADDR  0x38

/* ===== 触摸事件类型 ===== */
#define TOUCH_EVENT_PRESS   0
#define TOUCH_EVENT_RELEASE 1
#define TOUCH_EVENT_CONTACT 2
#define TOUCH_EVENT_NONE    3

/* ===== 手势类型 ===== */
#define GESTURE_NONE    0   // 无操作
#define GESTURE_TAP     1   // 点击
#define GESTURE_SWIPE_L 2   // 右→左滑动
#define GESTURE_SWIPE_R 3   // 左→右滑动
#define GESTURE_DTAP    4   // 双击（两次点击间隔 < 400ms）
#define GESTURE_SWIPE_D 5   // 顶部下滑（呼出下拉状态面板）

/* ===== 触摸点结构 ===== */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  weight;
    uint8_t  event;
    bool     touched;
} TouchPoint_t;

/* ===== 触摸可用标志 ===== */
extern bool touchAvailable;

/* ===== 最后触摸时间（毫秒，供自动息屏使用，gesture.cpp 更新） ===== */
extern unsigned long gLastTouchMs;

/* ===== 函数声明 ===== */
bool initTouch();
bool readTouch(TouchPoint_t *tp);
bool getTouch(uint16_t *x, uint16_t *y);

/* ===== 手势检测 ===== */
// 获取当前手势。返回手势类型。
// 如果 GESTURE_TAP，tx/ty 包含点击坐标。
int getGesture(uint16_t *tx, uint16_t *ty);
// 检测任意位置滑动（不分边缘），返回 1=右滑, -1=左滑, 0=无
int detectSwipe();

#endif
