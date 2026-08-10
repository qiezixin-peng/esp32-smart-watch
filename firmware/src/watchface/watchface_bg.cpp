#include "watchface_bg.h"
#include "watchface_manager.h"
#include "../wallpaper/wallpaper.h"
#include <stdlib.h>

/* 实现说明 (v29d 内存兜底):
 * 1. 快照: 仅当背景图解码成功时, 在解码完成后分配字形级小快照
 *    (纯背景, 供清旧文字 + setCallback 取色)。
 * 2. 黑底模式: 无背景图 / 解码失败 / 堆不足时, 不分配任何快照,
 *    文字直接实心绘制(黑色填充), 系统零额外内存, 绝不因表盘卡死。
 * 3. 绘制: 快照模式下 vlw 平滑字体 + setCallback 背景取色,
 *    官方抗锯齿路径, 无黑框/色块, 不读屏。 */

#define WF_BG_SLOTS 3

// 文字区矩形：位置(x,y) + 宽高(w,h)，used 标记该槽位是否已登记
struct WfBgRect { int16_t x, y, w, h; bool used; };

// 文字区矩形表（3 个槽位：日期/时间等各占一个）
static WfBgRect _rects[WF_BG_SLOTS];
static uint8_t* _snap[WF_BG_SLOTS] = { NULL, NULL, NULL };  // 纯背景快照 (堆, 仅背景模式)
static uint8_t _brightness = 0;      // 背景平均亮度 0-255 (无图时=0 黑)

// 作用：背景图组件初始化
void wf_bg_begin() {
    wallpaperSnapReset();   // v29m: 清除解码捕获注册
    for (int i = 0; i < WF_BG_SLOTS; i++) {
        if (_snap[i]) { free(_snap[i]); _snap[i] = NULL; }
        _rects[i].used = false;
    }
}

// 只记录矩形; 快照内存在 wf_bg_drawBackground 统一分配 (仅背景解码成功时)
// 作用：登记文字显示区域（用于保存/恢复背景快照）
void wf_bg_registerRect(int slot, int16_t x, int16_t y, int16_t w, int16_t h) {
    if (slot < 0 || slot >= WF_BG_SLOTS) return;
    if (w <= 0 || h <= 0) return;
    // 记录矩形参数并标记已登记
    _rects[slot].x = x; _rects[slot].y = y; _rects[slot].w = w; _rects[slot].h = h;
    _rects[slot].used = true;
}

// 作用：画背景图（或黑底，预览模式跳过背景）
void wf_bg_drawBackground(TFT_eSPI* tft) {
    // 0. 预览模式（表盘选择页）: 只显示表盘本体, 不绘制背景图
    if (wf_compactMode) {
        tft->fillScreen(TFT_BLACK);
        _brightness = 0;
        return;
    }
    // 1. 画背景 (解码/黑底)
    // 查询当前是否已有可用背景图
    bool haveBg = wallpaperHas();
    if (haveBg) {
        // v29m: 解码前分配快照 + 注册捕获区域（解码回调直接抓像素, 不再 readRect 读屏 —— 读回全 0 就是黑框）
        wallpaperSnapReset();
        for (int i = 0; i < WF_BG_SLOTS; i++) {
            if (_snap[i]) { free(_snap[i]); _snap[i] = NULL; }
            if (!_rects[i].used) continue;
            _snap[i] = (uint8_t*)malloc((size_t)_rects[i].w * _rects[i].h * 2);
            if (!_snap[i]) {
                _rects[i].used = false;   // 堆不足: 该区降级为直接叠字, 不涂黑框
                Serial.printf("[WF] slot%d snapshot OOM\n", i);
                continue;
            }
            memset(_snap[i], 0, (size_t)_rects[i].w * _rects[i].h * 2);  // 兜底: 小图未覆盖处不残留垃圾
            wallpaperSnapSet(i, _rects[i].x, _rects[i].y, _rects[i].w, _rects[i].h, (uint16_t*)_snap[i]);
        }
        wallpaperDraw(tft);
        _brightness = wallpaperGetBrightness();
        // 本次解码未覆盖某区: 快照无效, 释放走降级（不涂黑框）
        for (int i = 0; i < WF_BG_SLOTS; i++) {
            if (_rects[i].used && !wallpaperSnapCaptured(i)) {
                free(_snap[i]); _snap[i] = NULL;
                _rects[i].used = false;
            }
        }
    } else {
        tft->fillScreen(TFT_BLACK);
        _brightness = 0;
    }
}

