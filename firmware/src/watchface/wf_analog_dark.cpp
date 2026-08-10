/*
 * 表盘②：深色模拟表盘
 */

#include "watchface_manager.h"
#include "watchface.h"
#include <TFT_eSPI.h>
#include <math.h>

// 局部刷新缓存（保存上次实际绘制的角度，擦除用同一角度避免残影）
static float dk_lastSdeg = -1, dk_lastMdeg = -1, dk_lastHdeg = -1;

// 作用：渲染深色模拟表盘
void wf_analog_dark_render(TFT_eSPI* tft, int h, int m, int s) {
    // 角度变量（弧度）
    float rad;
    if (wf_firstFrame) {
        // 整屏初始绘制（表盘盘面）
        // 清屏：黑色背景
        tft->fillScreen(TFT_BLACK);
        // 双细环外圈（半径 100，深灰）
        tft->drawCircle(120, 120, 100, TFT_DARKGREY);
        // 双细环内圈（半径 101）：增加立体感
        tft->drawCircle(120, 120, 101, TFT_DARKGREY);
        // 画 60 个分钟刻度（每 6° 一个）
        for (int i = 0; i < 360; i += 6) {
            rad = (i - 90) * (PI / 180.0);
            int x0 = cos(rad) * 97 + 120;
            int y0 = sin(rad) * 97 + 120;
            // 整点刻度（每 30°）用大亮点，其余用单像素小点
            if (i % 30 == 0) tft->fillCircle(x0, y0, 3, TFT_WHITE);
            else tft->drawPixel(x0, y0, TFT_DARKGREY);
        }
        dk_lastSdeg = dk_lastMdeg = dk_lastHdeg = -1;
        wf_firstFrame = false;
    }

    // 秒针角度：1 秒 = 6°
    float sdeg = s * 6.0;
    // 分针角度：1 分 = 6° + 秒针微移
    float mdeg = m * 6.0 + sdeg * 0.01666667;
    // 时针角度：1 小时 = 30° + 分针微移
    float hdeg = h * 30.0 + mdeg * 0.0833333;

    // 擦除旧指针（用上次实际绘制的角度，避免残留成扇形）
    if (dk_lastSdeg >= 0) {
        rad = (dk_lastSdeg - 90) * (PI / 180.0);
        tft->drawLine(120, 120, cos(rad) * 85 + 120, sin(rad) * 85 + 120, TFT_BLACK);
    }
    if (dk_lastMdeg >= 0) {
        rad = (dk_lastMdeg - 90) * (PI / 180.0);
        tft->drawLine(120, 120, cos(rad) * 75 + 120, sin(rad) * 75 + 120, TFT_BLACK);
    }
    if (dk_lastHdeg >= 0) {
        rad = (dk_lastHdeg - 90) * (PI / 180.0);
        tft->drawLine(120, 120, cos(rad) * 50 + 120, sin(rad) * 50 + 120, TFT_BLACK);
        tft->drawLine(121, 120, cos(rad) * 50 + 121, sin(rad) * 50 + 120, TFT_BLACK);
    }
    // 擦掉中心轴心
    tft->fillCircle(120, 120, 4, TFT_BLACK);

    // 画新指针
    rad = (sdeg - 90) * (PI / 180.0);
    // 红色秒针（长 85 像素）
    tft->drawLine(120, 120, cos(rad) * 85 + 120, sin(rad) * 85 + 120, TFT_RED);
    rad = (mdeg - 90) * (PI / 180.0);
    // 青色分针（长 75 像素）
    tft->drawLine(120, 120, cos(rad) * 75 + 120, sin(rad) * 75 + 120, TFT_CYAN);
    rad = (hdeg - 90) * (PI / 180.0);
    // 黄色时针（长 50 像素，左侧一条）
    tft->drawLine(120, 120, cos(rad) * 50 + 120, sin(rad) * 50 + 120, TFT_YELLOW);
    // 黄色时针右侧加粗条（x 偏移 1 像素）
    tft->drawLine(121, 120, cos(rad) * 50 + 121, sin(rad) * 50 + 120, TFT_YELLOW);
    // 红色轴心
    tft->fillCircle(120, 120, 4, TFT_RED);

    dk_lastSdeg = sdeg; dk_lastMdeg = mdeg; dk_lastHdeg = hdeg;
}

// 注册表盘②：名称"深色简约"
WatchFace wf_analog_dark = {
    .name = "深色简约",
    .init = NULL,
    .render = wf_analog_dark_render
};
