/*
 * 运动板块 — sport.h
 * 功能：基于 T-Watch 板载 BMA423 三轴加速度计的计步/运动检测模块
 * 架构：本模块为独立板块，不依赖 TTGOClass，只使用 BMA423 官方 Bosch 驱动
 *      （位于 bma423/ 子目录），I2C 走 Wire1（与 AXP202 同总线）。
 * 使用：sport_init() 在 setup 中调用一次；sport_handleLoop() 在 loop 中周期调用
 * 串口标签：[SPORT]
 */
#ifndef SPORT_MODULE_H
#define SPORT_MODULE_H

#include <Arduino.h>
#include <TFT_eSPI.h>

/* ===== BMA423 硬件配置 ===== */
#define BMA423_I2C_ADDR   0x18    // BMA423 I2C 地址（PRIMARY）
#define BMA423_INT1       39      // BMA423 中断引脚（T-Watch 2020 全系）

/* ===== 运动数据类型 ===== */
#define SPORT_ACTIVITY_UNKNOWN   -1   // 未知/无效
#define SPORT_ACTIVITY_STILL      0   // 静止
#define SPORT_ACTIVITY_WALKING    1   // 步行
#define SPORT_ACTIVITY_RUNNING    2   // 跑步

/* ===== 运动状态（全局可见） ===== */
extern bool      sportAvailable;     // BMA423 是否初始化成功
extern uint32_t  sportSteps;         // 今日步数（芯片硬件累计）
extern int       sportActivity;      // 当前运动状态（见 SPORT_ACTIVITY_*）
extern float     sportDistanceKm;    // 距离（km，按步幅 0.7m 估算）
extern float     sportCalories;      // 卡路里（kcal，按步数估算）
extern int       sportStepTarget;    // 每日目标步数（默认 8000）
extern bool      sportDirty;         // 运动数据是否有变化（供 UI 局部刷新）

/* ===== 函数声明 ===== */
bool sport_init();                        // 初始化 BMA423：上电、写配置、开启计步/活动识别
void sport_handleLoop(TFT_eSPI* tft);     // 周期刷新：读步数/活动，重算距离卡路里（500ms 节流）
void sport_savePersist();                  // 将今日步数/芯片基数/日期写入 NVS（MQTT 上报前调用）
void sport_resetToday();                  // 清零今日步数
const char* sport_activityName();         // 返回运动状态中文名
bool sport_wristRaised();                 // 抬手检测（BMA423 TILT 中断状态，读即清；供息屏时抬手亮屏）

#endif