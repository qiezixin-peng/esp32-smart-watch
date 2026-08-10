/*
 * UI 主界面模块实现：主页面磁贴（表盘/运动/天气/时钟/通知…）、页面切换调度、
 * 各页面进入/退出刷新
 */
// ============ 模块职责 ============
// 本文件负责手表所有"应用页"界面的绘制与触摸分发：
//  - handleTouch：按当前页面把点击坐标交给对应页面处理
//  - drawPageXXX：各页面整页绘制（主页/健康/运动/天气/时钟/通知/设备/表盘选择/下拉面板）
//  - refreshPageXXX：局部刷新（只重绘变化区域，避免整屏闪烁）
//  - drawMainScreen：按 currentPage 调度绘制入口
// 屏幕坐标系：240×240，y 向下增大；状态栏在顶部 0~17 行

#include "ui.h"
#include "fonts/font_vlw.h"
#include "../power/power.h"
#include "../touch/touch.h"
#include "../watchface/watchface_manager.h"
#include "../weather/weather.h"
#include "../clock/clock.h"
#include "../sport/sport.h"
#include "../notify/notify.h"
#include "../wallpaper/wallpaper.h"
#include <WiFi.h>

extern int mqttLastRc;

// ===== 主页磁贴布局常量 =====
// 主页 6 个磁贴排成 3 行 2 列：每格 106×52，间距 10，圆角 8
// 磁贴布局常量
#define TILE_W      106
#define TILE_H      52
#define TILE_GAP    10
#define TILE_R      8
#define TILE_START_X 8
#define ROW1_Y      24
#define ROW2_Y      86
#define ROW3_Y      148

// ===== 触摸坐标数据（用于手势检测）=====
static uint16_t touchStartX = 0;
static uint16_t touchStartY = 0;
static bool touchDown = false;
static unsigned long touchDownTime = 0;

// ===== 触摸分发入口（main.cpp 单击手势最终调用这里）=====
// 作用：全局触摸分发：按当前页面交给对应页面处理
bool handleTouch(uint16_t x, uint16_t y) {
    // 表盘/背景图板块：顶部导航栏点击切换 tab
    // y<28 是顶部导航栏区域：左半边=表盘 tab，右半边=背景图 tab
    if (currentPage == PAGE_WFSEL && y < 28) {
        wfSetTab(x < 120 ? 0 : 1);
        drawMainScreen();
        return true;
    }
    // 表盘选择页：触摸用于切换表盘
    if (currentPage == PAGE_WFSEL) {
        // 记录触摸起点
    // 按下瞬间记录起点坐标与时间：后续 getGesture 用它计算滑动手势（滑动距离/方向/时长）
        if (!touchDown) {
            touchDown = true;
            touchStartX = x;
            touchStartY = y;
            touchDownTime = millis();
        }
        return true;
    }

    // 时钟板块：点击分发到时钟子页/编辑页
    // 时钟板块自身管理点击（秒表启停/计时器/闹钟编辑），这里直接转发
    if (currentPage == PAGE_CLOCK) return clock_handleTap(x, y);

    // 通知板块：点击清空按钮清空列表
    // 右下角"清空"按钮区域：命中后清空全部通知并重绘
    if (currentPage == PAGE_NOTIFY) {
        if (x >= 160 && x <= 240 && y >= 198 && y <= 240) {  // 命中区略大于按钮，包容手指微动
            notifyClear();
            drawMainScreen();
            return true;
        }
        return true;
    }

    // 非主页页面：这里不处理，返回 false（由上层忽略）
    // 主页面：点击磁贴跳转
    if (currentPage != PAGE_HOME) return false;
    struct { int x, y, w, h; } t[6] = {
        {TILE_START_X,                       ROW1_Y, TILE_W, TILE_H},
        {TILE_START_X + TILE_W + TILE_GAP,  ROW1_Y, TILE_W, TILE_H},
        {TILE_START_X,                       ROW2_Y, TILE_W, TILE_H},
        {TILE_START_X + TILE_W + TILE_GAP,  ROW2_Y, TILE_W, TILE_H},
        {TILE_START_X,                       ROW3_Y, TILE_W, TILE_H},
        {TILE_START_X + TILE_W + TILE_GAP,  ROW3_Y, TILE_W, TILE_H},
    };
    // 6 个磁贴矩形：3 行 2 列（第1列 x=8，第2列 x=124）
    for (int i = 0; i < 6; i++) {
        if (x >= t[i].x && x < t[i].x + t[i].w && y >= t[i].y && y < t[i].y + t[i].h) {
            switch (i) {
                case 0: currentPage = PAGE_CLOCK;  drawMainScreen(); return true;  // 时钟板块
                case 1: currentPage = PAGE_HEALTH; sensorRxCount = 0; drawMainScreen(); return true;
                // 进健康页时清空 RX 计数，方便用户观察"本次进入后收到几条数据"
                case 2: currentPage = PAGE_WFSEL;   drawMainScreen(); return true;  // 表盘板块
                case 3: currentPage = PAGE_WEATHER; if (weather_needRefresh()) weather_fetch(); drawMainScreen(); return true;
                // 进天气页：若到刷新间隔先拉一次最新天气，再显示页面
                case 4: currentPage = PAGE_SPORT;  drawMainScreen(); return true;  // 运动板块
                case 5: currentPage = PAGE_DEVICE;  drawMainScreen(); return true;
                default: return false;
            }
        }
    }
    // 底部通知栏（x 8~232, y 204~238）：进入通知列表页并标记已读
    // 主页底部常驻通知栏：点击进入通知列表，同时把所有通知标记为已读
    if (x >= 8 && x <= 232 && y >= 204 && y <= 238) {
        currentPage = PAGE_NOTIFY;
        notifyMarkRead();
        drawMainScreen();
        return true;
    }
    return false;
}

