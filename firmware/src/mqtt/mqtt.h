/*
 * MQTT 模块：与 ThingsCloud 云平台通信（上报传感器数据 / 接收下发指令）
 */

#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

/* ==========================================================
 * MQTT 模块 — ThingsCloud 云平台通信
 * ==========================================================
 * 功能：
 *   1. 连接 ThingsCloud MQTT 服务器（1883 端口）
 *   2. 每 40 秒上报完整传感器数据
 *   3. 接收云端下发的指令（震动提醒等）
 * ========================================================== */

// MQTT 服务器配置（ThingsCloud）
extern const char* MQTT_HOST;
extern const int   MQTT_PORT;
extern const char* MQTT_CLIENT_ID;
extern const char* MQTT_USER;
extern const char* MQTT_PASS;

// MQTT 客户端对象（定义在 main.cpp）
extern WiFiClient wifiClient;
extern PubSubClient mqtt;

// MQTT 状态
extern int  mqttLastRc;       // 上次连接错误码
extern bool mqttOk;           // MQTT 是否在线
extern IPAddress mqttIP;      // 服务器 IP
extern bool mqttIPResolved;   // IP 是否已解析

// MQTT 消息回调（收到服务器下发的消息）
void mqttCallback(char* topic, byte* payload, unsigned int len);

// 定时向 MQTT 上传完整传感器数据
void publishSensorData();

// 连接到 MQTT 服务器（非阻塞 + 超时控制）
bool connectMQTT();

#endif
