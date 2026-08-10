#ifndef BUTTON_MODULE_H
#define BUTTON_MODULE_H

#include <Arduino.h>

/*
 * 按钮模块 — T-Watch V3 专用
 * 按键通过 AXP202 PMU（电源管理芯片）的 PEK 引脚检测
 *
 * 返回值：
 *   BTN_NONE   = 无事件
 *   BTN_CLICK  = 单击（短按 < 1s）
 *   BTN_DCLICK = 双击（400ms 内两次单击）
 *   BTN_HOLD   = 长按（> 1s）
 */

#define BTN_NONE    0
#define BTN_CLICK   1
#define BTN_HOLD    2
#define BTN_DCLICK  3

void btn_init(int pin);
int  btn_update();

#endif
