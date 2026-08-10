/*
 * T-Watch V3 智能手表主程序（入口）
 * 功能：开机初始化各板块（电源/触摸/运动/天气/表盘），主循环调度所有板块：
 *       刷新表盘时间、触摸滑动切页、联网（WiFi/NTP/MQTT/BLE）、全局计步累计
 * 架构：本文件只做"调度"，具体功能在各板块目录实现（ui/weather/sport/...）

// ============ 代码阅读指引 ============
// 本文件是手表固件入口，只负责"调度"，不实现具体界面：
//  - setup()：开机初始化（电源/屏幕/联网/各板块）
//  - loop() ：主循环，每秒执行很多次（渲染表盘/响应触摸/联网保活/息屏唤醒）
//  - 具体界面与业务在各板块：ui(页面)/watchface(表盘)/weather(天气)/sport(运动)/
//    clock(时钟闹钟)/notify(通知)/wallpaper(背景图)/mqtt(云平台)/power(电源)/touch(触摸)
// =====================================
 */


#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include "fonts/font_vlw.h"
#include "ui/ui.h"
#include "mqtt/mqtt.h"
#include "power/power.h"
#include "touch/touch.h"
#include "button/button.h"
#include "watchface/watchface_manager.h"
#include "weather/weather.h"
#include "clock/clock.h"
#include "sport/sport.h"
#include "notify/notify.h"
#include "wallpaper/wallpaper.h"

// ===== 网络配置（需与路由器/云平台一致）=====
// WIFI_SSID/WIFI_PASS：家里 WiFi 账号密码
// MQTT_HOST/PORT：ThingsCloud 云平台服务器地址与端口；CLIENT_ID/USER/PASS：设备登录凭证
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* MQTT_HOST = "gz-3-mqtt.iot-api.com";
const int   MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "YOUR_MQTT_CLIENT_ID";
const char* MQTT_USER = "YOUR_MQTT_USERNAME";
const char* MQTT_PASS = "YOUR_MQTT_PASSWORD";

// ===== 引脚定义 =====
// SEN_SDA(21)/SEN_SCL(22)：外设 I2C 总线（Wire1），挂载 AXP202 电源芯片 + BMA423 加速度计
#define SEN_SDA    21
#define SEN_SCL    22
#define USER_BTN   36
// USER_BTN：物理按键引脚（本项目以触摸为主，按键未实际使用）
// TFT_BL：屏幕背光引脚（高电平亮）
#define TFT_BL     12
// BLE 服务/特征 UUID：与 C3 传感器手环约定的通信标识（两边一致才能互相识别）
#define SERVICE_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define CHARACTERISTIC_UUID "b2c3d4e5-f6a7-8901-bcde-f12345678901"

// ===== 全局运行状态 =====
// sysMode：当前模式（0=表盘 MODE_WATCHFACE，1=应用页 MODE_APPS）
// currentPage：应用页里的当前板块（0=主页/健康/运动/天气/时钟/通知...）
int sysMode = 0;
int currentPage = 0;

// ====== 下拉状态面板 ======
// ====== 下拉状态面板 ======
// pullDownActive：下拉面板是否打开；pullDownFromWatchface：是否从表盘页呼出（恢复方式不同）
static bool pullDownActive = false;          // 下拉面板是否打开
static bool pullDownFromWatchface = false;   // 是否从表盘页呼出（恢复方式不同）
// v29j: 息屏前页面记忆（唤醒后先显示表盘，右边缘左滑返回该页；-1=无）
static int  prevPageBeforeSleep = -1;   // 息屏前应用页 currentPage（-1=无）
static int  prevModeBeforeSleep = -1;   // 息屏前模式 MODE_APPS/MODE_WATCHFACE（-1=无）
#define MODE_WATCHFACE 0
#define MODE_APPS 1

// ===== 全局对象与联网状态 =====
// tft：屏幕绘制对象；mqtt：MQTT 客户端（连云平台，上报传感器/接收下发）
// seconds：软件秒计数（NTP 失败时表盘兜底计时）；wifiOk/mqttOk/bleConnected：各网络状态
TFT_eSPI tft;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
int seconds = 0, mqttLastRc = 0;
bool wifiOk = false, mqttOk = false, bleConnected = false;
IPAddress mqttIP;
bool mqttIPResolved = false;

// ===== 传感器数据（来自外部 ESP32-C3 手环，经 BLE 接收）=====
// sensorTemp/Hum/Press：温度/湿度/气压；sensorBpm/Spo2/Uv：心率/血氧/紫外线
float sensorTemp=0, sensorHum=0, sensorPress=0;
float sensorBpm=0, sensorSpo2=0, sensorUv=0;
volatile bool haveSensorData = false;
// haveSensorData：是否收到过 BLE 数据（健康页判断"有无数据"用）
// sensorRxCount：收到次数计数；pendingBuzz：请求马达震动标志；healthDirty：健康页待刷新标志
int sensorRxCount = 0;
volatile bool pendingBuzz = false;
volatile bool healthDirty = false;
// 亮屏通知卡片（亮屏时来消息：震动 + 卡片覆盖 3 秒，到期/触摸后恢复原页面）
// 亮屏通知卡片（亮屏时来消息：震动 + 卡片覆盖 3 秒，到期/触摸后恢复原页面）
// notifyLight/notifyUntil：息屏时的通知卡片（亮屏显示 3 秒后自动灭屏）
static bool notifyCardActive = false;
static int  notifyCardPrevPage = 0;
static unsigned long notifyCardUntil = 0;
static bool notifyLight = false;          // 息屏通知卡片显示中（文件级，亮屏分支共用）
static unsigned long notifyUntil = 0;     // 息屏卡片结束时间

