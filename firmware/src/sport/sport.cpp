/*
 * 运动板块 — sport.cpp
 * 功能：初始化 T-Watch 板载 BMA423，实现计步/运动状态检测
 * 架构：
 *   - 驱动层：bma423/ 子目录（Bosch 官方 bma4 + bma423 驱动，纯 C）
 *   - 业务层：本文件封装 I2C 读写回调（Wire1）、初始化序列、数据读取
 * 原理：
 *   - BMA423 芯片内置硬件计步器（Step Counter）与活动识别（Activity Recognition），
 *     只需配置并读取寄存器，无需软件滤波算法，省电且准确
 *   - 步数通过 bma423_step_counter_output() 轮询读取（500ms 节流）
 *   - 运动状态通过 bma423_activity_output() 读取：静止/步行/跑步
 * 串口标签：[SPORT]
 * ================================
 * 本文件阅读要点：
 *  - 步数体系：芯片硬件计步（永不清零，除非 reset）→ 软件维护"今日基数+增量"→ NVS 持久化
 *  - 抬手检测：芯片中断(TILT/WAKEUP) + 软件倾角下降沿 双通道（v29h 增强）
 *  - 调用方：sport_handleLoop 由 main loop 每帧调用（内部 200ms 节流）
 * ================================
 */
#include "sport.h"
#include <Wire.h>
#include <Preferences.h>
#include <time.h>
#include "bma423/bma423.h"
#include "bma423/bma4.h"

/* ===== 全局运动状态 ===== */
/* ===== 全局运动状态（跨文件使用）===== */
// sportAvailable：BMA423 是否初始化成功（失败则所有运动功能禁用）
// sportSteps：今日累计步数；sportActivity：当前活动状态(静止/步行/跑步)
// sportDistanceKm/sportCalories：由步数派生的距离/卡路里
// sportStepTarget：目标步数（进度条用）；sportDirty：运动数据变化标志（UI 局部刷新用）
bool      sportAvailable  = false;
uint32_t  sportSteps      = 0;
int       sportActivity   = SPORT_ACTIVITY_UNKNOWN;
float     sportDistanceKm = 0.0f;
float     sportCalories   = 0.0f;
int       sportStepTarget = 1000;
bool      sportDirty      = false;

// 步数派生参数的估算系数：
//  - 步幅 0.7 米/步（成年人平均值）
//  - 每步约 0.04 千卡（粗略估算，非医学精度）
/* ===== 估算参数 ===== */
#define SPORT_STRIDE_M      0.7f   // 平均步幅（米）
#define SPORT_CAL_PER_STEP  0.04f  // 每步卡路里（千卡，粗略估算）

/* ===== 每日步数持久化（NVS）===== */
#define SPORT_NS_NAME       "sport"        // NVS namespace
#define SPORT_KEY_SAVED     "savedSteps"   // 上次保存的今日累计步数（uint32）
#define SPORT_KEY_CHIP      "savedChip"    // 上次保存时芯片计步值（uint32）
#define SPORT_KEY_DAY       "day"          // 上次保存的日期 YYYYMMDD（uint32）

// 步数持久化原理：
//  persistBase：NVS 里保存的"今日累计步数"（跨重启恢复）
//  persistChip：保存时刻的芯片计步值（算增量用：今日步数 = 基数 + 芯片当前值 - 芯片基数）
//  persistDay ：保存时的日期 YYYYMMDD（跨天判断用）
static uint32_t persistBase   = 0;   // NVS 恢复的今日步数基数
static uint32_t persistChip   = 0;   // 芯片计步基数（用于计算增量）
static uint32_t persistDay    = 0;   // 上次保存/恢复的日期

