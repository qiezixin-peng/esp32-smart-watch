/*
 * 表盘①：经典模拟表盘（黑白指针+刻度）
 */

#include "watchface.h"
#include "watchface_manager.h"
#include <TFT_eSPI.h>
#include <math.h>

// 局部刷新缓存（保存上次实际绘制的角度，擦除用同一角度避免残影）
static float cl_lastSdeg = -1, cl_lastMdeg = -1, cl_lastHdeg = -1;

// 作用：渲染经典模拟表盘（黑白指针+刻度）
void wf_analog_classic_render(TFT_eSPI* tft, int h, int m, int s) {
    // 角度变量（弧度，供三角函数使用）
    float rad;
    if (wf_firstFrame) {
        // 整屏初始绘制（表盘盘面）
        // 清屏：黑色背景
        tft->fillScreen(TFT_BLACK);
        // 白色外圆（半径 108）：表盘底盘
        tft->fillCircle(120, 120, 108, TFT_WHITE);
        // 挖出黑色内盘：只留白色圆环边框
        tft->fillCircle(120, 120, 98, TFT_BLACK);
        // 画 12 个整点刻度（每 30° 一个）
        for (int i = 0; i < 360; i += 30) {
            rad = (i - 90) * (PI / 180.0);
            int x0 = cos(rad) * 100 + 120;
            int y0 = sin(rad) * 100 + 120;
            int x1 = cos(rad) * 88 + 120;
            int y1 = sin(rad) * 88 + 120;
            tft->drawLine(x0, y0, x1, y1, TFT_WHITE);
        }
        // 中心白色轴心
        tft->fillCircle(120, 120, 4, TFT_WHITE);
        cl_lastSdeg = cl_lastMdeg = cl_lastHdeg = -1;
        wf_firstFrame = false;
    }

    // Hand angles
    // 秒针角度：1 秒 = 6°（一圈 360°÷60 秒）
    float sdeg = s * 6.0;
    // 分针角度：1 分 = 6°，叠加秒针微移（每秒 0.1°）
    float mdeg = m * 6.0 + sdeg * 0.01666667;
    // 时针角度：1 小时 = 30°，叠加分针微移
    float hdeg = h * 30.0 + mdeg * 0.0833333;

    // 擦除旧指针（用上次实际绘制的角度，避免残留成扇形）
    if (cl_lastSdeg >= 0) {
        rad = (cl_lastSdeg - 90) * (PI / 180.0);
        tft->drawLine(120, 120, cos(rad) * 90 + 120, sin(rad) * 90 + 120, TFT_BLACK);
    }
    if (cl_lastMdeg >= 0) {
        rad = (cl_lastMdeg - 90) * (PI / 180.0);
        tft->drawLine(120, 120, cos(rad) * 80 + 120, sin(rad) * 80 + 120, TFT_BLACK);
    }
    if (cl_lastHdeg >= 0) {
        rad = (cl_lastHdeg - 90) * (PI / 180.0);
        tft->drawLine(120, 120, cos(rad) * 55 + 120, sin(rad) * 55 + 120, TFT_BLACK);
    }
    // 擦掉中心轴心
    tft->fillCircle(120, 120, 4, TFT_BLACK);

    // 画新指针
    rad = (sdeg - 90) * (PI / 180.0);
    // 红色秒针（长 90 像素）
    tft->drawLine(120, 120, cos(rad) * 90 + 120, sin(rad) * 90 + 120, TFT_RED);
    rad = (mdeg - 90) * (PI / 180.0);
    // 白色分针（长 80 像素）
    tft->drawLine(120, 120, cos(rad) * 80 + 120, sin(rad) * 80 + 120, TFT_WHITE);
    rad = (hdeg - 90) * (PI / 180.0);
    // 白色时针（长 55 像素）
    tft->drawLine(120, 120, cos(rad) * 55 + 120, sin(rad) * 55 + 120, TFT_WHITE);
    // 红色小轴心盖住三根指针根部
    tft->fillCircle(120, 120, 3, TFT_RED);

    cl_lastSdeg = sdeg; cl_lastMdeg = mdeg; cl_lastHdeg = hdeg;
}

// 注册表盘①：名称"经典白盘"，无 init 回调，render 指向本文件渲染函数
WatchFace wf_analog_classic = {
    .name = "经典白盘",
    .init = NULL,
    .render = wf_analog_classic_render
};