// ===== BLE 连接状态（连接 C3 传感器手环，接收实时数据）=====
// doConnect：扫描到目标后置位请求连接；doScan：是否继续扫描；pRemoteCharacteristic：通知特征
static BLEUUID bleServiceUUID(SERVICE_UUID);
static BLEUUID bleCharUUID(CHARACTERISTIC_UUID);
static boolean doConnect = false, doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic = NULL;
static BLEAdvertisedDevice* myDevice = NULL;
static BLEClient* pClient = NULL;

// ====== BLE 回调 ======
static void notifyCallback(BLERemoteCharacteristic*, uint8_t*, size_t, bool);
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient*) override {}
    void onDisconnect(BLEClient*) override { bleConnected=false; doScan=true; Serial.println("[BLE] Disconnected"); }
    // 手环断开：标记未连接 + 重新开启扫描（自动重连）
};
// 作用：连接手环 BLE 服务，成功后开始接收实时数据
bool connectToServer();
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
// 作用：BLE 扫描回调：记录手环广播地址
    void onResult(BLEAdvertisedDevice ad) override {
        if (ad.haveName() && ad.getName() == "C3-Sensor") {
            BLEDevice::getScan()->stop();
            myDevice = new BLEAdvertisedDevice(ad);
            doConnect = true; doScan = false;
            Serial.printf("[BLE] Found: %s\n", ad.getAddress().toString().c_str());
        }
    }
};

// 作用：BLE 收到手环数据回调：解析温度/湿度/气压/心率/血氧/UV
static void notifyCallback(BLERemoteCharacteristic*, uint8_t* pData, size_t len, bool) {
    char buf[128]; int c = min((int)len,127); memcpy(buf,pData,c); buf[c]=0;
    // 把 BLE 收到的原始字节复制到本地字符串（最多 127 字节，末尾补 0 成合法字符串）
    char* p;
    p=strstr(buf,"\"temp\":");  if(p) sensorTemp=atof(p+7);
    // 在 JSON 文本里逐个查找字段：strstr 定位 "temp": 出现的位置，atof 把其后的数字转成数值存进全局变量
    // 其余 hum/press/bpm/spo2/uv 同理（偏移量 6~8 = 引号+字段名+冒号 的长度）
    p=strstr(buf,"\"hum\":");   if(p) sensorHum=atof(p+6);
    p=strstr(buf,"\"press\":"); if(p) sensorPress=atof(p+8);
    p=strstr(buf,"\"bpm\":");   if(p) sensorBpm=atof(p+6);
    p=strstr(buf,"\"spo2\":");  if(p) sensorSpo2=atof(p+7);
    p=strstr(buf,"\"uv\":");    if(p) sensorUv=atof(p+5);
    // 标记"收到过数据"，健康页靠它区分"有数据/无数据"
    haveSensorData = true;
    // 仅当正在健康页时：计数 + 请求震动 + 标记刷新（其他页面收到数据只更新变量，不打扰用户）
    if (sysMode == MODE_APPS && currentPage == PAGE_HEALTH) { sensorRxCount++; pendingBuzz = true; healthDirty = true; }
    Serial.printf("[BLE RX#%d] t=%.1f h=%.0f p=%.0f bpm=%.0f spo2=%.0f uv=%.0f\n", sensorRxCount, sensorTemp, sensorHum, sensorPress, sensorBpm, sensorSpo2, sensorUv);
}

bool connectToServer() {
    Serial.printf("[BLE] Connecting to %s ...\n", myDevice->getAddress().toString().c_str());
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    if (!pClient->connect(myDevice)) return false;
    pClient->setMTU(517);
    BLERemoteService* s = pClient->getService(bleServiceUUID);
    if (!s) { pClient->disconnect(); return false; }
    pRemoteCharacteristic = s->getCharacteristic(bleCharUUID);
    if (!pRemoteCharacteristic) { pClient->disconnect(); return false; }
    if (pRemoteCharacteristic->canNotify())
        pRemoteCharacteristic->registerForNotify(notifyCallback);
    bleConnected = true; return true;
}

