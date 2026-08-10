/*
 * 表盘⑥：七段数码管数字表盘
 */

#include "watchface.h"
#include "watchface_manager.h"
#include "../fonts/font_vlw_big.h"
#include "../wallpaper/wallpaper.h"
#include "watchface_bg.h"
#include <TFT_eSPI.h>
#include <time.h>

/* 七段数码管 (v29q)
 * 时间 HH:MM 用自绘 7 段大数字（经典电子表造型, 无字体文件）
 * 日期 "8月7日周五" 用 vlw 平滑字体
 * 背景图模式: 亮段用自适应前景色, 灭段不画(背景透出, 无黑块)
 * 黑底模式:   亮段白色, 灭段深灰(真实数码管质感)
 * 局部刷新: 时间区用 wf_bg_restoreRect 恢复背景后重画全部分段
 */

// 7 段码 (bit0=a上横 bit1=b右上竖 bit2=c右下竖 bit3=d下横 bit4=e左下竖 bit5=f左上竖 bit6=g中横)
static const uint8_t SEGMAP[10] = {
    0b0111111, // 0
    0b0000110, // 1
    0b1011011, // 2
    0b1001111, // 3
    0b1100110, // 4
    0b1101101, // 5
    0b1111101, // 6
    0b0000111, // 7
    0b1111111, // 8
    0b1101111  // 9
};

// 数字几何: 4 数字 + 冒号总宽 200 -> x0=(240-200)/2=20
#define SEG_DW 36     // 数字宽
#define SEG_DH 62     // 数字高
#define SEG_ST 8      // 段厚
#define SEG_T  6      // 段斜角
#define SEG_X0 20     // 时间区起点 x
#define SEG_Y  130    // 数字顶部 y (日期在上, 时间在下)
#define SEG_GAP 10    // 数字间距

// 画一段平行四边形: P1(x,y+t) P2(x+w,y) P3(x+w,y+h-t) P4(x,y+h); draw=false 跳过(背景透出)
// 作用：画一段七段数码管笔画
static void drawSeg(TFT_eSPI* tft, int x, int y, int w, int h, uint16_t col, bool draw) {
    // draw=false：灭段直接跳过（背景透出，不画灰块）
    if (!draw) return;
    // 尺寸非法直接返回（保护绘制）
    if (w <= 0 || h <= 0) return;
    // 段斜角（默认 6 像素，让笔画有立体感）
    int t = SEG_T;
    // 斜角不能超过段高一半（防止变形）
    if (t > h / 2) t = h / 2;
    // 用两个三角形拼出平行四边形笔画（上半）
    tft->fillTriangle(x, y + t, x + w, y, x + w, y + h - t, col);
    tft->fillTriangle(x, y + t, x + w, y + h - t, x, y + h, col);
}

// 画一个数字 (x=数字左上角); drawOff=false 时灭段不画
static void drawSegDigit(TFT_eSPI* tft, int x, int y, int digit, uint16_t onCol, uint16_t offCol, bool drawOff) {
    // 查表取该数字的 7 段点亮码
    uint8_t s = SEGMAP[digit & 0x0F];
    // a 上横
    drawSeg(tft, x, y, SEG_DW, SEG_ST, (s & 0x01) ? onCol : offCol, (s & 0x01) || drawOff);
    // g 中横
    drawSeg(tft, x, y + SEG_DH / 2 - SEG_ST / 2, SEG_DW, SEG_ST, (s & 0x40) ? onCol : offCol, (s & 0x40) || drawOff);
    // d 下横
    drawSeg(tft, x, y + SEG_DH - SEG_ST, SEG_DW, SEG_ST, (s & 0x08) ? onCol : offCol, (s & 0x08) || drawOff);
    int vh = SEG_DH / 2 - SEG_ST;               // 竖段高度
    int vy = y + SEG_ST;                        // 上竖段起点
    int vy2 = y + SEG_DH / 2 + SEG_ST / 2;      // 下竖段起点
    // f 左上竖
    drawSeg(tft, x, vy, SEG_ST, vh, (s & 0x20) ? onCol : offCol, (s & 0x20) || drawOff);
    // b 右上竖
    drawSeg(tft, x + SEG_DW - SEG_ST, vy, SEG_ST, vh, (s & 0x02) ? onCol : offCol, (s & 0x02) || drawOff);
    // e 左下竖
    drawSeg(tft, x, vy2, SEG_ST, vh, (s & 0x10) ? onCol : offCol, (s & 0x10) || drawOff);
    // c 右下竖
    drawSeg(tft, x + SEG_DW - SEG_ST, vy2, SEG_ST, vh, (s & 0x04) ? onCol : offCol, (s & 0x04) || drawOff);
}

