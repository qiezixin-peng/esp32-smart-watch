/*
 * UI 主界面模块公共定义（页面编号、磁贴布局）
 */

#ifndef UI_MODULE_H
#define UI_MODULE_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../touch/touch.h"

extern TFT_eSPI tft;

// 系统模式
#define MODE_WATCHFACE  0
#define MODE_APPS       1
extern int sysMode;

// 页面索引
#define PAGE_HOME    0
#define PAGE_HEALTH  1
#define PAGE_ENV     2
#define PAGE_SYSTEM  3
#define PAGE_ABOUT   4
#define PAGE_DEVICE  5
#define PAGE_WFSEL   6
#define PAGE_WEATHER 7
#define PAGE_CLOCK   8
#define PAGE_SPORT   9
#define PAGE_NOTIFY  10
#define PAGE_COUNT   11

extern int currentPage;

extern float sensorTemp, sensorHum, sensorPress;
extern float sensorBpm, sensorSpo2, sensorUv;
extern int   sensorRxCount;
extern bool wifiOk, mqttOk, bleConnected;
extern int  seconds;

void drawStatusBar();
void refreshStatusBar();
void refreshPageHealth();
void drawPageHome();
void drawPageHealth();
void drawPageEnv();
void drawPageSystem();
void drawPageAbout();
void drawPageDevice();
void drawPageWFSel();
void drawPageWeather();
void drawPageClock();
void drawPageSport();
void drawPageNotify();
void refreshPageSport();
void drawPullDown();
void refreshPullDownTime();
void drawMainScreen();
bool handleTouch(uint16_t x, uint16_t y);
int  detectSwipe();

// ===== 表盘/背景图板块（v29o）=====
int  wfGetTab();              // 0=表盘 tab, 1=背景图 tab
void wfSetTab(int tab);
int  wfBgPreviewSlot();       // 背景图 tab 当前预览槽(0~2)
void wfBgCycle(int dir);      // 背景图 tab 切换预览槽: dir=1 下一张, -1 上一张
void wfBgToggle();            // 背景图 tab 双击: 启用/关闭当前预览槽 + 3s 卡片
bool wfTabCardExpired();      // 卡片已过期(用于主循环定时重绘清卡)
bool wfTabCardShowing();      // 卡片是否在显示中

#endif
