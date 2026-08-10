/*
 * 表盘③：点阵像素数字表盘（5x7 复古点阵风格）
 */

#include "watchface.h"
#include "watchface_manager.h"
#include "../fonts/font_vlw_big.h"
#include "../wallpaper/wallpaper.h"
#include "watchface_bg.h"
#include <TFT_eSPI.h>
#include <time.h>

/* 点阵像素 (v29q)
 * 时间 HH:MM 用 5x7 点阵字库自绘（复古 LED 屏风格, 无字体文件）
 * 日期 "8月7日周五" 用 vlw 平滑字体
 * 背景图模式: 亮点用自适应前景色, 背景透出
 * 黑底模式:   亮点白色
 * 局部刷新: 时间区用 wf_bg_restoreRect 恢复背景后重画全部点阵
 */

// 5x7 点阵字型 (bit5..bit0, 每行 5 bit)
static const uint8_t FONT57[11][7] = {
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}, // 0
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // 1
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}, // 2
    {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}, // 3
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}, // 4
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}, // 5
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}, // 6
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}, // 7
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}, // 8
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}, // 9
    {0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000}  // :
};

#define DOT_P  8       // 每点像素(放大倍数)
#define DOT_W  5*DOT_P // 字宽 40
#define DOT_H  7*DOT_P // 字高 56
#define DOT_GAP 6      // 字符间距

// 布局: 4 字 + 冒号 总宽 = 40*4 + 8 + 6*4 = 192 -> x0=24
#define DOT_X0 24
#define DOT_Y  130     // 时间区顶部 (日期在上, 时间在下)

// 画一个 5x7 字符 (idx: 0-9=数字, 10=冒号)
// 作用：画一个 5x7 点阵字符
static void drawDotChar(TFT_eSPI* tft, int x, int y, int idx, uint16_t col) {
    // 逐行扫描：7 行点阵组成一个字符
    for (int row = 0; row < 7; row++) {
        // 取该字符第 row 行的 5 位点阵数据
        uint8_t bits = FONT57[idx][row];
        // 逐列检查 5 个点
        for (int colb = 0; colb < 5; colb++) {
            // 该位为 1 才画点（为 0 则透出背景）
            if (bits & (0x10 >> colb)) {
                // 画一个放大后的点（DOT_P×DOT_P 像素）
                tft->fillRect(x + colb * DOT_P, y + row * DOT_P, DOT_P, DOT_P, col);
            }
        }
    }
}

// 格式化日期: "8月7日周五"
// 作用：格式化日期
static void wf_dot_fmtDate(struct tm* ti, char* out, size_t n) {
    static const char* wd[] = {"周日","周一","周二","周三","周四","周五","周六"};
    snprintf(out, n, "%d月%d日%s", ti->tm_mon + 1, ti->tm_mday, wd[ti->tm_wday]);
}

// 作用：渲染点阵像素表盘（复古 LED 风）
void wf_dot_matrix_render(TFT_eSPI* tft, int h, int m, int s) {
    char buf[16];
    static char lastHM[8] = "";
    static char lastDate[16] = "";
    (void)s;
    if (wf_firstFrame) {
        wf_bg_begin();
        wf_bg_registerRect(2, 20, 122, 200, 72); // 时间区(盖住点阵 20..220 x 130..186)
        wf_bg_registerRect(0, 16, 27, 208, 30);  // 日期区(时间上方)
        wf_bg_drawBackground(tft);
        lastHM[0] = 0; lastDate[0] = 0;
        wf_firstFrame = false;
    }

    // 时间格式化为 HH:MM
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    // 时间变了才重绘（没变跳过，省电）
    if (strcmp(buf, lastHM) != 0) {
        strcpy(lastHM, buf);
        // 有背景图用自适应前景色，无背景用白色
        uint16_t col = wallpaperHas() ? wf_bg_fgColor() : TFT_WHITE;
        // 先把时间区恢复成纯背景（清掉旧数字残影）
        wf_bg_restoreRect(tft, 2);
        // 从起始 x 依次画 5 个点阵字符（时十位/时个位/冒号/分十位/分个位）
        int x = DOT_X0;
        drawDotChar(tft, x, DOT_Y, buf[0]-'0', col); x += DOT_W + DOT_GAP;
        drawDotChar(tft, x, DOT_Y, buf[1]-'0', col); x += DOT_W + DOT_GAP;
        drawDotChar(tft, x, DOT_Y, 10, col);        x += DOT_P + DOT_GAP;
        drawDotChar(tft, x, DOT_Y, buf[3]-'0', col); x += DOT_W + DOT_GAP;
        drawDotChar(tft, x, DOT_Y, buf[4]-'0', col);
    }

    time_t now_t = time(nullptr);
    struct tm* ti = localtime(&now_t);
    if (ti && now_t > 1000000000) {
        wf_dot_fmtDate(ti, buf, sizeof(buf));
        if (strcmp(buf, lastDate) != 0) {
            strcpy(lastDate, buf);
            tft->loadFont(font_vlw_big);
            int tw = tft->textWidth(buf);
            wf_bg_drawText(tft, 0, buf, (240 - tw) / 2, 52);
            tft->unloadFont();
        }
    }
}

// 注册表盘③：名称"点阵像素"
WatchFace wf_dot_matrix = {
    .name = "点阵像素",
    .init = NULL,
    .render = wf_dot_matrix_render
};

