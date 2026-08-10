/* 
 * 按钮模块 — T-Watch V3 (中断 + 轮询)
 * 关键修复: 先禁掉所有非 PEK 的 IRQ，让 nIRQ 引脚恢复高电平
 */

#include "button.h"
#include "../power/power.h"

static bool btnInitialized = false;
static volatile bool irqTriggered = false;
static portMUX_TYPE irqMux = portMUX_INITIALIZER_UNLOCKED;
#define AXP202_INT 35

// 上次按键时间（去抖与双击窗口共用）
static unsigned long lastPressTime  = 0;
static unsigned long firstClickTime = 0;
// 是否在等待第二次点击（构成双击）
static bool waitingForDclick = false;
static const unsigned long DEBOUNCE_MS   = 350;
static const unsigned long DCLICK_WINDOW = 400;

// 作用：按键硬件中断（记录按下时间）
void IRAM_ATTR buttonISR() {
    portENTER_CRITICAL_ISR(&irqMux);
    irqTriggered = true;
    portEXIT_CRITICAL_ISR(&irqMux);
}

// 作用：清除 AXP 电源芯片中断标志
static void clearAXPIRQ() {
    writeAXP(0x47, 0xFF); writeAXP(0x49, 0xFF);
    writeAXP(0x4B, 0xFF); writeAXP(0x4D, 0xFF); writeAXP(0x4F, 0xFF);
}

// 作用：处理 AXP 中断事件（双击唤醒/按键）
static int processIRQ(unsigned long now) {
    uint8_t irq1 = readAXP(0x47);
    if (irq1 == 0) { clearAXPIRQ(); return BTN_NONE; }

    clearAXPIRQ();
    Serial.printf("[BTN_EVENT] 0x47=0x%02X\n", irq1);

    if (now - lastPressTime < DEBOUNCE_MS) return BTN_NONE;

    if (irq1 & 0x20) {
        if (waitingForDclick) {
            waitingForDclick = false; lastPressTime = now;
            return BTN_DCLICK;
        } else {
            waitingForDclick = true; firstClickTime = now; lastPressTime = now;
            return BTN_NONE;
        }
    }
    if (irq1 & 0x10) {
        waitingForDclick = false; lastPressTime = now;
        return BTN_HOLD;
    }
    return BTN_NONE;
}

// 作用：初始化按键引脚与中断
void btn_init(int pin) {
    (void)pin;

    // == 关键修复: 先禁掉所有非 PEK 的 IRQ 使能 ==
    // 0x4A (IRQ2 enable) 和 0x4C (IRQ3 enable) 有 VBUS/充电等 IRQ 在持续触发
    writeAXP(0x4A, 0x00);  // 关掉 IRQ2
    writeAXP(0x4C, 0x00);  // 关掉 IRQ3
    writeAXP(0x48, 0x30);  // 只开 PEK 短按+长按 IRQ
    clearAXPIRQ();          // 清除所有待处理 IRQ
    delay(10);              // 等 AXP202 更新引脚状态

    pinMode(AXP202_INT, INPUT);
    int gpio35 = digitalRead(AXP202_INT);
    Serial.printf("[BTN] GPIO35=%d (after init)\n", gpio35);

    if (gpio35 == 1) {
        attachInterrupt(digitalPinToInterrupt(AXP202_INT), buttonISR, FALLING);
        Serial.println("[BTN] IRQ attached @ GPIO35 FALLING");
    } else {
        // 试3次清除
        for (int i = 0; i < 3 && digitalRead(AXP202_INT) == 0; i++) {
            clearAXPIRQ();
            delay(10);
            Serial.printf("[BTN] Retry clear %d, GPIO35=%d\n", i+1, digitalRead(AXP202_INT));
        }
        if (digitalRead(AXP202_INT) == 1) {
            attachInterrupt(digitalPinToInterrupt(AXP202_INT), buttonISR, FALLING);
            Serial.println("[BTN] IRQ attached after retry");
        } else {
            Serial.println("[BTN] WARN: GPIO35 stuck LOW - button via IRQ won't work");
        }
    }
    btnInitialized = true;
}

// 作用：按键轮询：返回按键事件（按下/松开）
int btn_update() {
    if (!btnInitialized) return BTN_NONE;
    unsigned long now = millis();

    // 中断触发
    bool irqFlag = false;
    portENTER_CRITICAL(&irqMux);
    irqFlag = irqTriggered;
    irqTriggered = false;
    portEXIT_CRITICAL(&irqMux);
    if (irqFlag) { int ret = processIRQ(now); if (ret) return ret; }

    // 轮询后备 (每50ms)
    static unsigned long lastPoll = 0;
    if (now - lastPoll > 50) {
        lastPoll = now;
        if (digitalRead(AXP202_INT) == 0) {
            int ret = processIRQ(now);
            if (ret) return ret;
        }
    }

    // 双击超时
    if (waitingForDclick && (now - firstClickTime > DCLICK_WINDOW)) {
        waitingForDclick = false; lastPressTime = now;
        return BTN_CLICK;
    }
    return BTN_NONE;
}
