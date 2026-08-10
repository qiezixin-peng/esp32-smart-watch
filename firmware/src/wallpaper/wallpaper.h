#ifndef WALLPAPER_MODULE_H
#define WALLPAPER_MODULE_H
/* ==========================================================
 * 表盘背景模块 — 接收 MQTT 下发的图片（base64）并显示
 * ==========================================================
 * 链路（v2）：小程序 → 云存储 → 云函数 sendWallpaper（下载+base64）
 *            → ThingsCloud MQTT 下发 {"b64":"..."} → 本模块解码存 LittleFS
 * 绕开 ESP32 直连 HTTPS 的证书问题（腾讯云 EdgeOne 对 mbedTLS 协商异常）
 *
 * v29o: 升级为 3 槽背景图队列（FIFO）
 *   - LittleFS: /wallpaper_0.jpg /wallpaper_1.jpg /wallpaper_2.jpg
 *   - 上传第 4 张时自动删除最老的一张（按 NVS 递增序号排序）
 *   - NVS 键（命名空间 twatch）:
 *       bg_seq    u32 全局收图序号（每收 1 张 +1）
 *       bg_seq0~2 u32 各槽收图序号（0 = 空槽）
 *       bg_active i8  启用槽（-1 = 关闭背景图, 0~2 = 启用某槽）
 *   - 数字表盘只画「当前启用」的槽；关闭时 wallpaperHas()=false → 黑底
 * ========================================================== */
#include <Arduino.h>
#include <TFT_eSPI.h>

#define WALLPAPER_SLOTS 3

// 初始化：挂载 LittleFS + 读取 NVS 槽状态 + 注册 JPEG 解码回调
void wallpaperInit();

// MQTT 收到图片 payload（base64 JSON）时调用（只保存，不阻塞）
void wallpaperSetPayload(const byte* payload, unsigned int len);

// loop 中周期调用：处理待解码图片（失败自动重试，最多 3 次）
void wallpaperProcessPending();

// 是否已有「当前启用」的背景图（数字表盘据此决定画不画背景）
bool wallpaperHas();

// 全屏绘制「当前启用」的背景图（JPEG 解码；无启用则无操作）
void wallpaperDraw(TFT_eSPI* tft);

// 全屏绘制指定槽的背景图（板块预览用，不改变启用状态）
void wallpaperDrawSlot(TFT_eSPI* tft, int slot);

// 某槽是否有图
bool wallpaperSlotExists(int slot);

// 有效槽数量（0~3）
int wallpaperSlotCount();

// v29x: 预览顺序 = 上传时间顺序。显示序号 idx（0=最早上传, 最后=最新上传）
//       返回对应的物理槽位；越界/空返回 -1。上传第 4 张覆盖最老后自动重排。
int wallpaperOrderedSlot(int idx);

// 当前启用槽（-1 = 关闭）
int wallpaperGetActive();

// 设置启用槽（-1 = 关闭），写 NVS；越界/空槽忽略
void wallpaperSetActive(int slot);

// 背景平均亮度（0-255，用于智能前景色；无背景时保留上次值）
uint8_t wallpaperGetBrightness();

// 背景图是否已成功解码过（表盘据此决定是否可分配快照; 黑底模式零快照）
bool wallpaperDecodedOk();

/* v29m: 解码时捕获文字区像素快照（替代 readRect —— ST7789 无 MISO 读不了屏, 读回全 0 会变黑框）
 * 用法: 调用 wallpaperDraw 前逐槽 wallpaperSnapSet 注册(buf 由调用方分配/释放),
 *       解码后 wallpaperSnapCaptured(slot)==true 表示该区像素已填满。 */
void wallpaperSnapSet(int slot, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* buf);
void wallpaperSnapReset();
bool wallpaperSnapCaptured(int slot);

#endif
