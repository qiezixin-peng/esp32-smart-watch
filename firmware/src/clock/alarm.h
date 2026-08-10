#ifndef CLOCK_ALARM_H
#define CLOCK_ALARM_H

#include <Arduino.h>
#include <TFT_eSPI.h>

/* ==========================================================
 * 闹钟模块
 * - 最多 6 个闹钟
 * - 支持周几重复（days[7]，0=周一...6=周日）；全部 false = 仅一次
 * - NVS 存储（Preferences "clock" 命名空间，key "alarms"）
 * ========================================================== */

#define ALARM_MAX      6       // 最多 6 个闹钟
#define ALARM_LIST_H   34      // 列表每项高度（px）
#define ALARM_LIST_Y0  46      // 列表起始 y（标签栏下方）
#define ALARM_ADD_Y   210      // 底部[+添加闹钟]按钮 y
#define ALARM_ADD_H   26       // 按钮高度

typedef struct {
    bool     enabled;          // 是否启用
    uint8_t  hour;             // 时 0-23
    uint8_t  minute;           // 分 0-59
    bool     once;             // 仅响一次（保存前根据 days 计算）
    bool     days[7];          // 重复周几（0=周一...6=周日）
    bool     configured;       // 是否已创建（false=空槽位）
} AlarmItem;

// 初始化闹钟（从 NVS 加载）
void alarm_init();

// 保存到 NVS
void alarm_save();

// 获取闹钟（0-5），越界返回 NULL
AlarmItem* alarm_get(int index);

// 已创建闹钟数量（configured 槽位数）
int alarm_visibleCount();

// 计算当前是否有闹钟到点（返回触发闹钟索引，无则 -1）
// 会处理"仅一次"闹钟的自动关闭
int alarm_checkTrigger();

// 绘制闹钟列表页（scrollOffset 支持滚动）
void alarm_drawList(TFT_eSPI* tft, int scrollOffset);

// 增量滚动重绘（delta = 本次 scrollOffset 变化量）
void alarm_drawListIncremental(TFT_eSPI* tft, int scrollOffset, int delta);

// 绘制闹钟设置页（编辑第 index 个闹钟，-1 = 新增）
void alarm_drawEdit(TFT_eSPI* tft, int index);

// 处理闹钟列表页点击。返回 true 已消费。
// x/y 为屏幕坐标，scrollOffset 用于命中列表项
bool alarm_handleListTap(uint16_t x, uint16_t y, int scrollOffset);

// 处理闹钟设置页点击。返回 true 已消费。
bool alarm_handleEditTap(uint16_t x, uint16_t y);

// 设置页状态
bool alarm_isEditing();
int  alarm_editingIndex();

#endif