// 冒号: 两个小圆点
// 作用：画冒号（两个圆点）
static void drawSegColon(TFT_eSPI* tft, int x, int yMid, uint16_t col) {
    tft->fillCircle(x, yMid - 14, 4, col);
    tft->fillCircle(x, yMid + 14, 4, col);
}

// 格式化日期: "8月7日周五"
// 作用：格式化日期（如：8月7日周五）
static void wf_seg_fmtDate(struct tm* ti, char* out, size_t n) {
    static const char* wd[] = {"周日","周一","周二","周三","周四","周五","周六"};
    snprintf(out, n, "%d月%d日%s", ti->tm_mon + 1, ti->tm_mday, wd[ti->tm_wday]);
}

// 作用：渲染七段数码管表盘（日期+时间）
void wf_seven_seg_render(TFT_eSPI* tft, int h, int m, int s) {
    char buf[16];
    static char lastHM[8] = "";
    static char lastDate[16] = "";
    (void)s;
    if (wf_firstFrame) {
        wf_bg_begin();
        wf_bg_registerRect(2, 6, 102, 228, 98);  // 时间区(盖住数字 20..220 x 130..192)
        wf_bg_registerRect(0, 16, 27, 208, 30);  // 日期区(时间上方)
        wf_bg_drawBackground(tft);
        lastHM[0] = 0; lastDate[0] = 0;
        wf_firstFrame = false;
    }

    // 时间格式化为 HH:MM
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    // 时间变了才重绘
    if (strcmp(buf, lastHM) != 0) {
        strcpy(lastHM, buf);
        // 是否背景图模式（决定亮段颜色与灭段是否绘制）
        bool bgMode = wallpaperHas();
        // 亮段颜色：有背景自适应，无背景白色
        uint16_t onCol = bgMode ? wf_bg_fgColor() : TFT_WHITE;
        uint16_t offCol = 0x39E7;                 // 黑底模式灭段深灰
        // 先把时间区恢复成纯背景（清掉旧数字）
        wf_bg_restoreRect(tft, 2);
        // 从起始 x 依次画：时十位/时个位/冒号/分十位/分个位
        int x = SEG_X0;
        drawSegDigit(tft, x, SEG_Y, buf[0]-'0', onCol, offCol, !bgMode); x += SEG_DW + SEG_GAP;
        drawSegDigit(tft, x, SEG_Y, buf[1]-'0', onCol, offCol, !bgMode); x += SEG_DW + SEG_GAP;
        drawSegColon(tft, x + 5, SEG_Y + SEG_DH / 2, onCol);
        x += 16 + SEG_GAP;
        drawSegDigit(tft, x, SEG_Y, buf[3]-'0', onCol, offCol, !bgMode); x += SEG_DW + SEG_GAP;
        drawSegDigit(tft, x, SEG_Y, buf[4]-'0', onCol, offCol, !bgMode);
    }

    time_t now_t = time(nullptr);
    struct tm* ti = localtime(&now_t);
    if (ti && now_t > 1000000000) {
        wf_seg_fmtDate(ti, buf, sizeof(buf));
        if (strcmp(buf, lastDate) != 0) {
            strcpy(lastDate, buf);
            tft->loadFont(font_vlw_big);
            int tw = tft->textWidth(buf);
            wf_bg_drawText(tft, 0, buf, (240 - tw) / 2, 52);
            tft->unloadFont();
        }
    }
}

// 注册表盘⑥：名称"七段数码管"
WatchFace wf_seven_seg = {
    .name = "七段数码管",
    .init = NULL,
    .render = wf_seven_seg_render
};

