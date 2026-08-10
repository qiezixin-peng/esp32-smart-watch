#include "clock.h"
#include "alarm.h"
#include "stopwatch.h"
#include "timer.h"
#include "../touch/touch.h"
#include "../power/power.h"
#include <time.h>

extern TFT_eSPI tft;

/* ==========================================================
 * 时钟板块主控
 * - 标签栏：闹钟 | 秒表 | 计时（点击切换）
 * - 非边缘左右滑动切换子页
 * - 闹钟列表纵向滚动
 * - 响铃状态（闹钟/计时器到点，双击关闭）
 * ========================================================== */

// 当前子页（闹钟/秒表/计时器）
static int cur = CLOCK_PAGE_ALARM;
// 闹钟列表纵向滚动偏移（像素）
static int scrollOffset = 0;

// 响铃状态
static bool ringing = false;
static bool motorOn = false;   // 马达震动非阻塞标志（响铃时每 400ms 震 80ms）
#define RING_ALARM 0
#define RING_TIMER 1
static int  ringType = RING_ALARM;
static unsigned long ringStartMs = 0;
static unsigned long lastBuzzMs = 0;
static unsigned long lastTapMs = 0;
static uint8_t ringHour = 0, ringMinute = 0;

// 纵向滚动状态
static bool _vActive = false;
static int _vLastY = 0;
static int _vStartX = 0;
static bool _vChanged = false;

// 作用：初始化时钟板块
void clock_init() {
    alarm_init();
    sw_init();
    timer_init();
    cur = CLOCK_PAGE_ALARM;
    scrollOffset = 0;
    ringing = false;
}

int  clock_getPage() { return cur; }
// 作用：切换子页（闹钟/秒表/计时）
void clock_setPage(int page) {
    if (page < 0 || page >= CLOCK_PAGE_COUNT) return;
    cur = page;
    scrollOffset = 0;
    clock_drawPage(&tft);
}

// ==================== 标签栏 ====================

// 作用：画子页标签栏
static void drawTabBar() {
    const char* names[CLOCK_PAGE_COUNT] = {"闹钟", "秒表", "计时器"};
    // 每个标签等宽（240 / 子页数）
    int w = 240 / CLOCK_PAGE_COUNT;
    for (int i = 0; i < CLOCK_PAGE_COUNT; i++) {
        int x = i * w;
        if (i == cur) {
            tft.fillRect(x, 20, w, 24, TFT_CYAN);
            tft.setTextColor(TFT_BLACK, TFT_CYAN);
        } else {
            tft.fillRect(x, 20, w, 24, TFT_BLACK);
            tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        }
        int tw = tft.textWidth(names[i], 2);
        tft.drawString(names[i], x + (w - tw) / 2, 24, 2);
    }
    tft.drawLine(0, 44, 239, 44, TFT_DARKGREY);
}

// ==================== 绘制 ====================

// 作用：绘制当前时钟子页
void clock_drawPage(TFT_eSPI* tft) {
    tft->fillScreen(TFT_BLACK);
    if (alarm_isEditing()) {
        alarm_drawEdit(tft, alarm_editingIndex());
        return;
    }
    drawTabBar();
    switch (cur) {
        case CLOCK_PAGE_ALARM:     alarm_drawList(tft, scrollOffset); break;
        case CLOCK_PAGE_STOPWATCH: sw_drawPage(tft); break;
        case CLOCK_PAGE_TIMER:     timer_drawPage(tft); break;
    }
}

// ==================== 点击 ====================

// 作用：时钟板块点击分发
bool clock_handleTap(uint16_t x, uint16_t y) {
    // 闹钟设置页
    if (alarm_isEditing()) {
        alarm_handleEditTap(x, y);
        if (alarm_isEditing()) alarm_drawEdit(&tft, alarm_editingIndex());
        else clock_drawPage(&tft);
        return true;
    }
    // 标签栏切换
    if (y >= 20 && y <= 44) {
        int idx = x / (240 / CLOCK_PAGE_COUNT);
        if (idx >= 0 && idx < CLOCK_PAGE_COUNT && idx != cur) {
            cur = idx; scrollOffset = 0;
            clock_drawPage(&tft);
        }
        return true;
    }
    // 子页交互
    switch (cur) {
        case CLOCK_PAGE_ALARM:
            if (alarm_handleListTap(x, y, scrollOffset)) {
                if (alarm_isEditing()) alarm_drawEdit(&tft, alarm_editingIndex());
                else alarm_drawList(&tft, scrollOffset);
            }
            return true;
        case CLOCK_PAGE_STOPWATCH:
            if (sw_handleTap(x, y)) sw_drawPage(&tft);
            return true;
        case CLOCK_PAGE_TIMER: {
            int r = timer_handleTap(x, y);
            if (r == 1) timer_drawTimeOnly(&tft);
            else if (r == 2) timer_drawPage(&tft);
            return true;
        }
    }
    return false;
}

// ==================== 纵向滚动（闹钟列表） ====================