// 手势检测：左右滑
// 在 loop 中调用，返回 -1=左滑, 0=无手势, 1=右滑


// ===== 通知板块页面 =====
// 作用：绘制通知列表页
void drawPageNotify() {
    // 通知列表页：标题 + 最多 4 条通知 + 底部"共N条/清空"
    tft.fillScreen(TFT_BLACK);   // 直接刷新时确保清屏，无残留
    char buf[40];
    // 标题
    tft.setTextSize(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("\u901a\u77e5", 95, 24);
    int n = notifyCount();
    if (n == 0) {
        // 没有通知：直接显示"暂无通知"占位文字
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString("\u6682\u65e0\u901a\u77e5", 90, 110);
    }
    // 列表：一屏最多 4 条（每条 34px：来源+发件人 / 内容前 15 字）
    // 每条通知两行：第一行"来源 发件人"，第二行内容摘要；条目间画浅色分隔线
    int y = 52;
    for (int i = 0; i < n && i < 4; i++) {
        NotifyItem it;
        if (!notifyGet(i, &it)) break;
        if (i > 0) tft.drawLine(10, y - 2, 230, y - 2, 0x18E3);
        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString(it.src, 12, y);
        int x1 = 12 + tft.textWidth(it.src) + 8;
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(it.from, x1, y);
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString(it.text, 12, y + 18);
        y += 34;
    }
    // 底部：共 N 条 + 清空按钮
    // 底部信息区：左侧显示通知总数，右侧红色"清空"按钮（点击区域见 handleTouch）
    tft.drawLine(10, 194, 230, 194, 0x18E3);
    snprintf(buf, sizeof(buf), "\u5171 %d \u6761", n);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(buf, 12, 208);
    tft.fillRoundRect(176, 204, 54, 26, 6, 0x18E3);
    tft.drawRoundRect(176, 204, 54, 26, 6, TFT_RED);
    tft.setTextColor(TFT_RED, 0x18E3);
    tft.drawString("\u6e05\u7a7a", 176 + (54 - tft.textWidth("\u6e05\u7a7a")) / 2, 204 + (26 - tft.fontHeight()) / 2);
}

// ===== 运动板块页面 =====
// 作用：绘制运动页（步数/距离/卡路里）
void drawPageSport() {
    // 运动页：步数大数字 + 目标进度条 + 状态 + 距离/卡路里
    char buf[32];
    // 标题
    tft.setTextSize(2); tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("运动", 95, 24);

    // 步数大数字（内置 font7）
    // 步数用 7 号大字体绘制：先卸载 vlw（避免大字体冲突），画完再恢复
    tft.unloadFont();
    tft.setTextSize(1);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)sportSteps);
    int tw7 = tft.textWidth(buf, 7);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, (240 - tw7) / 2, 58, 7);
    tft.loadFont(font_vlw);
    // 单位"步"（vlw 中文；必须在 loadFont 之后，否则内置 font2 显示为方框）
    // "步"是中文，内置字体没有中文字形，必须用 vlw 字体画
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("步", (240 - tft.textWidth("步", 2)) / 2, 110, 2);

    // 目标进度条（不依赖字体）
    // 进度条：外框深灰 + 底槽 + 绿色填充（步数/目标 百分比）；目标为 0 时进度为 0 并截断到 100%
    const int barX = 24, barW = 192, barH = 10, barY = 128;
    float pct = sportStepTarget > 0 ? (float)sportSteps * 100.0f / sportStepTarget : 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    tft.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
    tft.fillRect(barX, barY, barW, barH, 0x10A2);
    if (pct > 0.0f) tft.fillRect(barX, barY, (int)(barW * pct / 100.0f), barH, TFT_GREEN);

    // 百分比 + 目标
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.1f%%", pct);
    tft.drawString(buf, 20, 146, 2);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(buf, sizeof(buf), "目标 %d 步", sportStepTarget);
    tft.drawString(buf, 240 - tft.textWidth(buf, 2) - 16, 146, 2);

    // 运动状态
    // 当前活动识别结果：静止/走路/跑步等（来自 BMA423 活动识别）
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "状态 %s", sport_activityName());
    tft.drawString(buf, 30, 176, 2);

    // 距离 / 卡路里
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "距离 %.1f m", sportDistanceKm * 1000.0f);
    tft.drawString(buf, 30, 204, 2);
    snprintf(buf, sizeof(buf), "卡路里 %.0f kcal", sportCalories);
    tft.drawString(buf, 30, 222, 2);
}

// 运动页局部刷新：只重绘数值变化区域，避免全屏闪烁
// 作用：运动页局部刷新（只更新变化的数字，不整屏重绘）
void refreshPageSport() {
    // 运动页局部刷新：只清掉数值区域重画，边框和静态文字不动（避免整屏闪烁）
    char buf[32];
    // 步数大数字
    tft.unloadFont();
    tft.setTextSize(1);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)sportSteps);
    int tw7 = tft.textWidth(buf, 7);
    tft.fillRect(20, 58, 200, 60, TFT_BLACK);   // 清步数区
    // 先清掉旧步数，再画新值（跟随主循环 sportDirty 触发，数据变化才调用）
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, (240 - tw7) / 2, 58, 7);
    tft.loadFont(font_vlw);

    // 进度条 + 百分比
    const int barX = 24, barW = 192, barH = 10, barY = 128;
    float pct = sportStepTarget > 0 ? (float)sportSteps * 100.0f / sportStepTarget : 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    tft.fillRect(barX, barY, barW, barH, 0x10A2);
    if (pct > 0.0f) tft.fillRect(barX, barY, (int)(barW * pct / 100.0f), barH, TFT_GREEN);
    tft.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
    tft.fillRect(16, 146, 208, 18, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.1f%%", pct);
    tft.drawString(buf, 20, 146, 2);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(buf, sizeof(buf), "目标 %d 步", sportStepTarget);
    tft.drawString(buf, 240 - tft.textWidth(buf, 2) - 16, 146, 2);

    // 状态 / 距离 / 卡路里
    tft.fillRect(16, 176, 208, 18, TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "状态 %s", sport_activityName());
    tft.drawString(buf, 30, 176, 2);

    tft.fillRect(16, 204, 208, 18, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "距离 %.1f m", sportDistanceKm * 1000.0f);
    tft.drawString(buf, 30, 204, 2);

    tft.fillRect(16, 222, 208, 18, TFT_BLACK);
    snprintf(buf, sizeof(buf), "卡路里 %.0f kcal", sportCalories);
    tft.drawString(buf, 30, 222, 2);
}
// ===== 页面绘制函数 =====

