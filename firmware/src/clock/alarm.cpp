#include "alarm.h"
#include <Preferences.h>
#include <time.h>

/* ==========================================================
 * 闹钟模块实现
 * - 6 个闹钟槽位，NVS 持久化（Preferences "clock" ns）
 * - 列表页支持纵向滚动（scrollOffset），只显示已创建闹钟
 * - 底部 [+添加闹钟] 按钮：点击进入新增（最多 6 个）
 * - 设置页：小时/分钟 +/-、星期多选、仅一次、保存/删除/取消
 * ========================================================== */

// 闹钟槽位数组（configured 标记是否已创建，空槽跳过）
static AlarmItem alarms[ALARM_MAX];

// 设置页状态
static bool editing = false;
static int  editIndex = 0;          // -1 = 新增
// 编辑副本：编辑期间只改副本，点保存才写回槽位
static AlarmItem editCopy;

// 触发去重：只检测分钟变化
static int lastCheckedMin = -1;

// ==================== 基础 ====================

// 作用：加载闹钟列表（NVS）
void alarm_init() {
    memset(alarms, 0, sizeof(alarms));
    Preferences prefs;
    if (prefs.begin("clock", true)) {      // 命名空间不存在也继续
        prefs.getBytes("alarms", (void*)alarms, sizeof(alarms));
        prefs.end();
    }
    // 数据校验：旧结构/损坏数据可能导致非法值，清空该槽位
    for (int i = 0; i < ALARM_MAX; i++) {
        if (alarms[i].configured && (alarms[i].hour > 23 || alarms[i].minute > 59)) {
            memset(&alarms[i], 0, sizeof(AlarmItem));
        }
    }
    lastCheckedMin = -1;
}

// 作用：保存闹钟列表
void alarm_save() {
    Preferences prefs;
    if (prefs.begin("clock", false)) {
        prefs.putBytes("alarms", (void*)alarms, sizeof(alarms));
        prefs.end();
    }
}

// 作用：取第 index 个闹钟
AlarmItem* alarm_get(int index) {
    if (index < 0 || index >= ALARM_MAX) return NULL;
    return &alarms[index];
}

// 作用：闹钟总数
int alarm_visibleCount() {
    int n = 0;
    for (int i = 0; i < ALARM_MAX; i++) if (alarms[i].configured) n++;
    return n;
}

// ==================== 触发检测 ====================

// 作用：检查是否有闹钟到点（返回触发下标）
int alarm_checkTrigger() {
    // 取当前时间戳
    time_t now_t = time(nullptr);
    // 转成本地时间结构（含星期/时/分）
    struct tm* ti = localtime(&now_t);
    if (!ti || now_t <= 1000000000) return -1;   // NTP 未同步
    if (ti->tm_min == lastCheckedMin) return -1; // 同一分钟不重复触发
    lastCheckedMin = ti->tm_min;

    int wday = (ti->tm_wday + 6) % 7;  // tm_wday: 0=周日 → 0=周一 ... 6=周日
    // 遍历全部闹钟：空槽/停用/时间不匹配都跳过
    for (int i = 0; i < ALARM_MAX; i++) {
        if (!alarms[i].configured) continue;     // 空槽位跳过
        if (!alarms[i].enabled) continue;
        if (alarms[i].hour != ti->tm_hour) continue;
        if (alarms[i].minute != ti->tm_min) continue;
        if (alarms[i].once) {
            // 仅一次：触发后自动关闭
            alarms[i].enabled = false;
            alarm_save();
            return i;
        }
        if (alarms[i].days[wday]) return i;
    }
    return -1;
}

// ==================== 重复描述 ====================

// 作用：闹钟重复规则文字
static String alarmRepeatText(const AlarmItem* a) {
    if (a->once) return String("仅一次");
    // 统计勾选的天数，用于识别"每天/工作日/周末"
    int cnt = 0;
    for (int d = 0; d < 7; d++) if (a->days[d]) cnt++;
    if (cnt == 7) return String("每天");
    if (cnt == 5 && a->days[0] && a->days[1] && a->days[2] && a->days[3] && a->days[4])
        return String("工作日");
    if (cnt == 2 && a->days[5] && a->days[6]) return String("周末");
    String s = "";
    const char* dn[] = {"一","二","三","四","五","六","日"};
    for (int d = 0; d < 7; d++) if (a->days[d]) s += dn[d];
    return s;
}

// ==================== 列表页 ====================

