/*
 * 表盘管理器：表盘注册与切换的公共接口
 */

#ifndef WATCHFACE_MANAGER_H
#define WATCHFACE_MANAGER_H

#include <TFT_eSPI.h>
#include "watchface.h"

/*
 * 表盘管理器
 * 管理所有表盘列表、当前选中、NVS 存储
 */

// 初始化管理器（从 NVS 加载上次选中的表盘）
void wf_init();

// 获取表盘总数
int wf_getCount();

// 获取指定索引的表盘名称
const char* wf_getName(int index);

// 获取当前表盘索引
int wf_getCurrent();

// 设置当前表盘并保存到 NVS（锁定）
void wf_setCurrent(int index);

// 切换到下一个表盘（循环）
void wf_next();

// 切换到上一个表盘（循环）
void wf_prev();

// 渲染当前表盘
void wf_render(TFT_eSPI* tft, int h, int m, int s);
void wf_invalidateCache();

extern bool wf_compactMode;
extern bool wf_firstFrame;

#endif