// 获取当前日期 YYYYMMDD（依赖 NTP/系统时间；未同步时返回 0）
static uint32_t sport_today()
// 把系统时间转成 YYYYMMDD 整数（如 20260807），用于判断"是否跨天"
{
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (tmv.tm_year < 100) return 0;   // 时间未同步（1970 年之前）
    // NTP 未校时（年份<2000）返回 0：调用方据此跳过跨天判断/落盘
    return (uint32_t)((tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday);
}

/* ===== BMA423 设备对象（静态） ===== */
static struct bma4_dev bmaDev;

/* ==========================================================
 * I2C 读写回调（Bosch 驱动要求，走 Wire1，与 AXP202 同总线）
 * ========================================================== */
static uint16_t bma_i2c_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
// Bosch 驱动要求的 I2C 读回调：先发寄存器地址，再连续读 len 字节
// 返回值 BMA4_OK=0 表示成功（Bosch 驱动约定 0 为 OK）
{
    Wire1.beginTransmission(dev_addr);
    Wire1.write(reg_addr);
    Wire1.endTransmission(false);
    Wire1.requestFrom((int)dev_addr, (int)len);
    uint32_t i = 0;
    while (Wire1.available() && i < len) {
        data[i++] = Wire1.read();
    }
    return (i == len) ? BMA4_OK : BMA4_E_FAIL;   // 0=成功（Bosch 约定）
}

static uint16_t bma_i2c_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
// I2C 写回调：先发寄存器地址，再写 data 前 len 字节；endTransmission 返回 0 表示 ACK 成功
{
    Wire1.beginTransmission(dev_addr);
    Wire1.write(reg_addr);
    for (uint32_t i = 0; i < len; i++) {
        Wire1.write(data[i]);
    }
    return (Wire1.endTransmission() == 0) ? BMA4_OK : BMA4_E_FAIL;
}

/* ==========================================================
 * I2C 总线扫描（诊断用）：列出 Wire1 上所有有响应的设备地址
 * 调用时机：sport_init 开头，用于确认 BMA423 实际挂在哪个地址
 * ========================================================== */
static void sport_scan_i2c()
// 诊断函数：扫描 0x08~0x77 全部地址，打印有响应的设备（开机日志可见 [SPORT] found）
{
    Serial.println("[SPORT] I2C1 scan start");
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire1.beginTransmission(addr);
        if (Wire1.endTransmission() == 0) {
            Serial.printf("[SPORT] found device at 0x%02X\n", addr);
            found++;
        }
    }
    if (found == 0) Serial.println("[SPORT] I2C1 scan: no device found");
    Serial.println("[SPORT] I2C1 scan done");
}
/* ==========================================================
 * 运动状态中文名
 * ========================================================== */
const char* sport_activityName()
// 把 BMA423 活动识别结果转成中文显示文字（运动页"状态"行用）
{
    switch (sportActivity) {
        case SPORT_ACTIVITY_STILL:   return "静止";
        case SPORT_ACTIVITY_WALKING: return "步行中";
        case SPORT_ACTIVITY_RUNNING: return "跑步中";
        default:                     return "未知";
    }
}

/* ==========================================================
 * 初始化 BMA423
 * 序列：驱动对象 → 芯片初始化 → 写配置流 → 加速度计配置
 *       → 使能计步/活动识别 → 清零步数
 * ========================================================== */