// draw one alarm item card (y = item top; card body starts at y+1)
// 作用：绘制一条闹钟
static void drawAlarmItem(TFT_eSPI* tft, const AlarmItem* a, int y) {
    uint16_t bg = a->enabled ? 0x39E7 : 0x2104;   // enabled light grey / disabled dark grey
    tft->fillRoundRect(4, y + 1, 232, ALARM_LIST_H - 2, 6, bg);
    tft->drawRoundRect(4, y + 1, 232, ALARM_LIST_H - 2, 6, a->enabled ? TFT_CYAN : 0x4208);

    // time (large font 4)
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", a->hour, a->minute);
    tft->setTextColor(a->enabled ? TFT_WHITE : 0x7BEF, bg);
    tft->drawString(buf, 14, y + 1, 4);

    // repeat text (right of time, same row)
    String rep = alarmRepeatText(a);
    tft->setTextColor(a->enabled ? TFT_CYAN : 0x7BEF, bg);
    tft->drawString(rep, 104, y + 5, 2);

    // ON/OFF switch (right side)
    tft->setTextColor(a->enabled ? TFT_GREEN : 0x7BEF, bg);
    tft->drawString(a->enabled ? "ON" : "OFF", 192, y + 8, 2);
}

// bottom [+ add alarm] button
// 作用：绘制「添加闹钟」按钮
static void drawAlarmAddButton(TFT_eSPI* tft) {
    tft->fillRoundRect(4, ALARM_ADD_Y, 232, ALARM_ADD_H, 6, 0x39E7);
    tft->drawRoundRect(4, ALARM_ADD_Y, 232, ALARM_ADD_H, 6, TFT_CYAN);
    tft->setTextColor(TFT_BLACK, 0x39E7);
    tft->drawString("+ 添加闹钟", 66, ALARM_ADD_Y + 4, 2);   // "+ 添加闹钟"
}

// previous frame scroll state (for incremental redraw, avoids whole-block black flash)
static int  prevScrollOffset = -1;          // -1 = no previous frame yet
static int  prevVisibleY[ALARM_MAX];
static int  prevVisibleSlot[ALARM_MAX];
static int  prevVisibleCount = 0;

// 作用：绘制闹钟列表
void alarm_drawList(TFT_eSPI* tft, int scrollOffset) {
    // clear list area (from below tab bar to screen bottom)
    tft->fillRect(0, ALARM_LIST_Y0 - 2, 240, 240 - (ALARM_LIST_Y0 - 2), TFT_BLACK);

    // draw only created alarms (skip empty slots)
    int shown = 0;
    int vis = alarm_visibleCount();
    for (int i = 0; i < ALARM_MAX && shown < vis; i++) {
        if (!alarms[i].configured) continue;
        int y = ALARM_LIST_Y0 + shown * ALARM_LIST_H - scrollOffset;
        shown++;
        if (y + ALARM_LIST_H < ALARM_LIST_Y0 || y > ALARM_ADD_Y) continue;  // clip (keep button clear)
        drawAlarmItem(tft, &alarms[i], y);
    }
    drawAlarmAddButton(tft);

    // remember this frame for the next incremental scroll
    prevScrollOffset = scrollOffset;
    prevVisibleCount = 0;
    shown = 0;
    for (int i = 0; i < ALARM_MAX && shown < vis; i++) {
        if (!alarms[i].configured) continue;
        int y = ALARM_LIST_Y0 + shown * ALARM_LIST_H - scrollOffset;
        shown++;
        if (y + ALARM_LIST_H < ALARM_LIST_Y0 || y > ALARM_ADD_Y) continue;
        prevVisibleY[prevVisibleCount] = y;
        prevVisibleSlot[prevVisibleCount] = i;
        prevVisibleCount++;
    }
}

