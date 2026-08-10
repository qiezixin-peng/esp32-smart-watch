/*
 * 手势检测实现：从触摸坐标序列识别 点击/双击/左滑/右滑，供各页面切换使用
 */

#include "touch.h"
/* 手势检测
 * 跟踪触摸起止位置，检测边缘滑动或点击
 * 从屏幕左边缘往右滑 = 返回表盘
 * 从屏幕右边缘往左滑 = 进入主界面
 * 屏幕中间点击区     = 板块交互
 */

// 手势状态
static uint16_t _gsStartX = 0, _gsStartY = 0;  // 触摸起点
static uint16_t _gsLastX  = 0, _gsLastY  = 0;  // 最后触摸点
static bool  _gsActive  = false;                // 正在触摸中
static unsigned long _gsStartMs = 0;            // 触摸开始时间
static bool  _gsMoved  = false;                 // 触摸期间是否发生过明显移动（防滚动误判点击）
static unsigned long _lastTapMs = 0;            // 上一次点击时间（用于双击检测）
static bool  _gsSwiped = false;                 // 滑动已触发：抑制松手误判为点击（防误触）
unsigned long gLastTouchMs = 0;                 // 最后触摸时间（毫秒，供自动息屏使用）

#define SWIPE_THRESHOLD 50   // 滑动最小距离（像素）
#define SWIPE_EDGE_R    180  // 右边缘起点阈值（x > 180 算右边缘）
#define SWIPE_EDGE_L    60   // 左边缘起点阈值（x < 60 算左边缘）

/* 
 * 获取手势
 * 返回 GESTURE_xxx
 * - GESTURE_SWIPE_L : 从右边缘向左滑动 → 进入主界面
 * - GESTURE_SWIPE_R : 从左边缘向右滑动 → 返回表盘
 * - GESTURE_TAP     : 在屏幕中间点击
 * - GESTURE_NONE    : 无有效手势
 */