// 作用：初始化 BMA423：I2C 扫描地址→写配置→恢复今日步数
bool sport_init()
{
    // 0. 诊断：扫描 I2C1 总线，确认 BMA423 实际地址
    // 扫描结果不影响流程：即使找不到设备也会继续尝试 0x18/0x19
    sport_scan_i2c();

    // 1. 探测 BMA423 地址：首选 0x18（SDO=0），无响应则尝试 0x19（SDO=1）
    uint8_t bmaAddr = BMA4_I2C_ADDR_PRIMARY;         // 0x18
    Wire1.beginTransmission(bmaAddr);
    if (Wire1.endTransmission() != 0) {
        bmaAddr = BMA4_I2C_ADDR_SECONDARY;           // 0x19
        Wire1.beginTransmission(bmaAddr);
        if (Wire1.endTransmission() != 0) {
            Serial.println("[SPORT] BMA423 not found on I2C1 (0x18/0x19)");
            sportAvailable = false;
            return false;
        }
        Serial.println("[SPORT] BMA423 found at 0x19 (secondary addr)");
    // 找到地址后继续（0x18/0x19 二选一）
    }

    // 2. 填充驱动对象
    // 把 I2C 回调/延时/配置填进 Bosch 驱动结构体，后续所有 bma4_xxx 调用都靠它通信
    memset(&bmaDev, 0, sizeof(bmaDev));
    bmaDev.dev_addr       = bmaAddr;
    bmaDev.interface      = BMA4_I2C_INTERFACE;      // I2C
    bmaDev.bus_read       = bma_i2c_read;
    bmaDev.bus_write      = bma_i2c_write;
    bmaDev.delay          = delay;
    bmaDev.read_write_len = 32;
    bmaDev.resolution     = 12;
    bmaDev.feature_len    = BMA423_FEATURE_SIZE;     // 64

    // 3. 芯片复位 + 初始化
    // 先软复位（命令寄存器写 0xB6），等 30ms 让芯片就绪，再正式初始化驱动
    uint8_t rst = 0xB6;
    bma_i2c_write(bmaAddr, BMA4_CMD_ADDR, &rst, 1);   // soft reset 命令寄存器
    delay(30);

    if (bma423_init(&bmaDev) != BMA4_OK) {
        uint8_t cid = 0;
        bma_i2c_read(bmaAddr, BMA4_CHIP_ID_ADDR, &cid, 1);
        Serial.printf("[SPORT] bma423_init FAIL (chip_id=0x%02X)\n", cid);
        sportAvailable = false;
        return false;
    }

    // 4. 检查 ASIC 是否已初始化，未初始化则写入配置流（启用步数/活动特征）
    // BMA423 首次上电需写一次配置流（固件），成功后 ASIC 标志位置位，之后不用重复写
    uint8_t asic = 0;
    bma_i2c_read(bmaAddr, BMA4_INTERNAL_STAT, &asic, 1);
    if (asic != BMA4_ASIC_INITIALIZED) {
        Serial.println("[SPORT] bma423 write config file...");
        uint16_t cfgRslt = bma423_write_config_file(&bmaDev);
        if (cfgRslt != BMA4_OK) {
            Serial.printf("[SPORT] write config FAIL (rslt=0x%04X)\n", cfgRslt);
            sportAvailable = false;
            return false;
        }
    }

    // 5. 配置加速度计：100Hz、±2g、普通滤波、连续模式
    // 采样率 100Hz 足够计步/抬手；量程 ±2g 灵敏度最高（重力约 1g）
    struct bma4_accel_config accelCfg;
    accelCfg.odr       = BMA4_OUTPUT_DATA_RATE_100HZ;
    accelCfg.range     = BMA4_ACCEL_RANGE_2G;
    accelCfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;
    accelCfg.perf_mode = BMA4_CONTINUOUS_MODE;
    if (bma4_set_accel_config(&accelCfg, &bmaDev) != BMA4_OK) {
        Serial.println("[SPORT] accel config FAIL");
        sportAvailable = false;
        return false;
    }
    if (bma4_set_accel_enable(BMA4_ENABLE, &bmaDev) != BMA4_OK) {
        Serial.println("[SPORT] accel enable FAIL");
        sportAvailable = false;
        return false;
    }

    // 6. 使能计步器 + 活动识别特征，清零今日步数
    // 打开硬件计步/活动识别/步数检测特征，并把芯片计步清零（配合 NVS 基数做"今日步数"）
    uint16_t fStep = bma423_feature_enable(BMA423_STEP_CNTR, BMA4_ENABLE, &bmaDev);
    uint16_t fAct  = bma423_feature_enable(BMA423_ACTIVITY, BMA4_ENABLE, &bmaDev);
    uint16_t fDet  = bma423_step_detector_enable(BMA4_ENABLE, &bmaDev);
    uint16_t fRst  = bma423_reset_step_counter(&bmaDev);
    Serial.printf("[SPORT] feature step=0x%04X act=0x%04X det=0x%04X rst=0x%04X\n", fStep, fAct, fDet, fRst);

    // 7. 启用抬手检测（BMA423 TILT 翻转 + WAKEUP 运动唤醒特征）+ 映射到 INT1（GPIO39）
    // 中断引脚映射 INT1 并开启 TILT/WAKEUP 两个特征：
    //  TILT=翻转角度事件，WAKEUP=运动唤醒事件，任一触发都会置位中断状态（供轮询读取）
    //    供息屏状态下「抬手亮屏」使用：sport_wristRaised() 轮询中断状态
    struct bma4_int_pin_config intPinCfg;
    memset(&intPinCfg, 0, sizeof(intPinCfg));
    intPinCfg.edge_ctrl = BMA4_LEVEL_TRIGGER;   // 电平触发
    intPinCfg.lvl       = BMA4_ACTIVE_HIGH;     // 高电平有效
    intPinCfg.od        = BMA4_PUSH_PULL;       // 推挽输出
    intPinCfg.output_en = BMA4_OUTPUT_ENABLE;   // 使能输出
    intPinCfg.input_en  = BMA4_INPUT_ENABLE;    // 使能输入
    uint16_t fPin    = bma4_set_int_pin_config(&intPinCfg, BMA4_INTR1_MAP, &bmaDev);
    uint16_t fTilt   = bma423_feature_enable(BMA423_TILT, BMA4_ENABLE, &bmaDev);
    uint16_t fWakeup = bma423_feature_enable(BMA423_WAKEUP, BMA4_ENABLE, &bmaDev);
    uint16_t fMap    = bma423_map_interrupt(BMA4_INTR1_MAP, BMA423_TILT_INT | BMA423_WAKEUP_INT, BMA4_ENABLE, &bmaDev);
    Serial.printf("[SPORT] tilt pin=0x%04X feat=0x%04X wake=0x%04X map=0x%04X\n", fPin, fTilt, fWakeup, fMap);

    // 恢复今日步数基数（NVS）：同一天则恢复累计，跨天/首次则从 0 开始
    // 开机恢复策略：
    //  - 同一天：恢复 NVS 里的累计数（跨重启不丢步数）
    //  - 跨天/首次：从 0 开始（新的一天）
    //  - NTP 未同步：保守恢复 NVS 数据，待时间同步后由 handleLoop 跨天检查校正
    {
        Preferences prefs;
        prefs.begin(SPORT_NS_NAME, true);
        persistDay = prefs.getUInt(SPORT_KEY_DAY, 0);
        uint32_t saved = prefs.getUInt(SPORT_KEY_SAVED, 0);
        uint32_t chip  = prefs.getUInt(SPORT_KEY_CHIP, 0);
        prefs.end();
        uint32_t today = sport_today();
        if (today != 0 && today == persistDay) {
            // 同日：恢复今日累计
            persistBase = saved;
            persistChip = chip;
        } else if (today != 0) {
            // 跨天（或首次）：从 0 开始
            // 新的一天：基数清零，芯片刚 reset 也从 0 计
            persistBase = 0;
            persistChip = 0;   // 芯片刚 reset，从 0 计
        } else {
            // 时间未同步（NTP 未完成）：保守恢复 NVS 数据避免丢步数，
            // 待 loop 中时间同步后由跨天检查校正
            persistBase = saved;
            persistChip = chip;
            Serial.printf("[SPORT] NTP not ready, keep NVS base=%u chip=%u\n", persistBase, persistChip);
        }
        Serial.printf("[SPORT] NVS restore base=%u chip=%u day=%u\n", persistBase, persistChip, persistDay);
    }

    sportSteps = persistBase;
    // 用恢复的基数初始化今日步数，并同步派生出距离/卡路里（避免开机上报 0）
    sportActivity = SPORT_ACTIVITY_STILL;
    // 距离/卡路里为步数派生数据：恢复步数时同步恢复，避免开机上报 0
    sportDistanceKm = persistBase * SPORT_STRIDE_M / 1000.0f;
    sportCalories   = persistBase * SPORT_CAL_PER_STEP;
    sportDirty = true;
    sportAvailable = true;
    Serial.println("[SPORT] BMA423 initialized OK (step + activity + tilt + wakeup)");
    return true;
}

