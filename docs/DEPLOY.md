# 部署教程：从零复刻本项目

> 目标：让一台全新的设备 + 一套新账号，按本文档一步步配置后，完整跑起整个系统。
> 约定：文中 `YOUR_*` 均为占位符，代表你在对应平台注册/创建后拿到的真实值。

---

## 第 0 步：硬件清单

| 硬件 | 说明 | 参考采购 |
|---|---|---|
| TTGO T-Watch V3 | 主手表（ESP32 + 1.54 寸 240×240 屏 + AXP202 + BMA423） | LilyGO 官方店 |
| ESP32-C3 SuperMini | 传感器手环主控 | 淘宝/立创 |
| BME280 模块 | 温度/湿度/气压（I2C 地址 0x76） | 淘宝 |
| MAX30102 模块 | 心率/血氧（I2C） | 淘宝 |
| LTR390 模块 | 紫外线（I2C 地址 0x53） | 淘宝 |

C3 与三传感器共用 I2C 总线：SDA=GPIO5、SCL=GPIO6。

---

## 第 1 步：注册账号（全部免费额度够用）

| 平台 | 用途 | 注册地址 |
|---|---|---|
| ThingsCloud | MQTT 云平台 + 应用端 API（存数据、下发通知/背景图） | thingscloud.xyz |
| 和风天气 | 天气 API（当前天气/3日预报/IP定位） | qweather.com |
| 微信公众平台 | 小程序 AppID + 云开发环境 | mp.weixin.qq.com |

---

## 第 2 步：C3 传感器手环固件（`c3-sensor/`）

1. 安装 [VS Code](https://code.visualstudio.com/) + [PlatformIO 插件](https://platformio.org/)
2. 用 PlatformIO 打开 `c3-sensor/` 文件夹
3. 连接 C3 开发板，点击「Upload」烧录
4. 打开串口监视器（115200）：应看到三个传感器初始化成功 + 每 2 秒一条 JSON 输出
5. 记录 C3 的 BLE 广播名（代码中为 `C3-Sensor`，可在 `src/ble/ble_server.cpp` 修改）——手表靠它连接

> 本固件**不需要任何账号配置**，烧录即用。

---

## 第 3 步：ThingsCloud 平台配置

1. 注册 ThingsCloud → 创建项目 → 添加设备（类型选"其他/自定义"）
2. 记录设备证书里的三个值：
   - **Client ID / AccessToken**（同一串字符）
   - **Username / Project-Key**
   - **Password**
   - 以及 MQTT 接入点域名（如 `gz-3-mqtt.iot-api.com`）和应用端 API 接入点（如 `gz-3-api.iot-api.com`）
3. （可选）在设备属性中定义 `temp/hum/press/bpm/spo2/uv/rssi/uptime/steps/dist/cal` 等属性，用于控制台查看

---

## 第 4 步：手表固件（`firmware/`）

1. 打开 `firmware/src/main.cpp`，把网络配置段替换为你的真实值：

```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";        // 你的 WiFi 名
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";    // 你的 WiFi 密码
const char* MQTT_HOST = "你的 MQTT 接入点";        // 如 gz-3-mqtt.iot-api.com
const char* MQTT_CLIENT_ID = "YOUR_MQTT_CLIENT_ID"; // 设备证书 Client ID
const char* MQTT_USER = "YOUR_MQTT_USERNAME";      // 设备证书 Username
const char* MQTT_PASS = "YOUR_MQTT_PASSWORD";      // 设备证书 Password
```

2. 打开 `firmware/src/weather/weather.cpp`，填入和风天气 Key：

```cpp
#define QWEATHER_KEY "YOUR_QWEATHER_KEY"
```

3. PlatformIO 打开 `firmware/` → 连接 T-Watch → Upload 烧录
4. 串口（115200）应依次出现：WiFi OK → NTP OK → MQTT Connected → 天气获取成功 → 表盘显示
5. 若 C3 手环在身边，手表会自动搜索并连接 BLE，显示实时生命体征

---

## 第 5 步：微信小程序（`miniprogram/`）

1. **注册小程序**：微信公众平台 → 小程序 → 注册（个人主体即可）→ 拿到 AppID
2. **开通云开发**：微信开发者工具导入 `miniprogram/` 文件夹 → 工具栏「云开发」→ 开通环境（按量付费免费额度即可）→ 记录环境 ID
3. **填写配置**：
   - `project.config.json`：`"appid": "YOUR_APPID"` → 改成你的 AppID
   - `app.js`：`env: 'YOUR_CLOUD_ENV_ID'` → 改成你的环境 ID
   - `cloudfunctions/collectSensorData/index.js` 和 `cloudfunctions/sendWallpaper/index.js`：填入第 3 步的 ThingsCloud 凭据：

```js
const API_ENDPOINT = 'https://gz-3-api.iot-api.com'  // 你的应用端 API 接入点
const ACCESS_TOKEN = 'YOUR_ACCESS_TOKEN'              // 设备证书 AccessToken
const PROJECT_KEY  = 'YOUR_PROJECT_KEY'               // 设备证书 Project-Key
```

4. **部署云函数**（3 个）：
   - 右键 `cloudfunctions/collectSensorData` → 「上传并部署：云端安装依赖」
   - 右键 `cloudfunctions/sendWallpaper` → 同上
   - 右键 `cloudfunctions/sportReport` → 同上
5. **创建数据库集合**：云开发控制台 → 数据库 → 新建集合 `health_records`（权限：仅创建者可读写即可）
6. 编译预览：首页应显示实时数据；健康/运动报表可查看图表

> 说明：本项目刻意**关闭了云函数定时触发器**（`config.json` 中 `triggers: []`），改为打开小程序页面时才采集，避免烧 ThingsCloud 免费配额。若需自动采集，可在微信开发者工具单独配置定时触发器。

---

## 第 6 步：通知转发（可选，手机端）

1. 手机安装 [MacroDroid](https://play.google.com/store/apps/details?id=com.arlosoft.macrodroid)
2. 无障碍设置中开启 MacroDroid 服务（国产 ROM 注意允许后台运行/自启动，否则服务会被系统杀掉）
3. 配置宏：触发器 = 通知（微信）→ 动作 = HTTP 请求调用 ThingsCloud 下行接口，把微信通知内容推给手表
4. 手表 MQTT 收到下行消息 → 震动 + 弹通知

> 若通知收不到，排查顺序：① 手表 MQTT 是否在线 → ② 平台 API 直调下发是否成功 → ③ 手机 MacroDroid 无障碍服务是否存活（国产 ROM 高发故障点）。

---

## 常见问题

- **天气 HTTPS 报 SSL 内存不足**：ESP32 内存紧张时偶发，固件已有重试与降级（IP 定位失败则用默认城市），多试一次或减少同时打开的页面
- **MQTT Connect failed rc=-4**：先确认 WiFi 正常、凭据正确；平台配额用尽时也会静默拒绝连接（次日零点恢复）
- **手表卡死/黑屏**：多为堆内存不足，固件已做低内存保护（跳过解码/降亮度）；升级固件后重启即可
- **BMA423 抬手亮屏不灵敏**：确认烧录的是最新固件（软件倾角 + 芯片中断双通道），佩戴姿势为"手腕从自然下垂快速抬起"

## License

MIT