// incremental scroll redraw: only erase the thin strips revealed by the move,
// then redraw items (opaque cards overwrite the rest) -> no whole-block black flash
// 作用：列表增量绘制（滚动时只画新露出的一行）
void alarm_drawListIncremental(TFT_eSPI* tft, int scrollOffset, int delta) {
    if (delta == 0) return;
    // large jump or invalid state -> fall back to full redraw
    if (prevScrollOffset < 0 || abs(delta) >= ALARM_LIST_H) {
        alarm_drawList(tft, scrollOffset);
        return;
    }

    const int LIST_TOP = ALARM_LIST_Y0 - 2;   // top of list area (black bg)
    const int CELL_H   = ALARM_LIST_H - 2;    // card height (starts at y+1)
    const int ad = abs(delta);

    // current visible items (slot + y)
    int newSlot[ALARM_MAX], newY[ALARM_MAX], newCount = 0;
    {
        int shown = 0;
        int vis = alarm_visibleCount();
        for (int i = 0; i < ALARM_MAX && shown < vis; i++) {
            if (!alarms[i].configured) continue;
            int y = ALARM_LIST_Y0 + shown * ALARM_LIST_H - scrollOffset;
            shown++;
            if (y + ALARM_LIST_H < ALARM_LIST_Y0 || y > ALARM_ADD_Y) continue;
            newSlot[newCount] = i;
            newY[newCount] = y;
            newCount++;
        }
    }

    // erase the thin strips revealed by old items moving
    for (int k = 0; k < prevVisibleCount; k++) {
        bool still = false;
        for (int j = 0; j < newCount; j++) if (newSlot[j] == prevVisibleSlot[k]) { still = true; break; }
        int oy = prevVisibleY[k];
        int y0, y1;
        if (!still) {
            // scrolled out of view: erase whole card
            y0 = oy + 1; y1 = oy + 1 + CELL_H;
        } else if (delta > 0) {
            // moved up: old card bottom ad rows are revealed
            y0 = oy + 1 + CELL_H - ad; y1 = oy + 1 + CELL_H;
        } else {
            // moved down: old card top ad rows are revealed
            y0 = oy + 1; y1 = oy + 1 + ad;
        }
        if (y0 < LIST_TOP) y0 = LIST_TOP;
        if (y1 > ALARM_ADD_Y) y1 = ALARM_ADD_Y;
        if (y1 > y0) tft->fillRect(4, y0, 232, y1 - y0, TFT_BLACK);
    }

    // overall revealed black band at top (moving up) or bottom (moving down)
    if (delta > 0) tft->fillRect(0, LIST_TOP, 240, ad, TFT_BLACK);
    else           tft->fillRect(0, ALARM_ADD_Y - ad, 240, ad, TFT_BLACK);

    // redraw current visible items (opaque cards overwrite leftovers)
    for (int k = 0; k < newCount; k++) drawAlarmItem(tft, &alarms[newSlot[k]], newY[k]);
    drawAlarmAddButton(tft);

    // update previous frame state
    prevScrollOffset = scrollOffset;
    prevVisibleCount = newCount;
    for (int k = 0; k < newCount; k++) { prevVisibleY[k] = newY[k]; prevVisibleSlot[k] = newSlot[k]; }
}

// 列表页点击命中
// 作用：闹钟列表点击处理（编辑/删除/开关）
bool alarm_handleListTap(uint16_t x, uint16_t y, int scrollOffset) {
    // 底部 [+添加闹钟] 按钮 → 新增
    if (y >= ALARM_ADD_Y && y < ALARM_ADD_Y + ALARM_ADD_H) {
        editing = true;
        editIndex = -1;
        memset(&editCopy, 0, sizeof(editCopy));
        editCopy.enabled = true;
        editCopy.configured = true;   // 保存时写入槽位
        return true;
    }
    if (y < ALARM_LIST_Y0 - 2 || y >= ALARM_ADD_Y) return false;

    int vis = alarm_visibleCount();
    int idx = (y - (ALARM_LIST_Y0 - 2) + scrollOffset) / ALARM_LIST_H;
    if (idx < 0 || idx >= vis) return false;

    // 找到第 idx 个已创建的槽位
    int slot = -1, cnt = -1;
    for (int k = 0; k < ALARM_MAX; k++) {
        if (alarms[k].configured) { cnt++; if (cnt == idx) { slot = k; break; } }
    }
    if (slot < 0) return false;

    // ON/OFF 区域（右侧 x>180）→ 切换启用状态
    if (x > 180) {
        alarms[slot].enabled = !alarms[slot].enabled;
        alarm_save();
        return true;
    }
    // 点击项主体 → 编辑
    editing = true;
    editIndex = slot;
    editCopy = alarms[slot];
    return true;
}

// ==================== 设置页 ====================

bool alarm_isEditing() { return editing; }
int  alarm_editingIndex() { return editIndex; }

// 编辑页按钮区域常量
#define ED_BTN_Y1 106
#define ED_BTN_Y2 140
#define ED_BTN_W  96
#define ED_BTN_H  28
#define ED_WEEK_Y 172
#define ED_WEEK_W 30
#define ED_WEEK_H 30
#define ED_ACT_Y  210

