/*
 * 表盘⑤：环形轨道数字表盘（圆环刻度 + 时间居中）
 */

#include "watchface.h"
#include "watchface_manager.h"
#include "../fonts/font_vlw_big.h"
#include "../fonts/font_vlw_time.h"
#include "../wallpaper/wallpaper.h"
#include "watchface_bg.h"
#include <TFT_eSPI.h>
#include <time.h>
#include <math.h>

/* 环形轨道 (v29s)
 * 放大圆环: 半径 100 双细环(中心 120,120 屏幕正中) + 60 刻度短线(整点加长加亮) —— 静态装饰
 * 圈内上方: 日期 "8月7日周五" (vlw 32pt, 基线70)
 * 圈内正中央: 时间 HH:MM (vlw 48pt, poY139 -> 字形 153..190, 中心171.5 避开日期)
 * 背景图模式: 环/刻度/文字均用自适应前景色, 背景透出
 * 黑底模式:   环深灰 + 整点刻度白, 时间白
 * 动态只更新文字区(快照恢复), 环为静态, 无残影
 */

#define RING_CX 120
#define RING_CY 120
#define RING_R  100

// 作用：画圆环刻度（整点刻度加长）
static void drawRing(TFT_eSPI* tft) {
    // 是否有背景图（决定环的颜色方案）
    bool bg = wallpaperHas();
    // 环色：有背景用自适应前景色，无背景深灰
    uint16_t col = bg ? wf_bg_fgColor() : TFT_DARKGREY;
    // 整点刻度色：有背景同前景色，无背景白色
    uint16_t maj = bg ? wf_bg_fgColor() : TFT_WHITE;
    // 外环（半径 100）
    tft->drawCircle(RING_CX, RING_CY, RING_R, col);
    // 内环（半径 98）：与外环组成双细环
    tft->drawCircle(RING_CX, RING_CY, RING_R - 2, col);
    // 画 60 个刻度短线（每格 6°）
    for (int i = 0; i < 60; i++) {
        // 角度转弧度（0° 朝上，顺时针）
        float a = i * 6.0f * PI / 180.0f;
        // 每 5 格是整点刻度（加长加亮）
        bool isMaj = (i % 5 == 0);
        int r0 = RING_R + 2;
        int r1 = isMaj ? RING_R + 13 : RING_R + 9;
        int x0 = RING_CX + (int)(r0 * sinf(a));
        int y0 = RING_CY - (int)(r0 * cosf(a));
        int x1 = RING_CX + (int)(r1 * sinf(a));
        int y1 = RING_CY - (int)(r1 * cosf(a));
        // 画刻度短线：整点用亮色，普通用环色
        tft->drawLine(x0, y0, x1, y1, isMaj ? maj : col);
    }
}

// 格式化日期: "8月7日周五"
// 作用：格式化日期
static void wf_ring_fmtDate(struct tm* ti, char* out, size_t n) {
    static const char* wd[] = {"周日","周一","周二","周三","周四","周五","周六"};
    snprintf(out, n, "%d月%d日%s", ti->tm_mon + 1, ti->tm_mday, wd[ti->tm_wday]);
}

// 作用：渲染环形轨道表盘（时间居中在圆环内）
void wf_ring_render(TFT_eSPI* tft, int h, int m, int s) {
    char buf[16];
    static char lastHM[8] = "";
    static char lastDate[16] = "";
    (void)s;
    if (wf_firstFrame) {
        wf_bg_begin();
        wf_bg_registerRect(2, 45, 151, 150, 42); // 时间区(v29v: 48pt 字形 153..190, 矩形 151..193 覆盖取色)
        wf_bg_registerRect(0, 16, 77, 208, 34);  // 日期区(v29v: 32pt 字形 79..108, 矩形 77..111)
        wf_bg_drawBackground(tft);
        drawRing(tft);
        lastHM[0] = 0; lastDate[0] = 0;
        wf_firstFrame = false;
    }

    // 时间格式化为 HH:MM
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    // 时间变了才更新（避免无谓重绘）
    if (strcmp(buf, lastHM) != 0) {
        strcpy(lastHM, buf);
        // v29u: 恢复背景快照 -> 重画静态环(幂等) -> 叠字; 不能用 drawText(其内部恢复纯背景会擦环)
        wf_bg_restoreRect(tft, 2);
        drawRing(tft);
        tft->loadFont(font_vlw_time);
        int tw = tft->textWidth(buf);
        wf_bg_drawTextOver(tft, 2, buf, (240 - tw) / 2, 139);
        tft->unloadFont();
    }

    time_t now_t = time(nullptr);
    struct tm* ti = localtime(&now_t);
    if (ti && now_t > 1000000000) {
        wf_ring_fmtDate(ti, buf, sizeof(buf));
        if (strcmp(buf, lastDate) != 0) {
            strcpy(lastDate, buf);
            wf_bg_restoreRect(tft, 0);
            drawRing(tft);
            tft->loadFont(font_vlw_big);
            int tw = tft->textWidth(buf);
            wf_bg_drawTextOver(tft, 0, buf, (240 - tw) / 2, 70);
            tft->unloadFont();
        }
    }
}

// 注册表盘⑤：名称"环形轨道"
WatchFace wf_ring = {
    .name = "环形轨道",
    .init = NULL,
    .render = wf_ring_render
};




