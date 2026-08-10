#ifndef CLOCK_TIMER_H
#define CLOCK_TIMER_H

#include <Arduino.h>
#include <TFT_eSPI.h>

/* ==========================================================
 * 计时器模块（倒计时）
 * - 预设 1/3/5/10 分钟按钮
 * - +/- 调整时间（运行中也可调整）
 * - 开始/停止/重置
 * - 倒计时到 0 → 触发响铃（由 clock 层处理）
 * ========================================================== */

// 初始化
void timer_init();

// 设置预设分钟数（1/3/5/10）
void timer_setPreset(int minutes);

// 调整剩余时间：delta 秒（正=增加 负=减少），最小 0
void timer_adjust(int deltaSec);

// 开始/停止切换
bool timer_toggle();

// 重置（归零并停止）
void timer_reset();

// 是否运行中
bool timer_running();

// 剩余毫秒
unsigned long timer_remainingMs();

// 每秒节拍（由 clock_handleLoop 调用），返回 true 表示归零触发响铃
bool timer_tick();

// 绘制计时器页
void timer_drawPage(TFT_eSPI* tft);

// 仅刷新时间显示区（比全页重绘快，用于预设/加减调整）
void timer_drawTimeOnly(TFT_eSPI* tft);

// 处理点击。返回：0=未消费 1=仅时间变化 2=按钮状态变化
int timer_handleTap(uint16_t x, uint16_t y);

#endif

