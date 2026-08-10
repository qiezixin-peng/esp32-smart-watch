#include "notify.h"
#include <ArduinoJson.h>
#include <time.h>

/* ==========================================================
 * 通知模块 — 实现
 * ==========================================================
 * 队列规则：最新在前（index 0 = 最新），满 20 条丢弃最旧
 * 文本规则：内容只保留前 15 个 UTF-8 字符（用户要求），
 *           发件人按长度截断（避免任意长文本占满 RAM）
 * ========================================================== */

// 通知队列（index 0 = 最新，满员丢最旧）
static NotifyItem _items[NOTIFY_MAX];
static int  _count = 0;      // 当前条数
static int  _unread = 0;     // 未读数
static bool _hasNew = false; // 新通知标志（息屏亮屏用）

/* 将 UTF-8 字符串安全截断为最多 maxChars 个字符（不截断多字节字符） */
// 作用：按字符数截断 UTF-8 字符串（避免截半个字变乱码）
static void utf8Truncate(const char* in, char* out, size_t outSize, int maxChars) {
    size_t o = 0;
    int n = 0;
    while (*in && o + 1 < outSize && n < maxChars) {
        unsigned char c = (unsigned char)*in;
        int len = 1;
        if      (c >= 0xF0) len = 4;   // 4 字节（表情等）
        else if (c >= 0xE0) len = 3;   // 3 字节（汉字）
        else if (c >= 0xC0) len = 2;   // 2 字节
        if (o + len + 1 > outSize) break;  // 放不下完整字符则停止
        for (int i = 0; i < len && *in; i++) out[o++] = *in++;
        n++;
    }
    out[o] = 0;
}

// 作用：初始化通知列表
void notifyInit() {
    _count = 0;
    _unread = 0;
    _hasNew = false;
}

// 作用：新增一条通知
void notifyAdd(const char* src, const char* from, const char* text) {
    if (!src) src = "";
    if (!from) from = "";
    if (!text) text = "";
    // 后移腾出最新位置（丢弃最旧）
    if (_count >= NOTIFY_MAX) {
        memmove(&_items[1], &_items[0], (NOTIFY_MAX - 1) * sizeof(NotifyItem));
        _count = NOTIFY_MAX - 1;
    } else if (_count > 0) {
        memmove(&_items[1], &_items[0], _count * sizeof(NotifyItem));
    }
    // 写入最新一条
    NotifyItem* it = &_items[0];
    memset(it, 0, sizeof(NotifyItem));
    strncpy(it->src, src, NOTIFY_SRC_LEN - 1);
    strncpy(it->from, from, NOTIFY_FROM_LEN - 1);
    utf8Truncate(text, it->text, NOTIFY_TEXT_LEN, 15);   // 前 15 字
    it->ts = (uint32_t)time(nullptr);
    if (_count < NOTIFY_MAX) _count++;
    if (_unread < 99) _unread++;
    _hasNew = true;
    Serial.printf("[NOTIFY] +1 from=%s text=%s unread=%d\n", it->from, it->text, _unread);
}

int notifyCount() { return _count; }
int notifyUnread() { return _unread; }

void notifyMarkRead() { _unread = 0; }

// 作用：清空全部通知
void notifyClear() {
    _count = 0;
    _unread = 0;
}

// 作用：取第 index 条通知
bool notifyGet(int index, NotifyItem* out) {
    if (index < 0 || index >= _count || !out) return false;
    memcpy(out, &_items[index], sizeof(NotifyItem));
    return true;
}

bool notifyHasNew() { return _hasNew; }
bool notifyTakeNew() { bool v = _hasNew; _hasNew = false; return v; }

/* 解析 MQTT 下行 JSON：{"src":"微信","from":"张三","text":"你好"} */
// 作用：解析 MQTT 下发的通知 JSON
void notifyParse(const char* json) {
    if (!json || !*json) return;
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        Serial.println("[NOTIFY] JSON parse fail");
        return;
    }
    const char* src  = doc["src"]  | "";
    const char* from = doc["from"] | "";
    const char* text = doc["text"] | "";
    notifyAdd(src, from, text);
}