/* ==========================================================
 * 抬手检测（v29h 增强）：
 *   1) 芯片中断：BMA423 TILT / WAKEUP 任一位置位即抬手（读即清）
 *   2) 软件倾角检测：读加速度原始数据算表盘倾角，
 *      检测「从接近竖直 → 表盘朝上」的下降沿 = 抬手看表动作
 *      （不依赖芯片特征状态机，真机更灵敏、阈值可调）
 * 注意：中断状态读即清；诊断与判断必须在同一次读取内完成
 * 调用方：息屏状态下 main loop 周期轮询，用于抬手亮屏
 * ========================================================== */
// 抬手检测参数（阈值越小越灵敏；真机可凭串口 tilt 值微调）
#define SPORT_WRIST_TILT_HIGH   50.0f   // 上一帧倾角大于此值（接近竖直/手臂下垂）
#define SPORT_WRIST_TILT_LOW    38.0f   // 当前倾角小于此值（表盘接近朝上 = 正在看表）
#define SPORT_WRIST_COOLDOWN_MS 2000    // 触发冷却：防止连续误触（毫秒）
#define SPORT_WRIST_SAMPLE_MS   60      // 倾角采样间隔（毫秒），控制 I2C 负载
// 作用：检测抬手动作（芯片中断 + 软件倾角双通道）
// 双通道任一触发即判定"抬手"：芯片中断快但可能不触发，软件倾角稳定可靠（双保险）
bool sport_wristRaised()
{
    if (!sportAvailable) return false;
    static unsigned long lastSample = 0, lastTrigger = 0, lastLog = 0;
    unsigned long now = millis();

    // ---- 通道1：芯片中断（TILT 翻转 / WAKEUP 运动唤醒）----
    // 读中断状态（读即清！）：只要 TILT 或 WAKEUP 位为 1 就认为有抬手动作
    // 冷却 2 秒防连触：冷却期内不重复触发
    uint16_t intStatus = 0;
    if (bma423_read_int_status(&intStatus, &bmaDev) == BMA4_OK) {
        if (intStatus & (BMA423_TILT_INT | BMA423_WAKEUP_INT)) {
            if (now - lastLog > 2000) { lastLog = now; Serial.printf("[SPORT] wrist INT=0x%04X\n", intStatus); }
            if (now - lastTrigger >= SPORT_WRIST_COOLDOWN_MS) { lastTrigger = now; return true; }
        }
    }

    // ---- 通道2：软件倾角检测（读加速度原始数据，稳定可靠）----
    // 60ms 采样间隔：控制 I2C 读取频率，避免占满总线
    if (now - lastSample < SPORT_WRIST_SAMPLE_MS) return false;
    lastSample = now;

    struct bma4_accel acc;
    if (bma4_read_accel_xyz(&acc, &bmaDev) != BMA4_OK) return false;

    // 倾角 = 表盘法线与重力方向的夹角（0°=表盘朝上，90°=表盘竖直）
    // 用 xy 平面加速度和 + z 轴绝对值算倾角：
    //   horiz = 水平方向合加速度，tilt = atan2(水平, 竖直) 即表盘与水平面的夹角
    float horiz = sqrtf((float)acc.x * acc.x + (float)acc.y * acc.y);
    float tiltDeg = atan2f(horiz, fabsf((float)acc.z)) * 180.0f / 3.14159265f;

    static float lastTilt = -1.0f;
    if (lastTilt >= 0.0f) {
        // 下降沿：从「接近竖直」变「表盘朝上」= 抬手看表动作
    // 关键判定：上一帧倾角 >50°（手垂下/竖直），当前 <38°（表盘朝上）→ 抬手看表
    // 冷却期内不触发；阈值可调（SPORT_WRIST_TILT_HIGH/LOW）
        if (lastTilt > SPORT_WRIST_TILT_HIGH && tiltDeg < SPORT_WRIST_TILT_LOW
            && now - lastTrigger >= SPORT_WRIST_COOLDOWN_MS) {
            lastTrigger = now;
            Serial.printf("[SPORT] wrist raise (tilt %.0f -> %.0f)\n", lastTilt, tiltDeg);
            return true;
        }
    }
    lastTilt = tiltDeg;

    // 诊断（2 秒节流）：打印倾角，真机验证时据此微调阈值
    // 串口持续输出当前倾角：真机微调阈值时直接看这个值
    if (now - lastLog > 2000) { lastLog = now; Serial.printf("[SPORT] tilt=%.0f\n", tiltDeg); }
    return false;
}

