#ifndef MAX30102_H
#define MAX30102_H

#include <Arduino.h>
#include <MAX30105.h>
#include <spo2_algorithm.h>

/*
 * MAX30102Sensor 类 — 心率血氧传感器
 * =====================================
 * 功能：采集 PPG 光电容积脉搏波信号，使用 Maxim 官方算法计算心率和血氧
 * I2C 地址：0x57
 *
 * 算法流程（重要——答辩可讲）：
 *   1. 传感器以 50Hz 连续采样红外(IR)和红光(Red)
 *   2. 每次 FIFO 有数据时，采集到 100 个样本的缓冲区
 *   3. 满 100 样本后，调用 maxim_heart_rate_and_oxygen_saturation()
 *      官方算法进行 FFT 分析
 *   4. 心率做 3 次中值平滑（过滤异常单次值）
 *   5. 手指移开超过约 2 秒自动重置
 *
 * 使用说明：
 *   MAX30102Sensor heart;     // 创建对象
 *   heart.begin();             // 初始化
 *   heart.update();            // 必须在 loop() 中频繁调用（采集样本）
 *   float bpm = heart.readHeartRate();  // 读取心率
 *   float spo2 = heart.readSpO2();      // 读取血氧
 */
class MAX30102Sensor {
public:
    MAX30102Sensor() { reset(); }

    // 初始化 MAX30102，配置采样参数
    bool begin() {
        // 先检测 I2C 是否有设备响应
        Wire.beginTransmission(0x57);
        if (Wire.endTransmission() != 0) {
            Serial.println("[MAX30102] I2C no response (0x57)");
            _ready = false;
            return false;
        }
        if (_sensor.begin(Wire, 0x57)) {
            _sensor.softReset();
            delay(100);
            _sensor.begin(Wire, 0x57);
            
            // 配置参数：50Hz 采样 + 平均滤波，防止 FIFO 溢出
            _sensor.setup(0x1F, 1, 2, 50, 411, 4096);
            _sensor.enableFIFORollover();     // FIFO 溢出时覆盖旧数据
            _sensor.setPulseAmplitudeRed(0x1F);
            _sensor.setPulseAmplitudeIR(0x1F);
            _sensor.setPulseAmplitudeGreen(0);
            
            delay(500);
            // 排空启动时的脏数据
            while (_sensor.available()) _sensor.getIR();
            Serial.println("[MAX30102] init OK");
            _ready = true;
        } else {
            Serial.println("[MAX30102] init FAIL");
        }
        return _ready;
    }

    // 必须在 loop() 中频繁调用——驱动 MAX30102 的数据采集和算法计算
    void update() {
        if (!_ready) return;

        _sensor.check();  // 驱动传感器内部状态机

        // 循环读空 FIFO 中的所有样本
        while (_sensor.available()) {
            uint32_t irVal   = _sensor.getIR();    // 红外值（用于心率）
            uint32_t redVal  = _sensor.getRed();   // 红光值（用于血氧）
            _sensor.nextSample();

            _lastRawIR = irVal;

            // 手指检测：IR 值在 5000~200000 之间认为有手指按压
            if (irVal > 5000 && irVal < 200000) {
                _noFingerCount = 0;
                if (!_finger) {
                    _finger = true;
                    _sampleCount = 0;  // 新手指按下，重新开始采集
                }

                // 采集 100 个样本到缓冲区
                if (_sampleCount < SAMPLE_SIZE) {
                    _irBuffer[_sampleCount]  = irVal;
                    _redBuffer[_sampleCount] = redVal;
                    _sampleCount++;

                    // 满 100 样本后运行 Maxim 官方算法
                    if (_sampleCount >= SAMPLE_SIZE) {
                        maxim_heart_rate_and_oxygen_saturation(
                            _irBuffer, SAMPLE_SIZE, _redBuffer,
                            &_spo2, &_validSPO2, &_heartRate, &_validHR
                        );
                        if (_validHR) {
                            // 3 次中值平滑，防止心率突变
                            _hrHistory[_hrIdx] = _heartRate;
                            _hrIdx = (_hrIdx + 1) % 3;
                            _hrCount = min(_hrCount + 1, 3);

                            // 冒泡排序取中值
                            int vals[3];
                            memcpy(vals, _hrHistory, sizeof(vals));
                            if (vals[0] > vals[1]) { int t=vals[0]; vals[0]=vals[1]; vals[1]=t; }
                            if (vals[1] > vals[2]) { int t=vals[1]; vals[1]=vals[2]; vals[2]=t; }
                            if (vals[0] > vals[1]) { int t=vals[0]; vals[0]=vals[1]; vals[1]=t; }
                            _bpm = (float)vals[1];  // 取中值
                        }
                        _sampleCount = 0;
                        _newResult = true;
                    }
                }
            } else {
                // 手指移开：累计空闲帧数
                _noFingerCount++;
                if (_noFingerCount > 100) {  // 约 2 秒无手指
                    _finger = false;
                    _bpm = 0;
                    _spo2 = 0;
                    _validSPO2 = 0;
                    _validHR = 0;
                    _heartRate = 0;
                    _sampleCount = 0;
                    _hrCount = 0;
                    _hrIdx = 0;
                }
            }
        }
    }

    // 读取心率（bpm）
    float readHeartRate() const { return _bpm; }
    
    // 读取血氧饱和度（百分比）
    float readSpO2() const { return (_validSPO2 > 0) ? (float)_spo2 : 0.0f; }
    
    bool  isReady() const { return _ready; }           // 传感器是否就绪
    long  readRawIR() const { return _lastRawIR; }     // 获取最近一次 IR 原始值
    int8_t  hrValid() const { return _validHR; }       // 心率数据是否有效
    int8_t  spo2Valid() const { return _validSPO2; }   // 血氧数据是否有效

private:
    static const int SAMPLE_SIZE = 100;  // 算法需要的每轮采样数

    // 重置所有内部状态
    void reset() {
        _ready = false; _bpm = 0; _spo2 = 0; _finger = false;
        _lastRawIR = 0; _sampleCount = 0; _noFingerCount = 0;
        _validSPO2 = 0; _validHR = 0; _heartRate = 0;
        _newResult = false; _hrCount = 0; _hrIdx = 0;
        memset(_hrHistory, 0, sizeof(_hrHistory));
    }

    MAX30105 _sensor;           // SparkFun MAX30105 库对象
    bool  _ready;               // 初始化标志
    float _bpm;                 // 心率值
    bool  _finger;              // 是否有手指按压
    long  _lastRawIR;           // 最近一次 IR 原始值
    int   _sampleCount;         // 当前缓冲区的样本数
    int   _noFingerCount;       // 无手指的连续帧计数
    bool  _newResult;           // 是否有新的算法结果

    uint32_t _irBuffer[SAMPLE_SIZE];   // 红外样本缓冲区
    uint32_t _redBuffer[SAMPLE_SIZE];  // 红光样本缓冲区

    int32_t _spo2, _heartRate;         // 算法输出
    int8_t  _validSPO2, _validHR;      // 算法输出的有效性标志

    // 心率平滑用的 3 次中值过滤器
    int _hrHistory[3];
    int _hrIdx;
    int _hrCount;
};

#endif
