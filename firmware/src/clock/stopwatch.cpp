#include "stopwatch.h"
#include "../fonts/font_vlw.h"

/* ==========================================================
 * 秒表模块实现
 * - 显示 时:分:秒（大号 7 段字体，居中）
 * - 底部两个按钮：开始/停止、重置
 * ========================================================== */

static bool running = false;
static unsigned long startAt = 0;   // 开始时刻
static unsigned long stopAt = 0;    // 停止时刻（累计）
static unsigned long accMs = 0;     // 已累计毫秒

#define SW_TIME_Y   70
#define SW_BTN1_X   20
#define SW_BTN1_Y   180
#define SW_BTN2_X   130
#define SW_BTN2_Y   180
#define SW_BTN_W    90
#define SW_BTN_H    40

// 作用：初始化秒表
void sw_init() {
    running = false;
    startAt = 0; stopAt = 0; accMs = 0;
}

// 作用：开始/暂停秒表
bool sw_toggle() {
    // 正在计时 → 暂停：结算本段时长到累计
    if (running) {
        stopAt = millis();
        accMs += stopAt - startAt;
        running = false;
    } else {
    // 开始/继续：记录本段起点
        startAt = millis();
        running = true;
    }
    return running;
}

// 作用：秒表清零
void sw_reset() {
    running = false;
    startAt = 0; stopAt = 0; accMs = 0;
}

bool sw_running() { return running; }

// 作用：当前已计时毫秒数
unsigned long sw_elapsedMs() {
    // 计时中 = 已累计 + 当前段；暂停 = 仅累计
    if (running) return accMs + (millis() - startAt);
    return accMs;
}

// 作用：绘制时间数字
static void drawTimeDigits(TFT_eSPI* tft, unsigned long ms) {
    unsigned long total = ms;
    unsigned long h = total / 3600000; if (h > 99) h = 99;   // keep 8-char fixed width
    unsigned long m = (total % 3600000) / 60000;
    unsigned long s = (total % 60000) / 1000;

    char buf[12];
    // 格式化为 时:分:秒（固定 8 字符宽）
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);

    // 临时卸载 vlw，使用内置大号 7 段字体（font 7，48px）
    tft->unloadFont();
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextSize(1);
    // font 7: digits all 32px, colon 12px -> total width fixed at 216px
    // drawString with bg color overwrites whole cells in one write -> no black flash
    int cw = 6 * 32 + 2 * 12;
    int tx = (240 - cw) / 2;
    tft->drawString(buf, tx, SW_TIME_Y + 8, 7);
    tft->loadFont(font_vlw);   // 恢复中文渲染
}

// 作用：绘制秒表页
void sw_drawPage(TFT_eSPI* tft) {
    drawTimeDigits(tft, sw_elapsedMs());
    // 按钮
    tft->fillRoundRect(SW_BTN1_X, SW_BTN1_Y, SW_BTN_W, SW_BTN_H, 8,
                       running ? TFT_ORANGE : TFT_GREEN);
    tft->setTextColor(TFT_BLACK, running ? TFT_ORANGE : TFT_GREEN);
    tft->drawString(running ? "停止" : "开始", SW_BTN1_X + 18, SW_BTN1_Y + 10, 2);
    tft->fillRoundRect(SW_BTN2_X, SW_BTN2_Y, SW_BTN_W, SW_BTN_H, 8, TFT_DARKGREY);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->drawString("重置", SW_BTN2_X + 18, SW_BTN2_Y + 10, 2);
}

// 作用：秒表页点击处理
bool sw_handleTap(uint16_t x, uint16_t y) {
    if (x >= SW_BTN1_X && x < SW_BTN1_X + SW_BTN_W &&
        y >= SW_BTN1_Y && y < SW_BTN1_Y + SW_BTN_H) {
        sw_toggle();
        return true;
    }
    if (x >= SW_BTN2_X && x < SW_BTN2_X + SW_BTN_W &&
        y >= SW_BTN2_Y && y < SW_BTN2_Y + SW_BTN_H) {
        sw_reset();
        return true;
    }
    return false;
}