/* ==========================================================
 * 周期刷新：读取步数/活动，重算距离卡路里（500ms 节流）
 * 调用方：main loop 每帧调用；内部自行节流
 * ========================================================== */
// 作用：后台计步循环：读芯片步数→算距离/卡路里→每5秒存NVS（任何页面都运行）
// main loop 每帧调用，内部 200ms 节流；即使息屏/在其他页面也在累计步数
void sport_handleLoop(TFT_eSPI* tft)
{
    static unsigned long lastRead = 0;
    unsigned long now = millis();
    if (!sportAvailable || now - lastRead < 200) return;
    lastRead = now;

    uint32_t steps = 0;
    // 读芯片硬件计步器当前值（芯片自己累计，不会因重启/页面切换丢失）
    if (bma423_step_counter_output(&steps, &bmaDev) == BMA4_OK) {
        // 跨天检查：日期变化则重置今日步数（以当前芯片值为新基数）
        // 零点跨天后：今日步数从 0 重新计，同时把 NVS 落盘（当天新基数）
        uint32_t today = sport_today();
        if (today != 0 && today != persistDay) {
            persistBase = 0;
            persistChip = steps;
            persistDay  = today;
            Preferences prefs;
            prefs.begin(SPORT_NS_NAME, false);
            prefs.putUInt(SPORT_KEY_SAVED, 0);
            prefs.putUInt(SPORT_KEY_CHIP, steps);
            prefs.putUInt(SPORT_KEY_DAY, today);
            prefs.end();
            Serial.printf("[SPORT] Day changed, reset today steps\n");
        }
        // 今日步数 = 基数 + (芯片当前值 - 芯片基数)；芯片重启（值变小）按从 0 计
        // 芯片值不回退就是正常增量；若芯片重启过（值变小），直接按当前值计
        uint32_t chipNow = (steps >= persistChip) ? (steps - persistChip) : steps;
        uint32_t total   = persistBase + chipNow;
        if (total != sportSteps) {
            // 步数变化才更新 UI 数据（sportDirty），避免无意义重绘
            sportSteps = total;
            sportDistanceKm = total * SPORT_STRIDE_M / 1000.0f;
            sportCalories   = total * SPORT_CAL_PER_STEP;
            sportDirty = true;
        }
    }

    // 每 5 秒将今日步数落盘（独立于 MQTT 上报，防断电/重启丢失；NVS 有磨损均衡）
    // 每 5 秒存一次 NVS：断电/重启后仍能恢复今日步数（NVS 自带磨损均衡）
    static unsigned long lastSave = 0;
    if (now - lastSave >= 5000) {
        lastSave = now;
        sport_savePersist();
    }

    uint8_t act = 0;
    // 读活动识别结果（静止/步行/跑步），变化才更新状态并标记 UI 刷新
    if (bma423_activity_output(&act, &bmaDev) == BMA4_OK) {
        int newAct;
        if      (act == BMA423_USER_RUNNING)    newAct = SPORT_ACTIVITY_RUNNING;
        else if (act == BMA423_USER_WALKING)   newAct = SPORT_ACTIVITY_WALKING;
        else if (act == BMA423_USER_STATIONARY) newAct = SPORT_ACTIVITY_STILL;
        else newAct = SPORT_ACTIVITY_UNKNOWN;
        if (newAct != sportActivity) {
            sportActivity = newAct;
            sportDirty = true;
        }
    }

}

