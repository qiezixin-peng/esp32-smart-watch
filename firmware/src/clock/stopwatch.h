#ifndef CLOCK_STOPWATCH_H
#define CLOCK_STOPWATCH_H

#include <Arduino.h>
#include <TFT_eSPI.h>

/* ==========================================================
 * 秒表模块
 * - 开始/停止/重置
 * - 显示 时:分:秒.百分秒
 * - 状态不存 NVS（掉电归零）
 * ========================================================== */

// 初始化
void sw_init();

// 开始/停止切换（返回新状态）
bool sw_toggle();

// 重置
void sw_reset();

// 是否运行中
bool sw_running();

// 当前累计毫秒数
unsigned long sw_elapsedMs();

// 绘制秒表页（含按钮）
void sw_drawPage(TFT_eSPI* tft);

// 处理点击。返回 true 已消费。
bool sw_handleTap(uint16_t x, uint16_t y);

#endif