// 作用：绘制编辑闹钟界面
void alarm_drawEdit(TFT_eSPI* tft, int index) {
    tft->fillScreen(TFT_BLACK);
    AlarmItem* a = &editCopy;

    // 标题
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->drawString(index < 0 ? "添加闹钟" : "编辑闹钟", 70, 6, 2);

    // 大时间
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", a->hour, a->minute);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->drawString(buf, 50, 40, 7);

    // 小时/分钟 调整按钮
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->drawString("小时", 24, 84, 1);
    tft->drawString("分钟", 140, 84, 1);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->fillRoundRect(16, ED_BTN_Y1, ED_BTN_W, ED_BTN_H, 6, TFT_DARKGREY);
    tft->drawString("[-]", 44, ED_BTN_Y1 + 5, 2);
    tft->fillRoundRect(16, ED_BTN_Y2, ED_BTN_W, ED_BTN_H, 6, TFT_DARKGREY);
    tft->drawString("[+]", 44, ED_BTN_Y2 + 5, 2);
    tft->fillRoundRect(128, ED_BTN_Y1, ED_BTN_W, ED_BTN_H, 6, TFT_DARKGREY);
    tft->drawString("[-]", 156, ED_BTN_Y1 + 5, 2);
    tft->fillRoundRect(128, ED_BTN_Y2, ED_BTN_W, ED_BTN_H, 6, TFT_DARKGREY);
    tft->drawString("[+]", 156, ED_BTN_Y2 + 5, 2);

    // 星期选择
    const char* dn[] = {"一","二","三","四","五","六","日"};
    for (int d = 0; d < 7; d++) {
        int x = 12 + d * (ED_WEEK_W + 3);
        uint16_t bg = a->days[d] ? TFT_CYAN : TFT_DARKGREY;
        uint16_t fg = a->days[d] ? TFT_BLACK : TFT_WHITE;
        tft->fillRoundRect(x, ED_WEEK_Y, ED_WEEK_W, ED_WEEK_H, 4, bg);
        tft->setTextColor(fg, bg);
        tft->drawString(dn[d], x + 8, ED_WEEK_Y + 4, 2);
    }

    // 操作按钮：保存 / 取消 / 删除(编辑已有时)
    int bx = 12;
    if (index >= 0) {
        tft->fillRoundRect(bx, ED_ACT_Y, 64, 30, 6, TFT_RED);
        tft->setTextColor(TFT_WHITE, TFT_RED);
        tft->drawString("删除", bx + 12, ED_ACT_Y + 5, 2);
        bx += 72;
    }
    tft->fillRoundRect(bx, ED_ACT_Y, 64, 30, 6, TFT_GREEN);
    tft->setTextColor(TFT_BLACK, TFT_GREEN);
    tft->drawString("保存", bx + 12, ED_ACT_Y + 5, 2);
    bx += 72;
    tft->fillRoundRect(bx, ED_ACT_Y, 64, 30, 6, TFT_DARKGREY);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->drawString("取消", bx + 12, ED_ACT_Y + 5, 2);
}

// 编辑页点击处理
// 作用：编辑界面点击处理
bool alarm_handleEditTap(uint16_t x, uint16_t y) {
    if (!editing) return false;
    AlarmItem* a = &editCopy;

    // 小时/分钟 调整
    if (y >= ED_BTN_Y1 && y < ED_BTN_Y1 + ED_BTN_H) {
        if (x >= 16 && x < 16 + ED_BTN_W) { a->hour = (a->hour + 23) % 24; return true; }
        if (x >= 128 && x < 128 + ED_BTN_W) { a->minute = (a->minute + 59) % 60; return true; }
    }
    if (y >= ED_BTN_Y2 && y < ED_BTN_Y2 + ED_BTN_H) {
        if (x >= 16 && x < 16 + ED_BTN_W) { a->hour = (a->hour + 1) % 24; return true; }
        if (x >= 128 && x < 128 + ED_BTN_W) { a->minute = (a->minute + 1) % 60; return true; }
    }

    // 星期选择
    if (y >= ED_WEEK_Y && y < ED_WEEK_Y + ED_WEEK_H) {
        for (int d = 0; d < 7; d++) {
            int wx = 12 + d * (ED_WEEK_W + 3);
            if (x >= wx && x < wx + ED_WEEK_W) {
                a->days[d] = !a->days[d];
                Serial.printf("[ALARM] week tap x=%d y=%d d=%d days=%d%d%d%d%d%d%d\n", x, y, d, a->days[0],a->days[1],a->days[2],a->days[3],a->days[4],a->days[5],a->days[6]);
                return true;
            }
        }
    }

    // 操作按钮
    if (y >= ED_ACT_Y && y < ED_ACT_Y + 30) {
        int bx = 12;
        if (editIndex >= 0) {
            if (x >= bx && x < bx + 64) {            // 删除
                memset(&alarms[editIndex], 0, sizeof(AlarmItem));   // 清空整个槽位
                alarm_save();
                editing = false;
                return true;
            }
            bx += 72;
        }
        if (x >= bx && x < bx + 64) {                // 保存
            a->once = true;
            for (int d = 0; d < 7; d++) if (a->days[d]) { a->once = false; break; }
            if (editIndex < 0) {
                // 新增：找第一个空槽位（configured=false）
                for (int i = 0; i < ALARM_MAX; i++) {
                    if (!alarms[i].configured) { alarms[i] = *a; alarms[i].configured = true; break; }
                }
            } else {
                alarms[editIndex] = *a;
            }
            alarm_save();
            editing = false;
            return true;
        }
        bx += 72;
        if (x >= bx && x < bx + 64) { editing = false; return true; }   // 取消
    }
    return false;
}