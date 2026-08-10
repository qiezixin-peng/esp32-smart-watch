#include "timer.h"
#include "../fonts/font_vlw.h"

/* ==========================================================
 * 计时器模块实现（倒计时）
 * - 预设 1/3/5/10 分钟按钮
 * - +/- 调整剩余时间（运行中也可调整）
 * - 开始/停止/重置
 * - 归零 → 返回 true 触发响铃（由 clock 层处理）
 * ========================================================== */

static bool running = false;
static unsigned long totalMs = 0;      // 设定总时长（未运行）
static unsigned long remainMs = 0;     // 剩余毫秒（显示/倒计时）
static unsigned long lastTick = 0;     // 上次节拍时间

#define TM_TIME_Y   60
#define TM_PRESET_Y 118
#define TM_ADJ_Y    152
#define TM_BTN1_X   20
#define TM_BTN2_X   130
#define TM_BTN_Y    200
#define TM_BTN_W    90
#define TM_BTN_H    40

// 作用：初始化计时器
void timer_init() {
    running = false;
    totalMs = 0; remainMs = 0; lastTick = 0;
}

// 作用：设置预设分钟数
void timer_setPreset(int minutes) {
    totalMs = (unsigned long)minutes * 60 * 1000;
    remainMs = totalMs;
    running = false;
    lastTick = 0;
}

// 作用：调整剩余秒数
void timer_adjust(int deltaSec) {
    if (deltaSec > 0) {
        remainMs += (unsigned long)deltaSec * 1000;
        if (totalMs == 0) totalMs = remainMs;
        // 上限 99 分钟
        if (remainMs > 99UL * 60 * 1000) remainMs = 99UL * 60 * 1000;
    } else {
        if (remainMs > (unsigned long)(-deltaSec) * 1000)
            remainMs -= (unsigned long)(-deltaSec) * 1000;
        else
            remainMs = 0;
    }
    lastTick = 0;
}

// 作用：开始/暂停倒计时
bool timer_toggle() {
    if (running) {
        running = false;
    } else {
        if (remainMs == 0) remainMs = totalMs;   // 已归零则重来
        if (remainMs > 0) {
            running = true;
            lastTick = millis();
        }
    }
    return running;
}

// 作用：重置倒计时
void timer_reset() {
    running = false;
    remainMs = totalMs;
    lastTick = 0;
}

bool timer_running() { return running; }
unsigned long timer_remainingMs() { return remainMs; }

// 节拍：每秒扣减，归零返回 true
// 作用：每秒递减，到 0 返回 true
bool timer_tick() {
    if (!running) return false;
    unsigned long now = millis();
    // 距上次节拍不足 100ms 则跳过（防抖）
    if (now - lastTick < 100) return false;
    lastTick = now;
    // 还有余量：扣 100ms 继续等待
    if (remainMs > 100) { remainMs -= 100; return false; }
    remainMs = 0;
    running = false;
    return true;   // 归零 → 响铃
}

// 作用：绘制倒计时数字
static void drawTime(TFT_eSPI* tft) {
    unsigned long ms = remainMs;
    unsigned long h = ms / 3600000; if (h > 99) h = 99;   // keep 8-char fixed width
    unsigned long m = (ms % 3600000) / 60000;
    unsigned long s = (ms % 60000) / 1000;

    char buf[12];
    // 格式化为 时:分:秒（固定 8 字符宽）
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);

    // 临时卸载 vlw，使用内置大号 7 段字体（font 7，48px）居中
    tft->unloadFont();
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextSize(1);
    // same as stopwatch: fixed width + bg single write, no black flash
    int cw = 6 * 32 + 2 * 12;
    int tx = (240 - cw) / 2;
    tft->drawString(buf, tx, TM_TIME_Y + 4, 7);
    tft->loadFont(font_vlw);   // 恢复中文渲染
}

// 作用：只刷新时间数字（不整屏重绘）
void timer_drawTimeOnly(TFT_eSPI* tft) {
    drawTime(tft);
}

// 作用：绘制计时器页
void timer_drawPage(TFT_eSPI* tft) {
    drawTime(tft);

    // 预设按钮
    const int presets[4] = {1, 3, 5, 10};
    for (int i = 0; i < 4; i++) {
        int x = 12 + i * 57;
        tft->fillRoundRect(x, TM_PRESET_Y, 51, 28, 6, TFT_DARKGREY);
        tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d分", presets[i]);
        tft->drawString(buf, x + 8, TM_PRESET_Y + 5, 2);
    }

    // +/- 调整
    tft->fillRoundRect(40, TM_ADJ_Y, 70, 36, 8, TFT_NAVY);
    tft->setTextColor(TFT_WHITE, TFT_NAVY);
    tft->drawString("+30秒", 48, TM_ADJ_Y + 8, 2);
    tft->fillRoundRect(130, TM_ADJ_Y, 70, 36, 8, TFT_MAROON);
    tft->setTextColor(TFT_WHITE, TFT_MAROON);
    tft->drawString("-30秒", 138, TM_ADJ_Y + 8, 2);

    // 开始/停止、重置
    tft->fillRoundRect(TM_BTN1_X, TM_BTN_Y, TM_BTN_W, TM_BTN_H, 8,
                       running ? TFT_ORANGE : TFT_GREEN);
    tft->setTextColor(TFT_BLACK, running ? TFT_ORANGE : TFT_GREEN);
    tft->drawString(running ? "停止" : "开始", TM_BTN1_X + 18, TM_BTN_Y + 10, 2);
    tft->fillRoundRect(TM_BTN2_X, TM_BTN_Y, TM_BTN_W, TM_BTN_H, 8, TFT_DARKGREY);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->drawString("重置", TM_BTN2_X + 18, TM_BTN_Y + 10, 2);
}

// 作用：计时器页点击处理
int timer_handleTap(uint16_t x, uint16_t y) {
    // 预设
    if (y >= TM_PRESET_Y && y < TM_PRESET_Y + 28) {
        const int presets[4] = {1, 3, 5, 10};
        for (int i = 0; i < 4; i++) {
            int px = 12 + i * 57;
            if (x >= px && x < px + 51) { timer_setPreset(presets[i]); return 1; }
        }
    }
    // +/-
    if (y >= TM_ADJ_Y && y < TM_ADJ_Y + 36) {
        if (x >= 40 && x < 110) { timer_adjust(30); return 1; }
        if (x >= 130 && x < 200) { timer_adjust(-30); return 1; }
    }
    // 开始/停止
    if (y >= TM_BTN_Y && y < TM_BTN_Y + TM_BTN_H) {
        if (x >= TM_BTN1_X && x < TM_BTN1_X + TM_BTN_W) { timer_toggle(); return 2; }
        if (x >= TM_BTN2_X && x < TM_BTN2_X + TM_BTN_W) { timer_reset(); return 2; }
    }
    return 0;
}