// 作用：触摸状态机：返回 点击/双击/左滑/右滑 事件
int getGesture(uint16_t *tx, uint16_t *ty) {
    TouchPoint_t tp;

    if (readTouch(&tp)) {
        gLastTouchMs = millis();   // 任何触摸事件都刷新最后触摸时间（自动息屏用）
        if (_gsSwiped) return GESTURE_NONE;   // 滑动已触发：忽略后续按住，等手指抬起
        // === 有触摸事件 ===
        if (!_gsActive) {
            // 第一次触摸 → 记录起点
            _gsStartX = tp.x;
            _gsStartY = tp.y;
            _gsLastX  = tp.x;
            _gsLastY  = tp.y;
            _gsActive = true;
            _gsMoved  = false;
            _gsStartMs = millis();
        } else {
            // 持续触摸 → 更新位置，检测滑动
            _gsLastX = tp.x;
            _gsLastY = tp.y;

            // 任意方向移动超过 15px 视为拖动/滚动，松手时不再当作点击
            if (abs((int)tp.x - (int)_gsStartX) > 15 ||
                abs((int)tp.y - (int)_gsStartY) > 15) {
                _gsMoved = true;
            }

            int dx = (int)tp.x - (int)_gsStartX;  // 正=向右滑 负=向左滑
            int dy = abs((int)tp.y - (int)_gsStartY);
            int vdy = (int)tp.y - (int)_gsStartY; // 正=向下滑

            // 从屏幕顶部边缘往下滑 → 呼出下拉面板（垂直优先）
            if (_gsStartY < 40 && vdy > SWIPE_THRESHOLD && vdy > abs(dx) * 2) {
                _gsActive = false;
                _gsSwiped = true;    // 抑制松手误判点击
                return GESTURE_SWIPE_D;
            }

            // 仅当水平移动 > 阈值 且 远大于垂直移动时，才判定为滑动
            if (abs(dx) > SWIPE_THRESHOLD && abs(dx) > dy * 2) {
                // 同时检查起点是否在边缘
                if (dx < 0 && _gsStartX > SWIPE_EDGE_R) {
                    // 从右边缘向左滑 → 进入主界面
                    _gsActive = false;
                    _gsSwiped = true;    // 抑制松手误判点击
                    return GESTURE_SWIPE_L;
                }
                if (dx > 0 && _gsStartX < SWIPE_EDGE_L) {
                    // 从左边缘向右滑 → 返回表盘
                    _gsActive = false;
                    _gsSwiped = true;    // 抑制松手误判点击
                    return GESTURE_SWIPE_R;
                }
            }
        }
        return GESTURE_NONE;
    }

    // === 无触摸事件（手指已抬起） ===
    if (_gsSwiped) { _gsSwiped = false; return GESTURE_NONE; }  // 滑动后抬起：清标志，不判点击
    if (_gsActive) {
        // 手指抬起 → 计算是否为有效的边缘滑动
        _gsActive = false;

        // 检查是否在滑动中（起点到终点的总位移）
        int totalDx = (int)_gsLastX - (int)_gsStartX;
        int totalDy = abs((int)_gsLastY - (int)_gsStartY);

        // 从屏幕顶部边缘往下滑 → 呼出下拉面板（抬起时也判定）
        if (_gsStartY < 40 && totalDy > SWIPE_THRESHOLD && totalDy > abs(totalDx) * 2) {
            _gsActive = false;
            _gsSwiped = true;    // 采样抖动时保险
            return GESTURE_SWIPE_D;
        }

        if (abs(totalDx) > SWIPE_THRESHOLD && abs(totalDx) > totalDy * 2) {
            // 边缘滑动判定（抬起时也检查）
            if (totalDx < 0 && _gsStartX > SWIPE_EDGE_R) {
                _gsSwiped = true;    // 采样抖动时保险
                return GESTURE_SWIPE_L;
            }
            if (totalDx > 0 && _gsStartX < SWIPE_EDGE_L) {
                _gsSwiped = true;    // 采样抖动时保险
                return GESTURE_SWIPE_R;
            }
        }

        // 触摸期间发生过明显移动（滚动/拖动）→ 不当作点击，防止误触
        if (_gsMoved) return GESTURE_NONE;

        // 双击检测：两次点击间隔 < 400ms → 双击
        unsigned long _tNow = millis();
        if (_tNow - _lastTapMs < 400) { _lastTapMs = 0; return GESTURE_DTAP; }
        _lastTapMs = _tNow;

        // 不是边缘滑动 → 作为点击处理
        *tx = _gsLastX;
        *ty = _gsLastY;
        return GESTURE_TAP;
    }

    return GESTURE_NONE;
}

/* ===== 独立状态：非边缘滑动检测（用于表盘选择页） ===== */
static uint16_t _swSX = 0, _swSY = 0;   // 触摸起点
static uint16_t _swLX = 0, _swLY = 0;   // 最后触摸点
static bool _swActive = false;          // 正在触摸中

/*
 * 检测任意位置的横向滑动（不要求起点在边缘）
 * 返回:  1 = 右滑, -1 = 左滑, 0 = 无
 */
// 作用：检测左右滑动方向（左滑=下一个，右滑=上一个）
int detectSwipe() {
    TouchPoint_t tp;
    if (readTouch(&tp)) {
        if (!_swActive) {
            _swSX = tp.x; _swSY = tp.y;
            _swLX = tp.x; _swLY = tp.y;
            _swActive = true;
        } else {
            _swLX = tp.x; _swLY = tp.y;
            int dx = (int)tp.x - (int)_swSX;
            int dy = abs((int)tp.y - (int)_swSY);
            // 水平移动 > 阈值 且 远大于垂直移动
            if (abs(dx) > 50 && abs(dx) > dy * 2) {
                // 起点在屏幕边缘 → 让给 getGesture 做页面切换（这里不消费）
                if (_swSX > 180 || _swSX < 60) { _swActive = false; return 0; }
                _swActive = false;
                return (dx > 0) ? 1 : -1;
            }
        }
        return 0;
    }
    // 手指抬起
    if (_swActive) _swActive = false;
    return 0;
}