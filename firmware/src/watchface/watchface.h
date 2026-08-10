/*
 * 表盘模块公共定义（表盘类型枚举/接口）
 */

#ifndef WATCHFACE_H
#define WATCHFACE_H

#include <TFT_eSPI.h>

/*
 * 表盘接口定义
 * 每个表盘实现一个渲染函数，手表通过此接口统一调度
 */

// 表盘渲染函数类型
// tft: 屏幕对象, h/m/s: 时/分/秒
typedef void (*WatchFaceRenderFn)(TFT_eSPI* tft, int h, int m, int s);

// 表盘初始化函数类型（可选，可为 NULL）
typedef void (*WatchFaceInitFn)();

// 表盘描述结构
typedef struct {
    const char*     name;       // 表盘显示名称（中文）
    WatchFaceInitFn init;       // 初始化函数（可选）
    WatchFaceRenderFn render;   // 渲染函数
} WatchFace;

#endif