// 恢复文字区为纯背景: 快照模式 pushImage 还原背景像素; 黑底模式 fillRect 黑
// 作用：恢复文字区域的背景快照（文字不覆盖背景）
void wf_bg_restoreRect(TFT_eSPI* tft, int slot) {
    if (slot < 0 || slot >= WF_BG_SLOTS) return;
    if (!_rects[slot].used) return;
    if (_snap[slot]) {
        int16_t rx = _rects[slot].x, ry = _rects[slot].y;
        int16_t rw = _rects[slot].w, rh = _rects[slot].h;
        tft->pushImage(rx, ry, rw, rh, (uint16_t*)_snap[slot]);
    } else {
        tft->fillRect(_rects[slot].x, _rects[slot].y, _rects[slot].w, _rects[slot].h, TFT_BLACK);
    }
}

// 作用：取文字颜色（深底白字/浅底黑字自动适配）
uint16_t wf_bg_fgColor() {
    return (_brightness > 140) ? TFT_BLACK : TFT_WHITE;
}

// setCallback 背景取色回调: 从内存快照返回 (x,y) 处纯背景色
// (仅在快照模式被调用; 坐标 = drawString 屏幕坐标, 字形都在快照区内)
// 作用：读取背景某像素颜色
static uint16_t wf_bg_getColor(uint16_t x, uint16_t y) {
    for (int i = 0; i < WF_BG_SLOTS; i++) {
        if (_rects[i].used && _snap[i]) {
            int16_t rx = _rects[i].x, ry = _rects[i].y;
            if (x >= rx && x < (uint16_t)(rx + _rects[i].w) &&
                y >= ry && y < (uint16_t)(ry + _rects[i].h)) {
                return ((uint16_t*)_snap[i])[(y - ry) * _rects[i].w + (x - rx)];
            }
        }
    }
    return 0x0000;  // 兜底: 正常不会到达 (所有字形都在快照区内)
}

// 叠画平滑文字(不恢复快照): 快照模式按快照背景色混合, 黑底模式透明笔画
// 作用：在背景上画带描边的文字
void wf_bg_drawTextOver(TFT_eSPI* tft, int slot, const char* str, int16_t x, int16_t y) {
    if (slot < 0 || slot >= WF_BG_SLOTS) return;
    if (!_rects[slot].used) return;
    uint16_t fg = wf_bg_fgColor();
    if (_snap[slot]) {
        tft->setCallback(wf_bg_getColor);
        tft->setTextColor(fg);
        tft->drawString(str, x, y, 1);
        tft->setCallback(nullptr);
    } else {
        tft->setTextColor(fg);   // 无背景填充: 只画笔画, 不盖住静态装饰(环)
        tft->drawString(str, x, y, 1);
    }
}

// 作用：在背景上画平滑文字（字带半透明，不破坏背景）
void wf_bg_drawText(TFT_eSPI* tft, int slot, const char* str, int16_t x, int16_t y) {
    if (slot < 0 || slot >= WF_BG_SLOTS) return;
    uint16_t fg = wf_bg_fgColor();
    if (!_rects[slot].used) return;

    // 无快照: 纯黑底模式清旧字(黑框不可见); 有背景图时绝不涂黑框, 直接叠字(可能轻微残影)
    if (!_snap[slot]) {
        if (!wallpaperHas() || wf_compactMode) {
            tft->fillRect(_rects[slot].x, _rects[slot].y, _rects[slot].w, _rects[slot].h, TFT_BLACK);
            tft->setTextColor(fg, TFT_BLACK);
            tft->drawString(str, x, y, 1);
        } else {
            tft->setTextColor(fg);
            tft->drawString(str, x, y, 1);
        }
        return;
    }

    // 快照模式: 恢复纯背景 -> 官方抗锯齿渲染(逐像素混合背景快照色)
    int16_t rx = _rects[slot].x, ry = _rects[slot].y;
    int16_t rw = _rects[slot].w, rh = _rects[slot].h;
    tft->pushImage(rx, ry, rw, rh, (uint16_t*)_snap[slot]);
    tft->setCallback(wf_bg_getColor);
    tft->setTextColor(fg);   // 回调生效时背景色参数被忽略
    tft->drawString(str, x, y, 1);
    tft->setCallback(nullptr);
}