// ===== setup()：开机初始化（整个程序只执行一次）=====
// 顺序：串口→马达引脚→屏幕背光→电源芯片(AXP)→按键→屏幕+字体→WiFi→NTP校时→
//       BLE扫描→触摸→各功能板块→MQTT连接→预拉天气→释放字体
void setup() {
    Serial.begin(115200);
    pinMode(MOTOR, OUTPUT);
    delay(300);
    Serial.println("\n=== T-Watch V3 v18 (NTP) ===\n");
    Serial.printf("[MEM] heap at setup start = %u\n", ESP.getFreeHeap());
    // 打印启动瞬间的空闲堆内存（字节）：记录初始值，便于后期对比排查内存泄漏

    Wire1.begin(SEN_SDA, SEN_SCL);
    // 启动外设 I2C 总线（Wire1）：电源芯片 AXP202、加速度计 BMA423 都挂在这条总线上
    // 背光 GPIO 初始化（高电平亮，不依赖 AXP 检测 / LEDC PWM）
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    // 屏幕背光引脚拉高：开机立即点亮背光（不等 AXP 检测，先亮屏再干活）
    uint8_t axpId = readAXP(0x03);
    Serial.printf("[PWR] AXP ID: 0x%02X\n", axpId);
    // 读取电源管理芯片型号：0x41 = AXP202（T-Watch V3 所用）
    // 确认型号后才做电源配置：
    //  - writeAXP(0x12, 0xFF)：0x12 是供电输出控制寄存器，全 1 = 打开所有 LDO/DC-DC 输出
    //  - setLDO2Voltage(3300)：LDO2 输出电压设为 3.3V（屏幕电源轨）
    //  - 快速"关→开"一次 LDO2：给屏幕断电再上电，彻底复位 LCD，避免花屏/残影
    if (axpId == 0x41) {
        writeAXP(0x12, 0xFF);
        setLDO2Voltage(3300);
        delay(100); setLDO2(false); delay(50); setLDO2(true); delay(200);
    }
    // 初始化物理按键（GPIO36）：本项目以触摸操作为主，按键仅保留检测，不参与页面逻辑
    btn_init(USER_BTN);  // 初始化按钮检测（需在 Wire1.begin 之后）

    tft.init(); tft.loadFont(font_vlw); tft.setRotation(0);
    // 屏幕初始化 + 加载矢量字体 font_vlw（含中文字形的字库）
    // setRotation(0)：竖屏方向（0 度），分辨率 240×240
    Serial.printf("[MEM] heap after loadFont(font_vlw) = %u\n", ESP.getFreeHeap());
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("智能手表", 35, 15);
    // 开机引导画面：黑底 + 标题，下面初始化到哪一步就在对应位置打印提示

    // ---- WiFi + NTP ----
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("WiFi连接中..", 30, 50);
    Serial.print("[WiFi] Connecting...");
    WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
    // 以"站点模式"连接家中路由器（STA=作为客户端去连别人的热点，不是自己开热点）
    for (int i=0; i<30; i++) { if (WiFi.status()==WL_CONNECTED) break; delay(500); Serial.print("."); }
    wifiOk = (WiFi.status()==WL_CONNECTED);
    // 最多等待 30×0.5 秒 = 15 秒：连上立即跳出，超时按失败处理
    // wifiOk 记录最终结果：后续 NTP/天气/MQTT 都先判断它，WiFi 不通就不做联网
    Serial.println(wifiOk?" OK":" FAIL");
    tft.drawString(wifiOk?"WiFi已连接":"WiFi失败", 45, 75);

    // NTP 网络校时（北京时间 UTC+8）
    if (wifiOk) {
        // 天气：自动 IP 定位 + 30分钟刷新
        configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
        // 配置 NTP 校时：时区 +8 小时（北京时间），同时提供 3 个 NTP 服务器轮流尝试
        Serial.println("[NTP] Syncing time...");
        int ntpWait = 0;
        while (time(nullptr) < 1000000000 && ntpWait < 20) {
            delay(500); ntpWait++;
        }
        if (time(nullptr) > 1000000000) Serial.println("[NTP] OK");
        // 等待系统时间有效（时间戳 > 10 亿 ≈ 2001 年，说明已从 NTP 拿到时间），最多 20×0.5=10 秒
        // 成功后系统时间就是北京时间：表盘/天气/闹钟都基于它
        else Serial.println("[NTP] FAIL");
    }

    // ---- BLE ----
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("BLE扫描中..", 30, 105);
    BLEDevice::init("");
    // 初始化 BLE 主机（手表作为主机去连接 C3 传感器手环）
    // 扫描参数：每 1.349 秒扫 0.449 秒、主动扫描，共扫 5 秒
    // 回调发现"C3-Sensor"后自动停止扫描并置 doConnect，稍后在 loop 里真正连接
    BLEScan* sc = BLEDevice::getScan();
    sc->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    sc->setInterval(1349); sc->setWindow(449); sc->setActiveScan(true);
    sc->start(5, false);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("BLE扫描中..", 35, 130);

    // ---- MQTT（v29f: 连接延后到字体释放后，省 ~52KB 堆；此处仅 DNS 解析）----
    if (wifiOk) {
        if (WiFi.hostByName(MQTT_HOST, mqttIP)) { mqttIPResolved=true; Serial.printf("[MQTT] IP: %s\n", mqttIP.toString().c_str()); }
        // 只做 DNS 解析：把 MQTT 域名换成 IP 存进 mqttIP（真正连接延后到字体释放后）
        // v29f：启动阶段少做一次连接尝试，可省下约 52KB 堆，避免启动期内存紧张
    }

    initTouch();
    // ===== 各功能板块初始化（按依赖顺序）=====
    // 触摸屏（FT6336 电容屏，走 I2C）
    sport_init();
    // 运动传感器 BMA423：计步 + 活动识别 + 抬手检测（息屏抬手亮屏依赖它）
    autoSleepInit();
    // 自动息屏开关：读取 NVS 里上次保存的设置（默认开），下拉面板可切换并写回
    wf_init();
    // 表盘管理器：从 NVS 读取当前选中的表盘序号，加载内置表盘列表
    wallpaperInit();
    // 表盘背景模块：挂载 LittleFS（存背景图文件）+ 注册 JPEG 解码回调
    weather_init();
    // 天气模块：读取 NVS 缓存的上次城市/天气，避免每次开机都重新定位
    clock_init();
    // 时钟模块：闹钟/秒表/计时器初始化（NVS 读取闹钟设置）
    // v29h: MQTT 连接放回 weather_fetch 之前（v18 时序）；堆已充足(200KB+)，不受字体影响
    if (wifiOk) {
        connectMQTT();
        tft.setTextColor(mqttOk?TFT_GREEN:TFT_RED, TFT_BLACK);
        // 真正建立 MQTT 连接（前面只做了 DNS 解析）；结果用绿/红字显示在开机画面上
        tft.drawString(mqttOk ? "MQTT OK" : "MQTT FAIL", 40, 185);
    }
    // 开机预拉取天气数据（在 WiFi 已连接时）
    // 开机立刻拉一次天气（只有 WiFi 已连接才有意义）；失败不阻塞，稍后 loop 里会自动重试
    if (wifiOk) {
        weather_fetch();
    }
    delay(500);
    tft.fillScreen(TFT_BLACK);
    tft.unloadFont();
    Serial.printf("[MEM] heap after unloadFont = %u\n", ESP.getFreeHeap());
    // 开机画面结束：卸载大字体释放约 52KB 堆（表盘用自带小字库渲染）
    // buzz(100)：开机马达震动一下，提示设备已启动
    Serial.println("[SYS] Watchface mode");
    buzz(100);
}

