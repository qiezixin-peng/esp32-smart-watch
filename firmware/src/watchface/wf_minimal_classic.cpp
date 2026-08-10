/*
 * 表盘④：简约居中数字表盘（日期在上、时间居中，适合搭配背景图）
 */

#include "watchface.h"
#include "watchface_manager.h"
#include "../fonts/font_vlw_big.h"
#include "../fonts/font_vlw_time.h"
#include "../fonts/font_vlw_time_big.h"
#include "../wallpaper/wallpaper.h"
#include "watchface_bg.h"
#include <TFT_eSPI.h>
#include <time.h>
// 格式化日期: "8月7日周五" (无年份, 无前导0, 月日+周几)
// 作用：格式化日期
static void wf_min_fmtDate(struct tm* ti, char* out, size_t n) {
    static const char* wd[] = {"周日","周一","周二","周三","周四","周五","周六"};
    snprintf(out, n, "%d月%d日%s", ti->tm_mon + 1, ti->tm_mday, wd[ti->tm_wday]);
}

/* 款式1 经典居中(v29v): 日期+星期一行居上, 时间 72pt 字形顶对位屏幕 1/2
 * 布局: 日期 "8月7日周五" (font_vlw_big 32pt) 字形顶 y=61 (poY52, 顶=poY+9, 汉字 61..90)
 *       时间 HH:MM (font_vlw_time_big 72pt) 字形顶 y=120(=屏幕240的一半), 字形 120..176,
 *       poY=98 (TFT公式: 字形顶=poY+asc-gdY=98+77-55=120), '0'底=176, 中心148
 * 矩形: 日期 x16..224 y59..93 / 时间 x24..216 y118..178
 * 注意: TFT_eSPI vlw 的 y 不是基线! 字形顶 = y + maxAscent(头字段5) - gdY
 */
// 作用：渲染简约居中表盘（日期在上、时间居中）
void wf_minimal_classic_render(TFT_eSPI* tft, int h, int m, int s) {
    // 临时缓冲区：格式化日期/时间文本
    char buf[24];
    // 上次绘制的日期文本（没变就不重绘）
    static char lastDate[16] = "";
    // 上次绘制的时间文本（没变就不重绘）
    static char lastHM[8] = "";
    if (wf_firstFrame) {
        wf_bg_begin();
        wf_bg_registerRect(0, 16, 59, 208, 34);   // 日期区(字形 61..90, 矩形 59..93)
        wf_bg_registerRect(2, 24, 118, 192, 60); // 时间区(字形 120..176, 矩形 118..178)
        wf_bg_drawBackground(tft);
        lastDate[0] = 0; lastHM[0] = 0;
        wf_firstFrame = false;
    }
    // 取当前 Unix 时间戳
    time_t now_t = time(nullptr);
    // 转换为本地时间结构（含年月日/时分秒/星期）
    struct tm* ti = localtime(&now_t);
    // 时间有效（晚于 2001 年）才刷新日期
    if (ti && now_t > 1000000000) {
        wf_min_fmtDate(ti, buf, sizeof(buf));
        if (strcmp(buf, lastDate) != 0) {
            strcpy(lastDate, buf);
            tft->loadFont(font_vlw_big);
            int tw = tft->textWidth(buf);
            wf_bg_drawText(tft, 0, buf, (240 - tw) / 2, 52);
            tft->unloadFont();
        }
    }
    // 时间格式化为 HH:MM（24 小时制，无秒）
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    if (strcmp(buf, lastHM) != 0) {
        strcpy(lastHM, buf);
        tft->loadFont(font_vlw_time_big);
        int tw = tft->textWidth(buf);
        wf_bg_drawText(tft, 2, buf, (240 - tw) / 2, 98);
        tft->unloadFont();
    }
}

// 注册表盘④：名称"简约居中"
WatchFace wf_minimal_classic = {
    .name = "简约居中",
    .init = NULL,
    .render = wf_minimal_classic_render
};

