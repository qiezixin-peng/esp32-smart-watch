/*
 * 时钟板块公共定义：闹钟/秒表/计时器三个子页
 */

#ifndef CLOCK_MODULE_H
#define CLOCK_MODULE_H

#include <Arduino.h>
#include <TFT_eSPI.h>

/* ==========================================================
 * 时钟板块 — 闹钟 / 秒表 / 计时器
 * 三个子页，顶部标签栏点击或非边缘左右滑动切换
 * ==========================================================
 * 交互约定：
 *   - 顶部标签栏：闹钟 | 秒表 | 计时（点击切换子页）
 *   - 非边缘左右滑动：切换子页
 *   - 左边缘右滑：返回主页面（由 ui/main 层处理）
 *   - 闹钟/计时器到点：全屏响铃 + 震动，任何界面连续点两下关闭
 * ========================================================== */

// 子页枚举
#define CLOCK_PAGE_ALARM     0   // 闹钟
#define CLOCK_PAGE_STOPWATCH 1   // 秒表
#define CLOCK_PAGE_TIMER     2   // 计时器
#define CLOCK_PAGE_COUNT     3

// 初始化（加载 NVS 闹钟数据）
void clock_init();

// 获取/设置当前子页
int  clock_getPage();
void clock_setPage(int page);

// 绘制当前子页（由 ui drawMainScreen 调用）
void clock_drawPage(TFT_eSPI* tft);

// 处理点击（由 ui handleTouch 调用）。返回 true 表示已消费。
bool clock_handleTap(uint16_t x, uint16_t y);

// 主循环调度（由 main loop 调用）
// - 内部处理非边缘滑动切换子页 / 闹钟列表纵向滚动
// - 秒表/计时器实时刷新
void clock_handleLoop(TFT_eSPI* tft);

// 闹钟触发检查：任何界面调用，返回 true 表示进入响铃状态
bool clock_checkAlarm();

// 是否正在响铃
bool clock_isRinging();

// 绘制响铃界面（由 main loop 在响铃时调用）
void clock_drawRinging(TFT_eSPI* tft);

// 响铃时的触摸处理（双击关闭），返回 true 表示响铃已结束
bool clock_handleRingingTap(uint16_t x, uint16_t y);

#endif