// 通知卡片绘制（亮屏/息屏共用）
// 作用：在表盘上绘制手机通知卡片
static void drawNotifyCard(const NotifyItem& nit) {
    tft.unloadFont();
    tft.fillScreen(TFT_BLACK);
    tft.loadFont(font_vlw);
    tft.setTextSize(1);
    tft.drawRoundRect(24, 46, 192, 108, 8, TFT_CYAN);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(nit.src, 40, 64);
    int nx1 = 40 + tft.textWidth(nit.src) + 10;
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(nit.from, nx1, 64);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(nit.text, 40, 92);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("\u53cc\u51fb\u8fdb\u5165\u901a\u77e5", 66, 130);
}

// ===== loop()：主循环（每帧多次执行）=====
// 职责：表盘/应用页渲染、触摸手势切页、自动息屏与抬手/双击唤醒、
//       BLE/WiFi/MQTT 联网保活、全局计步、天气周期刷新、闹钟响铃
void loop() {
    unsigned long now = millis();
    static unsigned long lastSecond = 0;
    // seconds：软件秒计数（每 500ms +1，即 1 秒计 2 次）；NTP 未成功时表盘用它兜底显示时间
    if (now - lastSecond >= 500) { lastSecond = now; seconds++; }
    // 内存诊断（每 30 秒）
    static unsigned long lastHeapLog = 0;
    if (now - lastHeapLog >= 30000) { lastHeapLog = now; Serial.printf("[MEM] loop heap = %u\n", ESP.getFreeHeap()); }
    // 每 30 秒打印一次堆内存：长时间运行可观察是否内存泄漏
    // 触摸 I2C 健康诊断（每 5 秒）：判定触摸芯片死活（不影响功能）
    static unsigned long lastTouchDiag = 0;
    if (now - lastTouchDiag >= 5000) {
        lastTouchDiag = now;
        TouchPoint_t tp;
        bool tok = readTouch(&tp);
        Serial.printf("[TOUCHD] readTouch=%d x=%u y=%u touched=%d avail=%d\n", (int)tok, tp.x, tp.y, (int)tp.touched, (int)touchAvailable);
        // 每 5 秒读一次触摸芯片诊断：readTouch=1 表示 I2C 通信正常，touched=1 表示有手指按下
        // （只做诊断不影响功能；touchAvailable 才是实际使用标志）
    }

    // 主循环心跳（每 5 秒）：确认 main loop 活着、在哪个分支
    static unsigned long lastModeLog = 0;
    if (now - lastModeLog >= 5000) {
        lastModeLog = now;
        Serial.printf("[LOOP] mode=%s screenOff=%d wifi=%d heap=%u\n",
            (sysMode==MODE_WATCHFACE?"WF":"APPS"), isScreenOff(), WiFi.status(), ESP.getFreeHeap());
            // 每 5 秒打印主循环心跳：确认程序活着、当前在哪个模式、屏幕开关状态
    }

    // 运动板块：全页面持续计步（v29z: 原来只在运动页调用，导致表盘/主页晃动步数不累计不上报）
    sport_handleLoop(&tft);
    // ===== 全局计步 =====
    // 放在循环最前面、每帧都调用：任何页面（甚至息屏）都持续累计步数（v29z 起从运动页移到这里）

    // ====== 自动息屏（30s 无触摸，开关打开时）======
    // ====== 自动息屏：30 秒无操作就灭屏（开关在下拉面板，可关闭）======
    if (!isScreenOff() && autoSleepEnabled() && !clock_isRinging()
        && !notifyLight && !notifyCardActive    // 通知卡片显示中不自动息屏
        && (now - gLastTouchMs >= 30000)) {
        prevPageBeforeSleep = (sysMode == MODE_APPS) ? currentPage : -1;   // v29j: 记录息屏前页面（唤醒后先表盘，左滑返回）
        prevModeBeforeSleep = sysMode;
        screenOff();
        // 灭屏前记住当前页面/模式：唤醒后先显示表盘，右边缘左滑一步返回这个页面
    }

    // ====== 息屏状态：保持网络/BLE/MQTT 维护 + 抬手/双击亮屏 ======
    if (isScreenOff()) {
    // ====== 息屏分支：屏幕关了但系统没停 ======
    // 继续维护 BLE/WiFi/MQTT，并监听抬手/双击唤醒；每 5 秒打一次"睡眠心跳"确认活着
        static unsigned long lastSleepBeat = 0;
        if (now - lastSleepBeat >= 5000) { lastSleepBeat = now; Serial.println("[PWR] sleep alive"); }
        // 闹钟/计时器到点：即使息屏也要强制亮屏响铃（响铃优先于一切）

        // 闹钟响铃 → 强制亮屏走正常响铃流程
        clock_checkAlarm();
        if (clock_isRinging()) {
            screenOn();
            tft.writecommand(0x11); delay(120);   // LCD 唤醒（SLEEP OUT）并等待稳定
            pullDownActive = false;   // 响铃优先：先关掉下拉面板，避免面板状态残留
            if (sysMode == MODE_WATCHFACE) { tft.unloadFont(); tft.fillScreen(TFT_BLACK); wf_invalidateCache(); }
            else { tft.loadFont(font_vlw); drawMainScreen(); }
            clock_drawRinging(&tft);
            delay(20);
            return;
        }
        // BLE 连接维护（保持 C3 数据接收）
        // 息屏时保持与 C3 手环的连接：doConnect 有请求就尝试连接，未连接就继续扫描
        if (doConnect) {
            if (connectToServer()) Serial.println("[BLE] Connected!");
            else { doScan = true; }
            doConnect = false;
        }
        if (!bleConnected && doScan) {
            BLEDevice::getScan()->start(0, false); doScan = false;
        }
        // 抬手检测诊断（每 10 秒）：确认息屏分支的抬手检查在正常执行
        // sportAvailable：BMA423 是否初始化成功；失败则抬手功能不可用（可从日志排查）
        static unsigned long lastTiltLog = 0;
        if (now - lastTiltLog >= 10000) {
            lastTiltLog = now;
            Serial.printf("[WAKE] tilt branch alive sportAvail=%d\n", (int)sportAvailable);
        }
        // ===== 抬手亮屏 / 双击屏幕亮屏（唤醒优先：绝不被 MQTT/WiFi 重连阻塞）=====
        // ===== 抬手亮屏 / 双击亮屏（唤醒优先：绝不被 MQTT/WiFi 重连阻塞）=====
        // sport_wristRaised()：BMA423 判断"手腕抬起"动作是否发生
        // getGesture() 同时识别"双击屏幕"手势；两者任一满足即亮屏
        bool wakeByTilt = sport_wristRaised();
        bool wakeByDtap = false;
        if (touchAvailable) {
            uint16_t wx, wy;
            int wg = getGesture(&wx, &wy);
            // 诊断：息屏时收到任何手势都打印（2 秒节流），确认触摸在息屏后是否仍工作
            if (wg != GESTURE_NONE) {
                static unsigned long lastWlog = 0;
                if (millis() - lastWlog > 2000) {
                    lastWlog = millis();
                    Serial.printf("[WAKE] gesture=%d (%u,%u) heap=%u\n", wg, wx, wy, ESP.getFreeHeap());
                }
            }
            if (wg == GESTURE_DTAP) wakeByDtap = true;
        }
        if (wakeByTilt || wakeByDtap) {
            gLastTouchMs = millis();   // v29i: 唤醒视为一次活动，刷新自动息屏计时（否则亮屏后 30s 超时立即成立，刚亮就灭）
            // 唤醒处理流程：
            //  1) 刷新"距上次触摸"计时 → 亮屏后不会立即触发 30 秒自动息屏（v29i 修复"刚亮就灭"）
            //  2) notifyTakeNew() 清掉待弹通知 → 用户主动唤醒时不弹通知卡片打扰
            //  3) 亮屏 + 唤醒 LCD(SLEEP OUT) + 马达提示
            //  4) v29j：总是先显示表盘时间页；右边缘左滑一次才返回息屏前页面
            notifyLight = false;
            notifyTakeNew();   // 用户主动唤醒优先：本次不再弹通知卡片
            screenOn();
            tft.writecommand(0x11); delay(120);   // LCD 唤醒（SLEEP OUT）并等待稳定
            buzz(20);
            // v29j: 唤醒后总是先显示表盘时间页；右边缘左滑再返回息屏前页面
            pullDownActive = false;
            sysMode = MODE_WATCHFACE;
            tft.unloadFont(); tft.fillScreen(TFT_BLACK); wf_invalidateCache();
            Serial.println(wakeByTilt ? "[PWR] Wakeup by tilt" : "[PWR] Wakeup by double-tap");
            delay(20);
            return;   // 唤醒即返回，跳过本次网络维护（保命优先）
            // 唤醒成功直接结束本次循环：不做任何网络操作，保证亮屏速度最快
        }

        // 息屏时新通知到达：震动 + 强制亮屏显示通知卡片 3 秒，随后自动灭屏
        // 息屏时新通知到达：震动 + 亮屏显示通知卡片 3 秒再灭
        if (!notifyLight && notifyHasNew()) {
            notifyTakeNew();
            NotifyItem nit;
            bool got = notifyGet(0, &nit);
            screenOn();
            tft.writecommand(0x11); delay(120);   // LCD 唤醒（SLEEP OUT）并等待稳定
            buzz(80);
            tft.unloadFont();
            tft.fillScreen(TFT_BLACK);
            tft.loadFont(font_vlw);
            tft.setTextSize(1);
            if (got) {
                // 通知卡片：来源 + 发件人 / 内容前 10 字
                tft.drawRoundRect(24, 46, 192, 108, 8, TFT_CYAN);
                tft.setTextColor(TFT_CYAN, TFT_BLACK);
                tft.drawString(nit.src, 40, 64);
                int nx1 = 40 + tft.textWidth(nit.src) + 10;
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.drawString(nit.from, nx1, 64);
                tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
                tft.drawString(nit.text, 40, 92);
                tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
                tft.drawString("\u53cc\u51fb\u8fdb\u5165\u901a\u77e5", 66, 130);
            }
            notifyLight = true;
            notifyUntil = now + 3000;
            // 卡片画完：标记"卡片显示中" + 3 秒到期时间；到期由下方统一逻辑自动灭屏
            Serial.println("[PWR] Notify screen ON 3s");
        }

        // WiFi/MQTT 保活 + 40s 周期上报（connectMQTT 内部已有堆保护，低堆自动跳过）
        // ===== 息屏下的网络保活（每 5 秒一次）=====
        // 1) 处理待下载背景图  2) WiFi 掉线自动重连（最多 3 秒）
        // 3) MQTT 掉线重连     4) 每 40 秒周期上报一次传感器数据
        static unsigned long lastSleepNet = 0;
        if (now - lastSleepNet >= 5000) {
            lastSleepNet = now;
            wallpaperProcessPending();  // 表盘背景：息屏时也可下载图片
            if (wifiOk && WiFi.status() != WL_CONNECTED) { wifiOk = false; Serial.println("[WiFi] Disconnected!"); }
            if (!wifiOk) {
                WiFi.reconnect();
                for (int i=0;i<30&&WiFi.status()!=WL_CONNECTED;i++) delay(100);
                if (WiFi.status()==WL_CONNECTED) { wifiOk=true; Serial.println("[WiFi] Reconnected!"); }
            }
            if (wifiOk) {
                if (mqtt.connected()) {
                    mqtt.loop();
                    static unsigned long lastPub2 = 0;
                    if (mqttOk && now-lastPub2>=40000) { lastPub2=now; publishSensorData(); }
                } else { connectMQTT(); }
            }
        }
        delay(20);
        return;
    }

    // 响铃优先处理（闹钟/计时器到点，任何界面双击关闭）
    // 响铃时持续绘制响铃画面；点击响铃区可关闭（clock_handleRingingTap 返回 true 表示已关闭）
    // 关闭后按当前模式重绘原界面（表盘或应用页）
    clock_checkAlarm();
    if (clock_isRinging()) {
        clock_drawRinging(&tft);
        if (touchAvailable) {
            uint16_t rtx, rty;
            int rg = getGesture(&rtx, &rty);
            // 双击关闭：快双击(<400ms)手势层会合并成 GESTURE_DTAP，必须一起接受，否则快速双击永远关不掉
            if ((rg == GESTURE_TAP || rg == GESTURE_DTAP) && clock_handleRingingTap(rtx, rty)) {
                if (sysMode == MODE_WATCHFACE) { tft.unloadFont(); tft.fillScreen(TFT_BLACK); wf_invalidateCache(); }
                else { tft.loadFont(font_vlw); drawMainScreen(); }
            }
        }
        delay(20);
        return;
    }

    // ====== 下拉状态面板（优先处理）======
    // 顶部下滑呼出：显示 WiFi/BLE/电量/时间 + 自动息屏开关
    // 点击开关切换；点击其他区域或右滑/下滑收起面板
    if (pullDownActive) {
        static unsigned long lastPd = 0;
        if (now - lastPd >= 500) { lastPd = now; refreshPullDownTime(); }
        if (touchAvailable) {
            uint16_t pdx, pdy;
            int pdg = getGesture(&pdx, &pdy);
            if (pdg == GESTURE_TAP) {
                // 自动息屏开关区域（下拉面板右下角，x 140~238，y 200~238）
                if (pdx >= 140 && pdx <= 238 && pdy >= 200 && pdy <= 238) {
                    autoSleepSet(!autoSleepEnabled());
                    // 点击面板右下角开关区域：翻转自动息屏设置（写 NVS 持久化），重绘面板刷新开关显示
                    drawPullDown();   // 重绘面板刷新开关状态
                    buzz(20);
                } else {
                    buzz(20);
                    pullDownActive = false;
                    if (pullDownFromWatchface) { tft.unloadFont(); tft.fillScreen(TFT_BLACK); wf_invalidateCache(); }
                    else { tft.loadFont(font_vlw); drawMainScreen(); }
                }
            } else if (pdg == GESTURE_SWIPE_R || pdg == GESTURE_SWIPE_D) {
                buzz(20);
                pullDownActive = false;
                if (pullDownFromWatchface) { tft.unloadFont(); tft.fillScreen(TFT_BLACK); wf_invalidateCache(); }
                else { tft.loadFont(font_vlw); drawMainScreen(); }
            }
        }
        delay(20);
        return;
    }

    // BLE维护
    // ===== BLE 维护（亮屏时同样保持连接）=====
    // doConnect：扫描到"C3-Sensor"后置位，这里真正建立连接
    // 未连接且 doScan：重新开启扫描，实现断线自动重连
    if (doConnect) {
        if (connectToServer()) Serial.println("[BLE] Connected!");
        else { doScan = true; }
        doConnect = false;
    }
    if (!bleConnected && doScan) {
        BLEDevice::getScan()->start(0, false); doScan = false;
    }
    // 马达
    if (pendingBuzz) { pendingBuzz=false; digitalWrite(MOTOR,HIGH); delay(80); digitalWrite(MOTOR,LOW); }
    // 马达驱动：pendingBuzz 由健康页收到 BLE 数据时置位，这里输出 80ms 高电平产生震动

    // 亮屏时收到新通知：震动 + 通知卡片覆盖 3 秒（息屏场景由息屏分支弹卡片）
    if (notifyHasNew()) {
        notifyTakeNew();
        NotifyItem nit;
        bool got = notifyGet(0, &nit);
        if (got && !notifyLight && !notifyCardActive) {   // 卡片显示中不重复弹
            buzz(50);
            notifyCardPrevPage = currentPage;
            drawNotifyCard(nit);
            notifyCardActive = true;
            notifyCardUntil = millis() + 3000;
            Serial.println("[NOTIFY] Card ON 3s (screen on)");
        } else {
            if (sysMode == MODE_APPS) {
            // 若已在主页/通知页：直接重绘页面，让新消息立即显示出来
                if (currentPage == PAGE_HOME) drawMainScreen();
                else if (currentPage == PAGE_NOTIFY) drawPageNotify();  // 通知页也要实时刷新
            }
        }
    }

    // ===== 通知卡片显示中：统一处理（息屏卡 notifyLight / 亮屏卡 notifyCardActive）=====
    // 到期：息屏卡 → 自动灭屏；亮屏卡 → 恢复显示卡片前的原页面
    // 未到期可操作：双击 → 进通知页；息屏卡左滑 → 回表盘；亮屏卡右滑 → 去掉卡片返回
    if (notifyLight || notifyCardActive) {
        bool isSleepCard = notifyLight;
        unsigned long cardUntil = isSleepCard ? notifyUntil : notifyCardUntil;
        if (now >= cardUntil) {
            // 到期：息屏卡灭屏 / 亮屏卡恢复原页面
            notifyLight = false;
            notifyCardActive = false;
            if (isSleepCard) {
                prevPageBeforeSleep = (sysMode == MODE_APPS) ? currentPage : -1;   // v29j: 记录息屏前页面
                prevModeBeforeSleep = sysMode;
                screenOff();
                Serial.println("[PWR] Notify card timeout -> OFF");
            } else {
                if (sysMode == MODE_WATCHFACE) { tft.unloadFont(); tft.fillScreen(TFT_BLACK); wf_invalidateCache(); }
                else { currentPage = notifyCardPrevPage; tft.loadFont(font_vlw); drawMainScreen(); }
            }
        } else {
            // 卡片显示期间触摸：双击进通知页 / 左边缘右滑返回表盘
            if (touchAvailable) {
                uint16_t ctx, cty;
                int cg = getGesture(&ctx, &cty);
                if (cg == GESTURE_DTAP) {
                    // 双击 → 进入通知页（两种卡片相同）
                    buzz(30);
                    notifyLight = false; notifyCardActive = false;
                    sysMode = MODE_APPS;
                    currentPage = PAGE_NOTIFY;
                    notifyMarkRead();
                    screenOn();
                    tft.loadFont(font_vlw);
                    drawMainScreen();
                    Serial.println("[NOTIFY] Card -> Notify page");
                    delay(20);
                    return;
                } else if (isSleepCard && cg == GESTURE_SWIPE_L) {
                    // 息屏卡：右边缘左滑（用户称"屏幕边缘右滑"）→ 返回表盘
                    buzz(30);
                    notifyLight = false; notifyCardActive = false;
                    sysMode = MODE_WATCHFACE;
                    screenOn();
                    tft.unloadFont(); tft.fillScreen(TFT_BLACK); wf_invalidateCache();
                    Serial.println("[NOTIFY] Card -> Watchface");
                    delay(20);
                    return;
                } else if (!isSleepCard && cg == GESTURE_SWIPE_R) {
                    // 亮屏卡：左边缘右滑（用户称"屏幕边缘左滑"）→ 去掉卡片，回到当前页面
                    buzz(30);
                    notifyLight = false; notifyCardActive = false;
                    currentPage = notifyCardPrevPage;
                    tft.loadFont(font_vlw);
                    drawMainScreen();
                    Serial.println("[NOTIFY] Card dismissed -> back");
                    delay(20);
                    return;
                }
                // 其他手势忽略，卡片继续显示到到期
            }
            // 卡片显示中保持 MQTT 维护，不进入页面分支（避免重绘覆盖卡片）
            if (wifiOk) {
                if (mqtt.connected()) { mqtt.loop(); }
                else { connectMQTT(); }
            }
            delay(20);
            return;
        }
    }

    // ====== 模式A：全屏表盘 ======
    // NTP 已成功校时（时间戳>10亿）就渲染真实时间；否则用软件秒计数兜底（表盘永不空白）
    if (sysMode == MODE_WATCHFACE) {
        time_t ntpNow; time(&ntpNow);
        struct tm* ti = localtime(&ntpNow);
        if (ti && ntpNow > 1000000000) {
            wf_render(&tft, ti->tm_hour, ti->tm_min, ti->tm_sec);
            // wf_render：按当前"表盘样式 + 是否启用背景图"绘制整个表盘（时间/日期/星期/传感器小字）
        } else {
            int totalSec = seconds/2;
            wf_render(&tft, (totalSec/3600)%24, (totalSec%3600)/60, totalSec%60);
        }

        // 手势: 右→左滑动或点击 → 进入主界面


        if (touchAvailable) {
            uint16_t tx, ty;
            int g = getGesture(&tx, &ty);
            // 表盘手势：
            // 左滑 = 进入主界面；若刚从息屏唤醒（记忆了息屏前页面），第一次左滑只返回那个页面
            // 顶部下滑 = 呼出下拉状态面板
            if (g == GESTURE_SWIPE_L) {
                buzz(30);
                if (prevModeBeforeSleep == MODE_APPS && prevPageBeforeSleep >= 0) {
                    // v29j: 唤醒后第一次左滑 → 返回息屏前页面（一次性）
                    currentPage = prevPageBeforeSleep;
                    sysMode = MODE_APPS;
                    prevPageBeforeSleep = -1;
                    prevModeBeforeSleep = -1;
                } else {
                    currentPage = 0; sysMode = MODE_APPS;
                    // 普通左滑：进入主页（板块 0）
                    // v29j 唤醒记忆：左滑返回息屏前页面后记忆清零，下次左滑就是正常进主页
                }
                tft.loadFont(font_vlw);
                drawMainScreen();
            } else if (g == GESTURE_SWIPE_D) {
                // 顶部下滑 → 呼出下拉状态面板
                pullDownFromWatchface = true; pullDownActive = true; buzz(30);
                drawPullDown();
            }
        }
        delay(50); return;
    }
    // ====== 模式B：应用页面 ======
    // 每 500ms 做一次"局部刷新"：状态栏变化才重绘、健康页收到新数据才刷新数值，避免整页重绘闪烁
    static unsigned long lastRefresh = 0;
    if (now - lastRefresh >= 500) {
        lastRefresh = now;
        if (currentPage != PAGE_WFSEL && currentPage != PAGE_CLOCK) {
            // 状态栏：仅在 WiFi/BLE/充电/电量 变化时刷新
            static bool lastWifi=false, lastBle=false, lastChg=false;
            static int  lastBat=-1;
            bool chg = isCharging();
            int  bat = getBatteryPercent();
            if (wifiOk!=lastWifi || bleConnected!=lastBle || chg!=lastChg || bat!=lastBat) {
                lastWifi=wifiOk; lastBle=bleConnected; lastChg=chg; lastBat=bat;
                refreshStatusBar();
                // 状态栏：仅 WiFi/BLE/充电/电量 任一变化才重绘（静态数据不浪费绘制）
            }
            // 健康页：仅在收到 C3 数据时刷新数值区
            if (currentPage == PAGE_HEALTH && healthDirty) {
                healthDirty = false;
                refreshPageHealth();
                // 健康页：收到 C3 新数据（healthDirty）才刷新数值区，数据没变不重绘
            }
        }
    }

    // ===== WiFi 保活（每 3 秒检查一次）=====
    // 掉线自动重连：最多等 6 秒（30×200ms）
    static unsigned long lastWifi = 0;
    if (now - lastWifi >= 3000) {
        lastWifi = now;
        if (wifiOk && WiFi.status()!=WL_CONNECTED) { wifiOk=false; Serial.println("[WiFi] Disconnected!"); }
        if (!wifiOk) {
            WiFi.reconnect();
            for (int i=0;i<30&&WiFi.status()!=WL_CONNECTED;i++) delay(200);
            if (WiFi.status()==WL_CONNECTED) { wifiOk=true; Serial.println("[WiFi] Reconnected!"); }
        }
    }

    if (wifiOk) {
        // 天气定时刷新（IP自动定位，30分钟间隔）


        // ===== 联网功能（WiFi 可用时才执行）=====
        // 天气：到 30 分钟刷新间隔就重拉（IP 自动定位城市）；正在天气页则重绘显示新数据
        if (weather_needRefresh()) { weather_fetch(); if (currentPage == PAGE_WEATHER) drawMainScreen(); }
        // MQTT 保活：每 5 秒 loop 处理收发 + 处理待下载背景图；MQTT 掉线自动重连
        // 每 40 秒周期上报一次传感器数据（温度/湿度/气压/心率/血氧/UV/步数等）
        static unsigned long lastMqtt = 0;
        if (now - lastMqtt >= 5000) {
            lastMqtt = now;
            wallpaperProcessPending();  // 表盘背景：处理待下载图片
            if (mqtt.connected()) {
                mqtt.loop();
                static unsigned long lastPub = 0;
                if (mqttOk && now-lastPub>=40000) { lastPub=now; publishSensorData(); }
            } else { connectMQTT(); }
        }
    }

    // 手势: 左→右滑动 → 回表盘 / 点击 → 处理板块
    // 手势: 左→右滑动 → 回表盘 / 点击 → 处理板块

    // 表盘选择页防抖变量
    static unsigned long lastWFSwipeTime = 0;

    // 背景图 3s 小卡片过期：自动重绘清卡
    if (currentPage == PAGE_WFSEL && wfTabCardExpired()) {
        drawMainScreen();
    }

    // 表盘/背景图板块：非边缘滑动切换（表盘 tab 切表盘, 背景图 tab 切槽）
    // 防抖：一次长滑可能连续触发多次回调，500ms 内只响应第一次
    // 背景图方向与表盘一致：左滑=下一个，右滑=上一个；严格按上传时间 1/3→2/3→3/3 循环
    // （新上传的图挤掉最旧图，排序始终保持时间顺序）
    if (currentPage == PAGE_WFSEL) {
        int sw = detectSwipe();
        // v29x: 手势防抖 - detectSwipe 一次长滑可能连续多次返回, 500ms 内只响应一次
        if (sw != 0 && (now - lastWFSwipeTime >= 500)) {
            if (wfGetTab() == 0) {
                if (sw == 1) wf_prev(); else wf_next();
            } else {
                // v29y: 方向与表盘 tab 一致(左滑=下一个, 右滑=上一个); detectSwipe 左滑=-1, 故取反
                wfBgCycle(-sw);
            }
            drawMainScreen();
            lastWFSwipeTime = millis();
        }
    }

    // 时钟板块：非边缘滑动切子页 + 秒表/计时器实时刷新 + 闹钟滚动
    if (currentPage == PAGE_CLOCK) {
        clock_handleLoop(&tft);
        // 时钟板块内部循环：秒表/计时器实时刷新、闹钟列表滚动、左右滑切子页
    }

    // 运动板块 UI 局部刷新（步数累计已移到 loop 开头全局调用，任何页面/息屏都持续计步）
    if (currentPage == PAGE_SPORT && sportDirty) { sportDirty = false; refreshPageSport(); }
    // 运动页局部刷新：步数/距离/卡路里变化（sportDirty）才重绘数值区

    if (touchAvailable) {
        uint16_t tx, ty;
        // ===== 应用页手势处理 =====
        // 顶部下滑 = 呼出下拉面板
        // 右滑(左边缘) = 返回上一级：非主页回主页，主页则回表盘
        // 双击 = 表盘/背景图板块：表盘 tab 返回主页；背景图 tab 启用/关闭当前背景
        // 单击 = 交给 handleTouch 处理具体板块点击
        int g = getGesture(&tx, &ty);
        if (g == GESTURE_SWIPE_D) {
            // 顶部下滑 → 呼出下拉状态面板
            pullDownFromWatchface = false; pullDownActive = true; buzz(30);
            drawPullDown();
        } else if (g == GESTURE_SWIPE_R) {
            // 左边缘右滑 = 返回上一级
            if (currentPage != PAGE_HOME) {
                buzz(30);
                currentPage = PAGE_HOME;
                drawMainScreen();
            } else {
                buzz(30); tft.unloadFont(); tft.fillScreen(TFT_BLACK); wf_invalidateCache(); sysMode = MODE_WATCHFACE;
            }
        } else if (g == GESTURE_DTAP) {
            // 表盘/背景图板块：表盘 tab 双击锁定返回主页; 背景图 tab 双击启用/关闭
            if (currentPage == PAGE_WFSEL) {
                buzz(30);
                if (wfGetTab() == 0) {
                    currentPage = PAGE_HOME;
                    drawMainScreen();
                } else {
                    wfBgToggle();
                    // 背景图 tab 双击：启用/关闭当前背景图（开关写入 NVS，下次开机保留）
                    drawMainScreen();
                }
            }
        } else if (g == GESTURE_TAP) {
            int prevPage = currentPage;
            handleTouch(tx, ty);
            // 单击：交给 ui 层 handleTouch 判断点中了哪个板块/按钮并响应
            // 如果刚进入表盘选择页，设置防抖时间避免立即退出
            if (prevPage != PAGE_WFSEL && currentPage == PAGE_WFSEL) {
            // 刚进入表盘选择页：重置滑动防抖计时，避免残留手势把它立刻切走
                lastWFSwipeTime = millis();
            }
        }
    }
}

