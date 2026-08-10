#ifndef WEATHER_MODULE_H
#define WEATHER_MODULE_H

#include <Arduino.h>
#include <TFT_eSPI.h>

/*
 * 天气模块
 * 数据来源：和风天气 (QWeather)
 * 自动定位：通过 IP 地理定位接口
 * 缓存策略：每 30 分钟刷新一次
 */

// 单日预报数据结构
struct WeatherForecast {
    String date;        // 日期，如 "07-31"
    String textDay;     // 天气描述，如 "晴"
    int    iconDay;     // 天气图标代码
    float  tempMax;     // 最高温度
    float  tempMin;     // 最低温度
};

// 天气数据结构
struct WeatherData {
    // 当前天气
    float  temp;          // 当前温度
    float  feelsLike;     // 体感温度
    int    humidity;      // 湿度百分比
    String weatherText;   // 天气描述
    int    weatherIcon;   // 天气图标代码
    String windDir;       // 风向
    String windSpeed;     // 风速
    // 3天预报
    WeatherForecast forecast[3];
    // 城市信息
    String cityName;      // 城市名称
    float  lat, lon;      // 经纬度
    // 状态
    bool   valid;         // 数据是否有效
    unsigned long updateTime; // 上次成功更新时间 (millis())
};

/* ---- 接口函数 ---- */

// 初始化天气模块
void weather_init();

// 获取天气数据（从和风天气 API）
// 自动完成：IP定位 → 当前天气 → 3天预报
bool weather_fetch();

// 绘制天气页面（在 TFT 上渲染）
void weather_drawPage(TFT_eSPI* tft);

// 检查是否需要刷新（距上次更新 > 30分钟）
bool weather_needRefresh();

// 获取天气数据引用
WeatherData* weather_getData();

/* ---- 天气图标绘制 ---- */
// 根据和风天气 icon 代码绘制天气符号
void weather_drawIcon(TFT_eSPI* tft, int iconCode, int x, int y, int size);

#endif