// 作用：闹钟列表滚动处理
static int handleAlarmScroll() {
    static int lastDrawOffset = 0;   // scrollOffset of the last frame actually drawn
    TouchPoint_t tp;
    // 手指按住：开始/继续记录滑动
    if (readTouch(&tp)) {
        if (!_vActive) {
            _vActive = true; _vStartX = tp.x; _vLastY = tp.y; _vChanged = false;
            lastDrawOffset = scrollOffset;
        } else {
            // 本次移动的纵向增量（下移为正）
            int dy = (int)tp.y - _vLastY;
            if (abs((int)tp.x - _vStartX) < abs(dy)) {   // vertical dominates
                _vLastY = tp.y;
                // 最大可滚动距离 = 内容总高 - 可视区高
                int maxS = alarm_visibleCount() * ALARM_LIST_H - (ALARM_ADD_Y - (ALARM_LIST_Y0 - 2));
                if (maxS < 0) maxS = 0;
                int old = scrollOffset;
                scrollOffset += dy;
                // 限制滚动范围在 0..maxS
                if (scrollOffset < 0) scrollOffset = 0;
                if (scrollOffset > maxS) scrollOffset = maxS;
                if (scrollOffset != old) _vChanged = true;
            }
        }
        if (_vChanged) {
            int d = scrollOffset - lastDrawOffset;
            lastDrawOffset = scrollOffset;
            return d;
        }
        return 0;
    }
    // 手指松开：结束本次滑动跟踪
    _vActive = false; _vChanged = false;
    return 0;
}

// ==================== 主循环调度 ====================

// 作用：时钟板块后台循环（计时器走秒/闹钟到点检测）
void clock_handleLoop(TFT_eSPI* tft) {
    static unsigned long lastRedraw = 0;
    unsigned long now = millis();

    // 非边缘左右滑切换子页（编辑页锁定）
    if (!alarm_isEditing()) {
        int sw = detectSwipe();
        if (sw != 0) {
            if (sw == 1) { cur = (cur + CLOCK_PAGE_COUNT - 1) % CLOCK_PAGE_COUNT; }
            else         { cur = (cur + 1) % CLOCK_PAGE_COUNT; }
            scrollOffset = 0;
            clock_drawPage(tft);
            return;
        }
    }

    // 闹钟列表：仅滚动/内容变化时重绘（去掉周期重绘）
    if (cur == CLOCK_PAGE_ALARM && !alarm_isEditing()) {
        int d = handleAlarmScroll();
        if (d != 0) alarm_drawListIncremental(tft, scrollOffset, d);
    }

    // 秒表实时刷新（只到秒，500ms 足够；停止时不再周期重绘）
    if (cur == CLOCK_PAGE_STOPWATCH) {
        if (sw_running() && now - lastRedraw >= 500) { lastRedraw = now; sw_drawPage(tft); }
    }

    // 计时器
    if (cur == CLOCK_PAGE_TIMER) {
        if (timer_running()) {
            if (timer_tick()) {
                ringing = true; ringType = RING_TIMER; ringStartMs = now; lastBuzzMs = 0; lastTapMs = 0;
                clock_drawRinging(tft);
                return;
            }
            if (now - lastRedraw >= 500) { lastRedraw = now; timer_drawTimeOnly(tft); }
        }
    }
}

// ==================== 响铃 ====================

// 作用：检查是否有闹钟到点
bool clock_checkAlarm() {
    if (ringing) return false;
    int idx = alarm_checkTrigger();
    if (idx >= 0) {
        AlarmItem* a = alarm_get(idx);
        if (a) { ringHour = a->hour; ringMinute = a->minute; }
        ringing = true; ringType = RING_ALARM; ringStartMs = millis(); lastBuzzMs = 0; lastTapMs = 0;
        return true;
    }
    return false;
}

bool clock_isRinging() { return ringing; }

// 作用：绘制闹钟响铃界面
void clock_drawRinging(TFT_eSPI* tft) {
    tft->fillScreen(TFT_BLACK);
    tft->setTextColor(TFT_RED, TFT_BLACK);
    tft->drawString(ringType == RING_ALARM ? "闹钟!" : "计时结束!", 55, 60, 4);
    if (ringType == RING_ALARM) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", ringHour, ringMinute);
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        tft->drawString(buf, 75, 105, 4);
    }
    tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft->drawString("连续点两下关闭", 50, 165, 2);
    // 马达震动：非阻塞式（每 400ms 震 80ms），不再用 delay 卡主循环，避免触摸采样被中断导致双击丢帧
    unsigned long now = millis();
    if (!motorOn && now - lastBuzzMs >= 400) {
        lastBuzzMs = now;
        digitalWrite(MOTOR, HIGH);
        motorOn = true;
    } else if (motorOn && now - lastBuzzMs >= 80) {
        digitalWrite(MOTOR, LOW);
        motorOn = false;
    }
}

// 作用：响铃界面点击处理（双击关闭）
bool clock_handleRingingTap(uint16_t x, uint16_t y) {
    unsigned long now = millis();
    // 两次点击间隔 < 1200ms 视为双击关闭（放宽窗口：快双击被手势层合并成 DTAP 也能关）
    if (now - lastTapMs < 1200) {
        ringing = false;
        lastTapMs = 0;
        digitalWrite(MOTOR, LOW);   // 立即停震
        motorOn = false;
        return true;
    }
    lastTapMs = now;
    return false;
}
