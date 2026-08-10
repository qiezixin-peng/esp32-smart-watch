#ifndef WATCHFACE_BG_MODULE_H
#define WATCHFACE_BG_MODULE_H
/* ==========================================================
 * 表盘背景公共模块 - 平滑文字直接印在背景上 (v29b 优化)
 * 功能: 时间/日期/星期用 vlw 平滑字体(每像素带 alpha) +
 *       setCallback 背景取色, 走 TFT_eSPI 官方抗锯齿渲染路径,
 *       文字像直接印在背景图上: 边缘平滑、背景透出字形、
 *       无黑框/遮罩/色块, 不做抠图避让。
 * 优化(v29b): 本模块不再加载/卸载字体(调用方负责, 一次加载),
 *       避免高频 loadFont 导致的堆碎片与卡顿。
 * ==========================================================
 * 用法 (在表盘 render 的 wf_firstFrame 分支中):
 *   wf_bg_begin();
 *   wf_bg_registerRect(0, x,y,w,h);         // 日期区 (slot 0)
 *   wf_bg_registerRect(1, x,y,w,h);         // 星期区 (slot 1)
 *   wf_bg_registerRect(2, x,y,w,h);         // 时间区 (slot 2)
 *   wf_bg_drawBackground(tft);              // 画背景 + 保存文字区快照
 * 每帧渲染 (文字变化时):
 *   tft->loadFont(font_vlw_time);           // 先加载对应平滑字体
 *   wf_bg_drawText(tft, slot, str, x, y);   // 平滑文字 (y=基线)
 *   tft->unloadFont();                      // 调用方卸载
 * ========================================================== */
#include <TFT_eSPI.h>

// 新一帧: 清空已注册文字区
void wf_bg_begin();

// 注册文字区矩形 (slot: 0=日期 1=星期 2=时间; 超出快照容量忽略)
void wf_bg_registerRect(int slot, int16_t x, int16_t y, int16_t w, int16_t h);

// 画背景: 有图则解码显示 + 统计亮度 + 保存文字区快照; 无图则黑底
void wf_bg_drawBackground(TFT_eSPI* tft);

// 恢复文字区为纯背景（快照存在->pushImage 还原; 黑底模式->fillRect 黑）
// 供自绘元素（七段/点阵/环形等）清旧再重画, 不经过 drawString
void wf_bg_restoreRect(TFT_eSPI* tft, int slot);

// 不恢复快照, 直接在当前屏幕内容上叠画平滑文字 (v29u)
// 供"恢复背景 + 重画静态装饰 + 叠字"场景(环形轨道): 调用方先 wf_bg_restoreRect
// 清旧字并重画装饰, 再叠字, 避免 drawText 内部恢复纯背景把装饰擦掉
// 快照模式: 文字边缘按纯背景快照色混合; 黑底模式: 透明背景只画笔画(不盖装饰)
// 注意: 调用前必须已 loadFont; 本函数不加载/卸载字体
void wf_bg_drawTextOver(TFT_eSPI* tft, int slot, const char* str, int16_t x, int16_t y);

// 智能前景色: 深背景->白字, 浅背景->黑字
uint16_t wf_bg_fgColor();

// 平滑文字: 内部先恢复文字区背景, 再用当前已加载的 vlw 平滑字体 +
// 背景回调逐像素 alpha 混合绘制 (官方抗锯齿路径, 不读屏, 无黑框)
// 注意: 调用前必须已 loadFont; 本函数不加载/卸载字体
// slot: 0=日期 1=星期 2=时间; y 为基线
void wf_bg_drawText(TFT_eSPI* tft, int slot, const char* str, int16_t x, int16_t y);

#endif