// 作用：绘制顶部状态栏（时间/WiFi/电量）
void drawStatusBar() {
    // 顶部状态栏：右上角电量，右侧两个状态点(WiFi/BLE)，下方一排页面小圆点，底部横线
    // 强制内置小字体绘制状态栏（vlw 16pt 会使电量文字超高超框）
    // 状态栏内容：右上角电量(+充电/低电量变色) → WiFi/BLE 状态点 → 一排页面指示点 → 底部横线
    tft.unloadFont();
    tft.setTextSize(1);
    char buf[16];
    int bat = getBatteryPercent();
    bool charging = isCharging();
    snprintf(buf, sizeof(buf), "%s%d%%", charging ? "+" : "", bat);
    tft.setTextColor(charging ? TFT_GREEN : (bat < 20 ? TFT_RED : TFT_WHITE), TFT_BLACK);
    tft.drawString(buf, 196, 3, 1);
    tft.fillCircle(180, 8, 3, wifiOk ? TFT_GREEN : TFT_RED);
    tft.fillCircle(188, 8, 3, bleConnected ? TFT_GREEN : TFT_ORANGE);
    for (int p = 0; p < PAGE_COUNT; p++)
        tft.fillCircle(4 + p * 8, 8, 2, (p == currentPage) ? TFT_CYAN : TFT_DARKGREY);
    tft.drawLine(0, 17, 239, 17, TFT_DARKGREY);
    tft.loadFont(font_vlw);   // 恢复 UI 中文字体
}

// 局部刷新：只重绘顶部状态栏区域（避免整屏闪烁）
// 作用：状态栏局部刷新（每分钟更新一次时间）
void refreshStatusBar() {
    // 状态栏局部刷新：先黑条清空顶部 18px，再重画（只有网络/电量变化时才调用，见 main.cpp）
    tft.unloadFont();
    tft.setTextSize(1);
    tft.fillRect(0, 0, 240, 18, TFT_BLACK);
    // 先黑条清空顶部 18px 再重画，避免新旧文字重叠（局部刷新，只在状态变化时调用）
    char buf[16];
    int bat = getBatteryPercent();
    bool charging = isCharging();
    snprintf(buf, sizeof(buf), "%s%d%%", charging ? "+" : "", bat);
    tft.setTextColor(charging ? TFT_GREEN : (bat < 20 ? TFT_RED : TFT_WHITE), TFT_BLACK);
    tft.drawString(buf, 196, 3, 1);
    tft.fillCircle(180, 8, 3, wifiOk ? TFT_GREEN : TFT_RED);
    tft.fillCircle(188, 8, 3, bleConnected ? TFT_GREEN : TFT_ORANGE);
    for (int p = 0; p < PAGE_COUNT; p++)
        tft.fillCircle(4 + p * 8, 8, 2, (p == currentPage) ? TFT_CYAN : TFT_DARKGREY);
    tft.drawLine(0, 17, 239, 17, TFT_DARKGREY);
    tft.loadFont(font_vlw);   // 恢复 UI 中文字体
}