/* ==========================================================
 * 清零今日步数
 * ========================================================== */
// 作用：跨天自动清零当日步数（零点自动重置）
// 手动清零今日步数：重置芯片计步 + 同步更新 NVS 基数，避免重启后步数回跳
void sport_resetToday()
{
    if (!sportAvailable) return;
    bma423_reset_step_counter(&bmaDev);
    sportSteps = 0;
    sportDistanceKm = 0.0f;
    sportCalories = 0.0f;
    sportDirty = true;
    // 同步重置 NVS 基数：以当前芯片值为新基数，避免重启后步数回跳
    uint32_t chip = 0;
    bma423_step_counter_output(&chip, &bmaDev);
    persistBase = 0;
    persistChip = chip;
    persistDay  = sport_today();
    Preferences prefs;
    prefs.begin(SPORT_NS_NAME, false);
    prefs.putUInt(SPORT_KEY_SAVED, 0);
    prefs.putUInt(SPORT_KEY_CHIP, chip);
    prefs.putUInt(SPORT_KEY_DAY, persistDay);
    prefs.end();
}

/* ==========================================================
 * 保存今日步数到 NVS（每 5 秒由 handleLoop 调用；MQTT 上报前也会强制保存一次）
 * 作用：断电/重启后仍能恢复今日累计步数
 * ========================================================== */
// 作用：把步数+日期写入 NVS 持久化（重启不丢）
// 保存当前今日步数 + 芯片值 + 日期：下次开机恢复时用这套数据还原
// 时间未同步不落盘：避免把错误的日期/基数写进 NVS
void sport_savePersist()
{
    if (!sportAvailable) return;
    uint32_t chip = 0;
    bma423_step_counter_output(&chip, &bmaDev);
    uint32_t today = sport_today();
    if (today == 0) return;   // 时间未同步不落盘（避免日期错乱）
    Preferences prefs;
    prefs.begin(SPORT_NS_NAME, false);
    prefs.putUInt(SPORT_KEY_SAVED, sportSteps);
    prefs.putUInt(SPORT_KEY_CHIP, chip);
    prefs.putUInt(SPORT_KEY_DAY, today);
    prefs.end();
    persistBase = sportSteps;
    persistChip = chip;
    persistDay  = today;
}
