/*
 * 电源模块：AXP202 电源管理（电量/充电/背光）公共接口
 */

#ifndef POWER_H
#define POWER_H

#include <Arduino.h>
#include <Wire.h>

// AXP202 寄存器地址
#define AXP_ADDR   0x35
#define MOTOR      4      // 马达驱动引脚
#define TFT_BL     12     // 屏幕背光引脚（GPIO12，GPIO 高低电平控制）

// I2C 读写 AXP202
uint8_t readAXP(uint8_t reg);
void   writeAXP(uint8_t reg, uint8_t val);

// 控制 LDO2 输出（屏幕供电）
void setLDO2(bool on);

// 设置 LDO2 电压（1800~3300 mV），同时开启
void setLDO2Voltage(int mV);

// 马达震动（毫秒）
void buzz(int ms);

// 屏幕电源（背光 PWM）控制：息屏 / 亮屏
void screenOff();            // 息屏（背光 PWM 置 0，屏幕不断电，触摸仍可用）
void screenOn();             // 亮屏（恢复背光 PWM）
bool isScreenOff();          // 当前是否处于息屏状态

// 自动息屏开关（30s 无触摸自动息屏；关闭则一直亮屏）
void autoSleepInit();        // 上电读取 NVS 开关状态（默认开）
bool autoSleepEnabled();     // 当前开关状态
void autoSleepSet(bool on);  // 设置开关并保存 NVS

// 电池电量百分比（根据电压线性换算）
int getBatteryPercent();

// 是否正在充电
bool isCharging();

#endif
