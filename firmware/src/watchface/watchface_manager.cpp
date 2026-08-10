/*
 * 表盘管理器实现：7 款表盘（模拟/数字）注册、切换、记录当前选中
 * 注：现实际注册 6 款（翻页钟 v29r 已移除），以 faces 数组为准
 */

#include "watchface_manager.h"

// 全局标志：表盘选择页的"紧凑预览"模式（只画表盘本体，不叠加背景图）
bool wf_compactMode = false;
// 全局标志：是否为切盘后的第一帧（首帧整屏重绘盘面，后续帧只局部刷新）
bool wf_firstFrame = true;
#include <Preferences.h>

extern WatchFace wf_analog_classic;
extern WatchFace wf_analog_dark;
extern WatchFace wf_minimal_classic;
extern WatchFace wf_seven_seg;
extern WatchFace wf_dot_matrix;
extern WatchFace wf_ring;

// 表盘注册表：数组顺序 = 选择页展示顺序（共 6 款：经典白盘/深色简约/简约居中/七段数码管/点阵像素/环形轨道）
static WatchFace* faces[] = {
    &wf_analog_classic,
    &wf_analog_dark,
    &wf_minimal_classic,
    &wf_seven_seg,
    &wf_dot_matrix,
    &wf_ring
};
// 表盘总数：由数组长度自动计算，避免手写数字出错
static const int FACE_COUNT = sizeof(faces) / sizeof(faces[0]);
// 当前选中的表盘编号（0=第 1 款，掉电后从 NVS 恢复）
static int currentFace = 0;
// 上次渲染的时分秒缓存：时间没变就不重绘（省电）
static int lastH = -1, lastM = -1, lastS = -1;

// 作用：初始化表盘管理器（注册 6 款表盘）
void wf_init() {
    // 打开 NVS 存储（只读模式 true：命名空间不存在也不报错）
    Preferences prefs;
    if (!prefs.begin("twatch", true)) {  // 命名空间不存在也继续
        // 首次开机无记录：默认选第 0 款
        currentFace = 0;
    } else {
        // 读回上次保存的表盘编号
        currentFace = prefs.getInt("watchface", 0);
        prefs.end();
    }
    // 越界保护：异常编号强制回第 0 款
    if (currentFace < 0 || currentFace >= FACE_COUNT) currentFace = 0;
    if (faces[currentFace]->init) faces[currentFace]->init();
    // 清空渲染缓存 + 标记首帧：切盘后强制整屏重绘
    lastH = lastM = lastS = -1;
    wf_firstFrame = true;
}

// 作用：返回表盘总数（选择页循环使用）
int wf_getCount() { return FACE_COUNT; }
// 作用：取第 index 款表盘的显示名称
const char* wf_getName(int index) {
    if (index < 0 || index >= FACE_COUNT) return "未知";
    return faces[index]->name;
}
// 作用：返回当前选中的表盘编号
int wf_getCurrent() { return currentFace; }

// 作用：切换当前表盘
void wf_setCurrent(int index) {
    if (index < 0 || index >= FACE_COUNT) return;
    currentFace = index;
    lastH = lastM = lastS = -1;
    wf_firstFrame = true;
    if (faces[currentFace]->init) faces[currentFace]->init();
    Preferences prefs;
    if (prefs.begin("twatch", false)) {
        prefs.putInt("watchface", currentFace);
        prefs.end();
    }
}

// 作用：切换到下一款（到尾自动循环回第 0 款）
void wf_next() { wf_setCurrent((currentFace + 1) % FACE_COUNT); }
// 作用：切换到上一款（到头自动循环到最后一款）
void wf_prev() { wf_setCurrent((currentFace - 1 + FACE_COUNT) % FACE_COUNT); }

// 作用：强制下一帧整屏重绘（切换表盘/背景图后调用）
void wf_invalidateCache() { lastH = lastM = lastS = -1; wf_firstFrame = true; }

// 作用：渲染当前选中的表盘
void wf_render(TFT_eSPI* tft, int h, int m, int s) {
    // 时间没变：直接跳过重绘（省 CPU/省电）
    if (h == lastH && m == lastM && s == lastS) return;
    // 记录本次时间，供下次比对
    lastH = h; lastM = m; lastS = s;
    // 调当前表盘的渲染回调（若已注册）
    if (currentFace >= 0 && currentFace < FACE_COUNT && faces[currentFace]->render)
        faces[currentFace]->render(tft, h, m, s);
}


