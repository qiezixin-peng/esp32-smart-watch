#ifndef NOTIFY_MODULE_H
#define NOTIFY_MODULE_H
/* ==========================================================
 * 通知模块 — 接收 MQTT 下发的手机通知（微信/QQ/短信）
 * ==========================================================
 * 功能：
 *   1. 解析 MQTT JSON 通知 {"src":"微信","from":"张三","text":"你好"}
 *   2. RAM 环形队列保留最近 20 条（重启清空）
 *   3. 未读数统计 + 新通知标志（息屏时亮屏 3 秒用）
 * ========================================================== */
#include <Arduino.h>

#define NOTIFY_MAX      20          // 最多保留条数
#define NOTIFY_SRC_LEN  12          // 来源名（微信/QQ/短信）
#define NOTIFY_FROM_LEN 20          // 发件人
#define NOTIFY_TEXT_LEN 48          // 内容（UTF-8 前 15 个汉字，45 字节 + 结尾符）

typedef struct {
    char src[NOTIFY_SRC_LEN];
    char from[NOTIFY_FROM_LEN];
    char text[NOTIFY_TEXT_LEN];
    uint32_t ts;                    // 收到时间（NTP 秒）
} NotifyItem;

void notifyInit();                                        // 初始化（清空队列）
void notifyAdd(const char* src, const char* from, const char* text);  // 入队 + 未读 +1 + 置新通知标志
int  notifyCount();                                       // 当前条数
int  notifyUnread();                                      // 未读条数
void notifyMarkRead();                                    // 全部标记已读（进入列表页时）
void notifyClear();                                       // 清空
bool notifyGet(int index, NotifyItem* out);               // index 0 = 最新一条
bool notifyHasNew();                                      // 是否有新通知（息屏亮屏用）
bool notifyTakeNew();                                     // 消费新通知标志
void notifyParse(const char* json);                       // 解析 MQTT JSON → 入队

#endif