// 健康页局部刷新：只重绘顶部状态行 + 六宫格数值区（不动边框，不全屏清屏）
// 作用：健康页局部刷新（心率/血氧/UV）
void refreshPageHealth() {
    // 健康页局部刷新：只重绘顶部 BLE 状态行 + 六宫格数值区，边框不动
    char buf[32];
    bool hasData = (sensorRxCount > 0);
    // 顶部 BLE/RX 小字
    tft.fillRect(0, 18, 240, 16, TFT_BLACK);
    tft.unloadFont();
    tft.setTextSize(1);
    tft.setTextColor(bleConnected ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "BLE:%s", bleConnected ? "OK" : "NO");
    tft.drawString(buf, 6, 24, 1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(buf, sizeof(buf), "RX:%d", sensorRxCount);
    tft.drawString(buf, 240 - tft.textWidth(buf, 1) - 6, 24, 1);
    tft.loadFont(font_vlw);
    // 六宫格数值区
    // 六宫格 3×2：格子 110×60，起点 (6,36)，间距 8/6；
    // 每格：标签(彩色) + 数值(白/大字) + 单位(灰/小字)，数值有数据按字段显示小数/整数，无数据显示 "--"
    // 六宫格布局：3 行 2 列；每格有标签/数值/单位，数值与单位整体居中
    struct Card { const char* label; const char* unit; float val; uint16_t color; bool frac; };
    Card cards[6] = {
        {"心率", "bpm", sensorBpm,   TFT_RED,     false},
        {"血氧", "%",   sensorSpo2,  TFT_GREEN,   false},
        {"温度", "C",   sensorTemp,  TFT_ORANGE,  true},
        {"湿度", "%",   sensorHum,   TFT_YELLOW,  false},
        {"气压", "hPa", sensorPress, TFT_CYAN,    false},
        {"紫外线", "", sensorUv, TFT_MAGENTA, true},
    };
    const int CW = 110, CH = 60, X0 = 6, Y0 = 36, GX = 8, GY = 6;
    for (int i = 0; i < 6; i++) {
        int cx = X0 + (i % 2) * (CW + GX);
        int cy = Y0 + (i / 2) * (CH + GY);
        uint16_t bg = 0x18E3;
        tft.fillRect(cx + 2, cy + 4, CW - 4, CH - 8, bg);
        tft.setTextSize(1);
        tft.setTextColor(cards[i].color, bg);
        tft.drawString(cards[i].label, cx + 6, cy + 4);
        if (hasData) {
            if (cards[i].frac) snprintf(buf, sizeof(buf), "%.1f", cards[i].val);
            else snprintf(buf, sizeof(buf), "%.0f", cards[i].val);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        int tw4 = tft.textWidth(buf, 4);
        int tw1 = 0;
        if (cards[i].unit[0]) tw1 = tft.textWidth(cards[i].unit, 1) + 4;
        int startX = cx + (CW - (tw4 + tw1)) / 2;
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawString(buf, startX, cy + 20, 4);
        if (cards[i].unit[0]) {
            tft.setTextColor(TFT_DARKGREY, bg);
            tft.drawString(cards[i].unit, startX + tw4 + 4, cy + 24, 1);
        }
    }
}

// 作用：绘制主页磁贴（表盘/运动/天气/时钟…）
void drawPageHome() {
    // 主页磁贴：6 块（时钟/健康/表盘背景图/天气/运动/设备详情）+ 底部通知栏
    struct { const char* n; uint16_t c; int x, y; } tiles[6] = {
        {"时钟", TFT_CYAN,    TILE_START_X,                       ROW1_Y},
        {"健康", TFT_RED,     TILE_START_X + TILE_W + TILE_GAP,  ROW1_Y},
        {"表盘/背景图", TFT_GREEN,   TILE_START_X,                       ROW2_Y},
        {"天气", TFT_ORANGE,  TILE_START_X + TILE_W + TILE_GAP,  ROW2_Y},
        {"运动", TFT_GREEN,   TILE_START_X,                       ROW3_Y},
        {"设备详情", TFT_CYAN, TILE_START_X + TILE_W + TILE_GAP,  ROW3_Y},
    };
        // 6 块磁贴定义：名称/描边颜色/位置（3 行 2 列）
    for (int i = 0; i < 6; i++) {
        int x = tiles[i].x, y = tiles[i].y, w = TILE_W;
        tft.fillRoundRect(x, y, w, TILE_H, TILE_R, TFT_DARKGREY);
        tft.drawRoundRect(x, y, w, TILE_H, TILE_R, tiles[i].c);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
        int tw = tft.textWidth(tiles[i].n);
        tft.drawString(tiles[i].n, x + (w - tw) / 2, y + (TILE_H - 22) / 2);
    }
    // ===== 底部通知栏：入口 + 未读提示 =====
    // 通知栏：左侧"通知"标签，右侧显示未读红字 + 最新一条来源/发件人，无通知显示"暂无通知"
    const int nbx = 8, nby = 204, nbw = 224, nbh = 34;
    const uint16_t nbBG = 0x18E3;
    tft.fillRoundRect(nbx, nby, nbw, nbh, 8, nbBG);
    tft.drawRoundRect(nbx, nby, nbw, nbh, 8, TFT_CYAN);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, nbBG);
    tft.drawString("\u901a\u77e5", nbx + 10, nby + (nbh - 22) / 2);
    int nx = nbx + 10 + tft.textWidth("\u901a\u77e5") + 8;
    int nn = notifyCount();
    if (nn > 0) {
        if (notifyUnread() > 0) {
            char ub[8];
            snprintf(ub, sizeof(ub), "%d", notifyUnread());
            tft.setTextColor(TFT_RED, nbBG);
            tft.drawString(ub, nx, nby + (nbh - 22) / 2);
            nx += tft.textWidth(ub) + 8;
        }
        NotifyItem it;
        if (notifyGet(0, &it)) {
            char sb[40];
            snprintf(sb, sizeof(sb), "%s %s", it.src, it.from);
            tft.setTextColor(TFT_DARKGREY, nbBG);
            tft.drawString(sb, nx, nby + (nbh - 22) / 2);
        }
    } else {
        tft.setTextColor(TFT_DARKGREY, nbBG);
        tft.drawString("\u6682\u65e0\u901a\u77e5", nx, nby + (nbh - 22) / 2);
    }
}

// 作用：绘制健康页（心率/血氧/UV 卡片）
void drawPageHealth() {
    // 健康页整页：顶部 BLE 状态 + RX 计数 + 六宫格数据卡片
    char buf[32];
    bool hasData = (sensorRxCount > 0);

    // 顶部：BLE 状态（左）+ RX 计数（右），用小号内置字体避免被卡片挡住
    // 顶部用内置小字体（不占 vlw 大字体空间），绘制后立即恢复 vlw 供卡片中文用
    tft.unloadFont();
    tft.setTextSize(1);
    tft.setTextColor(bleConnected ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "BLE:%s", bleConnected ? "OK" : "NO");
    tft.drawString(buf, 6, 24, 1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(buf, sizeof(buf), "RX:%d", sensorRxCount);
    tft.drawString(buf, 240 - tft.textWidth(buf, 1) - 6, 24, 1);
    tft.loadFont(font_vlw);

    // 六宫格：心率/血氧/温度/湿度/气压/UV
    // 六宫格 3×2：格子 110×60，起点 (6,36)，间距 8/6；每格卡片圆角边框 + 彩色标签 + 白字数值 + 灰色单位
    struct Card { const char* label; const char* unit; float val; uint16_t color; bool frac; };
    Card cards[6] = {
        {"心率", "bpm", sensorBpm,   TFT_RED,     false},
        {"血氧", "%",   sensorSpo2,  TFT_GREEN,   false},
        {"温度", "C",   sensorTemp,  TFT_ORANGE,  true},
        {"湿度", "%",   sensorHum,   TFT_YELLOW,  false},
        {"气压", "hPa", sensorPress, TFT_CYAN,    false},
        {"紫外线", "", sensorUv, TFT_MAGENTA, true},
    };
    const int CW = 110, CH = 60, X0 = 6, Y0 = 36, GX = 8, GY = 6;
    for (int i = 0; i < 6; i++) {
        int cx = X0 + (i % 2) * (CW + GX);
        int cy = Y0 + (i / 2) * (CH + GY);
        uint16_t bg = 0x18E3;
        tft.fillRoundRect(cx, cy, CW, CH, 6, bg);
        tft.drawRoundRect(cx, cy, CW, CH, 6, cards[i].color);
        // 标签（左上）
        tft.setTextSize(1);
        tft.setTextColor(cards[i].color, bg);
        tft.drawString(cards[i].label, cx + 6, cy + 4);
        // 数值 + 单位：整体居中，单位在数值正右方、垂直对齐
        if (hasData) {
            if (cards[i].frac) snprintf(buf, sizeof(buf), "%.1f", cards[i].val);
            else snprintf(buf, sizeof(buf), "%.0f", cards[i].val);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        int tw4 = tft.textWidth(buf, 4);
        int tw1 = 0;
        if (cards[i].unit[0]) tw1 = tft.textWidth(cards[i].unit, 1) + 4;
        int startX = cx + (CW - (tw4 + tw1)) / 2;
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawString(buf, startX, cy + 20, 4);
        if (cards[i].unit[0]) {
            tft.setTextColor(TFT_DARKGREY, bg);
            tft.drawString(cards[i].unit, startX + tw4 + 4, cy + 24, 1);
        }
    }
}




// 作用：绘制环境页（温度/湿度/气压）
void drawPageEnv() {
    // 环境页：温度/湿度/气压三行大数字（数据来自 C3 手环经 BLE 上报）
    char buf[32];
    tft.setTextSize(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("传感器数据", 65, 25);
    int y = 55;
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("温度", 20, y); y += 18;
    tft.setTextSize(3); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.1f C", sensorTemp);
    tft.drawString(buf, 30, y); y += 35;
    tft.drawString("湿度", 20, y); y += 18;
    tft.setTextSize(3); tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.0f %%", sensorHum);
    tft.drawString(buf, 30, y); y += 35;
    tft.drawString("气压", 20, y); y += 18;
    tft.setTextSize(2); tft.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.0f hPa", sensorPress);
    tft.drawString(buf, 30, y);
}

// 作用：绘制系统信息页
void drawPageSystem() {
    // 系统信息页：WiFi/MQTT/BLE/电池/运行时长/RX 计数（调试用）
    char buf[64];
    tft.setTextSize(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("系统状态", 75, 25);
    int y = 50;
    tft.setTextSize(1);
    tft.setTextColor(wifiOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
    if (wifiOk) sprintf(buf, "WiFi:%s (%d dBm)", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    else sprintf(buf, "WiFi:FAIL");
    tft.drawString(buf, 15, y); y += 18;
    tft.setTextColor(mqttOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
    if (mqttOk) tft.drawString("MQTT:OK", 15, y);
    else { sprintf(buf, "MQTT:FAIL (rc=%d)", mqttLastRc); tft.drawString(buf, 15, y); }
    y += 18;
    tft.setTextColor(bleConnected ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
    tft.drawString(bleConnected ? "BLE:OK" : "BLE:搜索中...", 15, y); y += 18;
    int bat = getBatteryPercent();
    snprintf(buf, sizeof(buf), "电池:%d%%", bat);
    tft.setTextColor(bat < 20 ? TFT_RED : TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, 15, y); y += 18;
    snprintf(buf, sizeof(buf), "运行:%ds", seconds);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, 15, y); y += 18;
    snprintf(buf, sizeof(buf), "RX:%d", sensorRxCount); tft.drawString(buf, 15, y);
}

// 作用：绘制关于页
void drawPageAbout() {
    // 关于页：固件版本与硬件简介
    char buf[64];
    tft.setTextSize(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("T-Watch V3", 60, 25); int y = 55;
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("T-Watch V3 (ESP32)", 15, y); y += 18;
    sprintf(buf, "固件: v16 Modular"); tft.drawString(buf, 15, y); y += 18;
    tft.drawString("ESP32 240MHz, 16MB Flash", 15, y); y += 18;
    sprintf(buf, "电池: %d%%", getBatteryPercent()); tft.drawString(buf, 15, y); y += 30;
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("传感器 via BLE", 15, y); y += 16;
    tft.drawString("上传 to Cloud 40s", 15, y);
}

// 作用：进入天气页（触发一次天气刷新）
void drawPageWeather() {
    weather_drawPage(&tft);
}

// 作用：进入时钟板块（闹钟/秒表/计时）
void drawPageClock() {
    clock_drawPage(&tft);
}

// 作用：绘制设备信息页
void drawPageDevice() {
    // 设备详情页：硬件参数 + 当前网络/传感器状态汇总
    char buf[64];
    tft.setTextSize(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("设备详情", 75, 25); int y = 50;
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("型号: TTGO T-Watch V3", 15, y); y += 18;
    tft.drawString("主控: ESP32 (240MHz)", 15, y); y += 18;
    tft.drawString("Flash: 16MB, RAM: 320KB", 15, y); y += 18;
    tft.drawString("屏幕: 1.54\" ST7789 240x240", 15, y); y += 18;
    tft.drawString("触摸: FT6336 电容触摸", 15, y); y += 18;
    int bat = getBatteryPercent();
    snprintf(buf, sizeof(buf), "电池: %d%% %s", bat, isCharging() ? "(充电中)" : "");
    tft.setTextColor(bat < 20 ? TFT_RED : TFT_WHITE, TFT_BLACK); tft.drawString(buf, 15, y); y += 18;
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("传感器: 温湿度/气压/心率/血氧/UV", 15, y); y += 18;
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(buf, sizeof(buf), "WiFi:%s BLE:%s MQTT:%s", wifiOk?"OK":"NO", bleConnected?"OK":"NO", mqttOk?"OK":"NO");
    tft.drawString(buf, 15, y); y += 18;
    snprintf(buf, sizeof(buf), "运行时间: %ds", seconds); tft.drawString(buf, 15, y); y += 18;
    snprintf(buf, sizeof(buf), "数据接收: %d 条", sensorRxCount); tft.drawString(buf, 15, y);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.drawString("按按钮返回首页", 55, 225);
    // 提示文字：物理按键返回主页（本项目以触摸为主，此提示仅供参考）
}

// ===== 表盘/背景图板块（v29o）=====
// 板块状态：_wfTab 当前 tab(0表盘/1背景图)、_wfBgPreview 背景图预览序号、
// _wfCardMsg/_wfCardUntil：双击后的 3s 提示小卡片
// tab: 0=表盘, 1=背景图；背景图预览槽 0~2；3s 小卡片
static int _wfTab = 0;
    // 当前 tab：0=表盘，1=背景图
static int _wfBgPreview = 0;
    // 背景图预览序号（显示序号，按上传时间排序后的第几张）
static char _wfCardMsg[24] = "";
static unsigned long _wfCardUntil = 0;
    // 小卡片显示截止时间（millis + 3000）；清空消息则隐藏

int wfGetTab() { return _wfTab; }
// 作用：读取当前 tab（main.cpp 滑动/双击分支判断用）
// 作用：表盘/背景图 tab 切换
void wfSetTab(int tab) {
    // 导航栏点击切换 tab：只接受 0/1，非法值忽略
    if (tab != 0 && tab != 1) return;
    _wfTab = tab;
}
int wfBgPreviewSlot() { return wallpaperOrderedSlot(_wfBgPreview); }   // v29x: 物理槽位(按上传时间排序)
// 作用：把"显示序号"换算成"物理存储槽位"（上传时间排序后第 N 张存在哪个文件槽）
// 作用：背景图左右滑切换（dir=+1 下一个 / -1 上一个）
void wfBgCycle(int dir) {
    int cnt = wallpaperSlotCount();
    // 没有壁纸时左右滑无效
    if (cnt <= 0) return;                       // 无图不移动
    int n = _wfBgPreview + (dir > 0 ? 1 : -1);  // v29x: 按有效张数循环(显示序号)
    if (n < 0) n = cnt - 1;
    // 边界回绕：第一个的上一个是最后一个，最后一个的下一个是第一个（循环切换）
    if (n >= cnt) n = 0;
    _wfBgPreview = n;
    _wfCardMsg[0] = 0;   // 切换槽位时清除旧卡片，避免文案与当前槽不符
    // 切槽后清掉旧的提示文案：防止显示"已启用该背景图"但当前槽已变化
}
// 作用：启用/关闭当前背景图
void wfBgToggle() {
    int slot = wallpaperOrderedSlot(_wfBgPreview);   // v29x: 物理槽位
    if (slot < 0) return;                            // 空槽不响应
    int act = wallpaperGetActive();
    // 当前启用的物理槽位：与点击槽位相同 → 关闭；不同 → 切换启用
    if (act == slot) {
        wallpaperSetActive(-1);
        snprintf(_wfCardMsg, sizeof(_wfCardMsg), "已关闭背景图");
    } else {
        wallpaperSetActive(slot);
        snprintf(_wfCardMsg, sizeof(_wfCardMsg), "已启用该背景图");
    }
    _wfCardUntil = millis() + 3000;
    // 记录 3 秒后到期：由 wfTabCardExpired/drawWfCard 控制卡片消失
}
// 作用：判断「已启用/已关闭」小卡片是否到 3 秒自动消失
bool wfTabCardExpired() {
    // 卡片到 3 秒：清空消息并返回 true，让 main.cpp 触发整页重绘清掉卡片
    if (!_wfCardMsg[0]) return false;
    if (millis() >= _wfCardUntil) { _wfCardMsg[0] = 0; return true; }
    return false;
}
bool wfTabCardShowing() { return _wfCardMsg[0] != 0; }

// 顶部导航栏：表盘 | 背景图（覆盖在内容之上，点击切换 tab）
// 作用：绘制表盘板块顶部导航栏（表盘 | 背景图）
static void drawWfNavBar() {
    // 顶部导航栏：深灰底 + "表盘 | 背景图"两个 tab 标题 + 当前 tab 下划线
    tft.setTextSize(1);
    tft.fillRect(0, 0, 240, 26, 0x1082);           // 深灰底
    tft.drawRect(0, 0, 240, 26, TFT_DARKGREY);
    tft.setTextColor(_wfTab == 0 ? TFT_CYAN : TFT_DARKGREY, 0x1082);
    tft.drawString("表盘", 30, 5);
    tft.setTextColor(_wfTab == 1 ? TFT_CYAN : TFT_DARKGREY, 0x1082);
    tft.drawString("背景图", 140, 5);
    // 当前 tab 下划线
    if (_wfTab == 0) tft.drawLine(20, 25, 100, 25, TFT_CYAN);
    else             tft.drawLine(128, 25, 220, 25, TFT_CYAN);
}

// 3s 小卡片（居中弹窗）
// 作用：绘制表盘/背景图预览卡片主体
static void drawWfCard() {
    // 3s 提示小卡片：文字宽自适应 + 居中弹窗，到点自动消失
    if (!_wfCardMsg[0]) return;
    if (millis() >= _wfCardUntil) { _wfCardMsg[0] = 0; return; }
    int w = tft.textWidth(_wfCardMsg) + 24;
    int x = (240 - w) / 2;
    tft.fillRoundRect(x, 100, w, 40, 8, 0x2104);
    tft.drawRoundRect(x, 100, w, 40, 8, TFT_CYAN);
    tft.setTextColor(TFT_WHITE, 0x2104);
    tft.drawString(_wfCardMsg, x + 12, 112);
}

// 表盘 tab：渲染当前表盘全屏预览
// 作用：绘制「表盘」tab：7 款表盘预览 + 当前选中标记
static void drawWfTabFace() {
    // 表盘 tab：调用表盘渲染器全屏预览当前表盘（时间取当前真实时间）
    tft.unloadFont();
    time_t now_t = time(nullptr);
    struct tm* ti = localtime(&now_t);
    if (ti && now_t > 1000000000) {
        wf_invalidateCache();  // 强制重新渲染
        wf_compactMode = true;  // 预览模式：表盘跳过底部信息
    // 预览模式：隐藏表盘底部信息，只显示表盘主体，便于观察款式
        wf_render(&tft, ti->tm_hour, ti->tm_min, ti->tm_sec);
    } else {
        wf_invalidateCache();
        wf_render(&tft, 10, 10, 30);
    }
    wf_compactMode = false;
    // 重新加载 .vlw 字体用于 UI 绘制
    tft.loadFont(font_vlw);
    // 左上：表盘名字（导航栏下方）
    // 预览页覆盖信息：左上表盘名、右上"第几张/共几张"
    tft.setTextSize(1); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(wf_getName(wf_getCurrent()), 2, 30);
    // 右上：第几个
    char _idxbuf[16];
    snprintf(_idxbuf, sizeof(_idxbuf), "%d/%d", wf_getCurrent() + 1, wf_getCount());
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(_idxbuf, 240 - tft.textWidth(_idxbuf) - 2, 30);
}

// 背景图 tab：全屏预览当前槽 + 启用标记 + 卡片
// 作用：绘制「背景图」tab：3 张壁纸预览（左上角 x/3 序号）
static void drawWfTabBg() {
    // 背景图 tab：全屏预览当前槽位背景图 + 左上角"背景图 x/n"序号 + 3s 卡片
    tft.unloadFont();
    tft.fillScreen(TFT_BLACK);
    int slot = wallpaperOrderedSlot(_wfBgPreview);   // v29x: 物理槽位(按上传时间排序)
    int cnt  = wallpaperSlotCount();
    if (slot >= 0 && wallpaperSlotExists(slot)) {
        // 槽位有效才绘制壁纸；空槽保持黑屏
        wallpaperDrawSlot(&tft, slot);
    }
    tft.loadFont(font_vlw);
    tft.setTextSize(1);
    // 页面唯一常驻信息：左上角 当前序号/有效张数（导航栏下方）；启用状态与操作提示已去掉(v29t)
    char buf[24];
    if (cnt > 0) snprintf(buf, sizeof(buf), "背景图 %d/%d", _wfBgPreview + 1, cnt);
    else         snprintf(buf, sizeof(buf), "背景图 无");
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, 2, 30);
    // 小卡片（双击启用/关闭反馈, 3s 自动消失）
    drawWfCard();
}

// ===== 表盘/背景图板块主绘制 =====
// 作用：绘制表盘/背景图选择页（入口调度）
void drawPageWFSel() {
    // 表盘/背景图选择页入口：先画 tab 内容，再让导航栏覆盖在最上层
    if (_wfTab == 0) drawWfTabFace();
    else             drawWfTabBg();
    drawWfNavBar();   // 导航栏始终覆盖在最上层
}

// ====== 下拉状态面板：日期时间/星期/连接状态/电量 ======
static const uint16_t PD_BG = 0x0841;   // 面板背景（深灰）
// PD mark: check/cross (self-drawn, no glyph in builtin fonts)
// 作用：下拉面板画小状态点（√ 正常 / × 异常）
static void drawPDMark(int16_t x, int16_t y, bool ok, uint16_t colOk, uint16_t colBad) {
    // 自绘状态符号：√ 正常（两段折线）/ × 异常（两条交叉线）；内置字体无对勾字形故手绘
    if (ok) {
        tft.drawLine(x, y + 6, x + 5, y + 11, colOk);
        tft.drawLine(x + 5, y + 11, x + 14, y + 1, colOk);
    } else {
        tft.drawLine(x, y, x + 13, y + 13, colBad);
        tft.drawLine(x + 13, y, x, y + 13, colBad);
    }
}


// 作用：绘制下拉面板（WiFi/MQTT/电源 快捷开关）
void drawPullDown() {
    // 下拉状态面板整页：日期/星期/时间 + WiFi/BLE/MQTT 状态 + 自动息屏开关 + 电量
    tft.unloadFont();
    tft.fillRect(0, 0, 240, 240, PD_BG);
    tft.drawRect(0, 0, 240, 240, TFT_CYAN);
    char buf[32];
    time_t now_t = time(nullptr);
    struct tm* ti = localtime(&now_t);
    static const char* wd[] = {"\u5468\u65e5","\u5468\u4e00","\u5468\u4e8c","\u5468\u4e09","\u5468\u56db","\u5468\u4e94","\u5468\u516d"};
    tft.setTextSize(1);
    // date (font4)
    // 日期用 4 号大字体居中；NTP 未校时显示占位横线
    if (ti && now_t > 1000000000)
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
    else snprintf(buf, sizeof(buf), "----/--/--");
    tft.setTextColor(TFT_CYAN, PD_BG);
    tft.drawString(buf, (240 - tft.textWidth(buf, 4)) / 2, 28, 4);
    // weekday (vlw Chinese font; builtin fonts have no CJK glyphs)
    // 星期用 vlw 中文字体（内置字体无中文字形）
    tft.setTextColor(TFT_CYAN, PD_BG);
    if (ti && now_t > 1000000000)
        snprintf(buf, sizeof(buf), "%s", wd[ti->tm_wday]);
    else snprintf(buf, sizeof(buf), "--");
    tft.loadFont(font_vlw);
    int ww = tft.textWidth(buf);
    tft.drawString(buf, (240 - ww) / 2, 70, 1);
    tft.unloadFont();
    tft.setTextSize(1);
    // time (font7)
    // 时间用 7 号大字体，先清一块区域再画，避免残影
    if (ti && now_t > 1000000000)
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
    else snprintf(buf, sizeof(buf), "--:--:--");
    int tw = tft.textWidth(buf, 7);
    tft.setTextColor(TFT_WHITE, PD_BG);
    tft.fillRect((240 - tw) / 2 - 4, 104, tw + 8, 52, PD_BG);
    tft.drawString(buf, (240 - tw) / 2, 104, 7);
    // status marks (no colon)
    // 三行连接状态：每行左侧 √/× 标记 + 右侧文字（WiFi/BLE/MQTT）
    int y = 172;
    drawPDMark(30, y + 4, wifiOk, TFT_GREEN, TFT_RED);
    tft.setTextColor(wifiOk ? TFT_GREEN : TFT_RED, PD_BG);
    snprintf(buf, sizeof(buf), "WiFi %s", wifiOk ? "\u5df2\u8fde\u63a5" : "\u672a\u8fde\u63a5");
    tft.drawString(buf, 50, y, 2); y += 20;
    drawPDMark(30, y + 4, bleConnected, TFT_GREEN, TFT_ORANGE);
    tft.setTextColor(bleConnected ? TFT_GREEN : TFT_ORANGE, PD_BG);
    snprintf(buf, sizeof(buf), "BLE %s", bleConnected ? "\u5df2\u8fde\u63a5" : "\u672a\u8fde\u63a5");
    tft.drawString(buf, 50, y, 2); y += 20;
    drawPDMark(30, y + 4, mqttOk, TFT_GREEN, TFT_RED);
    tft.setTextColor(mqttOk ? TFT_GREEN : TFT_RED, PD_BG);
    snprintf(buf, sizeof(buf), "MQTT %s", mqttOk ? "\u5df2\u8fde\u63a5" : "\u672a\u8fde\u63a5");
    tft.drawString(buf, 50, y, 2); y += 20;
    // 自动息屏开关（右下角，点击切换，main.cpp 下拉面板分支处理点击区域）
    // 开关显示"自动息屏 开/关"，颜色绿/红；点击区域在 main.cpp 下拉面板分支判断
    tft.loadFont(font_vlw);                     // font2 无中文字形，开关文字用 vlw 字体
    tft.setTextColor(TFT_CYAN, PD_BG);
    tft.drawString("\u81ea\u52a8\u606f\u5c4f", 148, 210, 1);
    tft.setTextColor(autoSleepEnabled() ? TFT_GREEN : TFT_RED, PD_BG);
    tft.drawString(autoSleepEnabled() ? "\u5f00" : "\u5173", 148 + tft.textWidth("\u81ea\u52a8\u606f\u5c4f") + 8, 210, 1);
    tft.unloadFont();
    // battery (top-right)
    // 右上角电量：充电时加"+"号且绿色，低电量红色
    int bat = getBatteryPercent();
    snprintf(buf, sizeof(buf), "%s%d%%", isCharging() ? "+" : "", bat);
    tft.setTextColor(bat < 20 ? TFT_RED : TFT_WHITE, PD_BG);
    tft.drawString(buf, 240 - tft.textWidth(buf, 2) - 6, 8, 2);
}


// 作用：下拉面板时间文字刷新
void refreshPullDownTime() {
    // 下拉面板时间局部刷新：秒数变了才重绘（static lastSec 去重），消除每秒闪烁
    static int8_t lastSec = -1;                 // 上次绘制秒，秒未变则不重绘，消除闪烁
    char buf[32];
    time_t now_t = time(nullptr);
    struct tm* ti = localtime(&now_t);
    int8_t sec = (ti && now_t > 1000000000) ? (int8_t)ti->tm_sec : -1;
    if (sec == lastSec) return;                 // 秒未变：跳过重绘
    lastSec = sec;
    if (ti && now_t > 1000000000)
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
    else snprintf(buf, sizeof(buf), "--:--:--");
    int tw = tft.textWidth(buf, 7);
    tft.setTextColor(TFT_WHITE, PD_BG);
    tft.fillRect((240 - tw) / 2 - 4, 104, tw + 8, 52, PD_BG);
    tft.drawString(buf, (240 - tw) / 2, 104, 7);
}

// 作用：屏幕总调度：按当前页面刷新对应界面
void drawMainScreen() {
    // 全屏重绘入口：清屏 + 画状态栏 + 按当前页面分发绘制
    // 注意：整页重绘只在切页/关键变化时调用；连续刷新用各 refresh 局部函数
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    switch (currentPage) {
        // 页面分发：每个 PAGE_XXX 对应一个 drawPageXXX
        case PAGE_HOME:   drawPageHome(); break;
        case PAGE_HEALTH: drawPageHealth(); break;
        case PAGE_ENV:    drawPageEnv(); break;
        case PAGE_SYSTEM: drawPageSystem(); break;
        case PAGE_ABOUT:  drawPageAbout(); break;
        case PAGE_DEVICE: drawPageDevice(); break;
        case PAGE_WEATHER: drawPageWeather(); break;
        case PAGE_CLOCK:    drawPageClock();    break;
        case PAGE_SPORT:    drawPageSport();    break;
        case PAGE_NOTIFY:   drawPageNotify();   break;
        case PAGE_WFSEL:  drawPageWFSel(); break;
    }
}




