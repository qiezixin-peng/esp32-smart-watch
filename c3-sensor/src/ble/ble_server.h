#ifndef BLE_SERVER_H
#define BLE_SERVER_H

/*
 * BLE 服务端模块
 * ==============
 * 功能：让 ESP32-C3 以 BLE Server 身份广播传感器数据
 * T-Watch（BLE Client）扫描并连接到 C3，通过 Notify 接收数据
 *
 * 使用流程：
 *   1. bleServerInit("设备名", "SERVICE_UUID", "CHAR_UUID") —— 初始化
 *   2. bleServerNotify("json字符串") —— 向已连接的客户端发送数据
 *   3. bleServerHandleDisconnections() —— 处理断线重连（loop() 中调用）
 *
 * 全局变量：
 *   bleDeviceConnected —— true 表示有客户端已连接
 */

extern bool bleDeviceConnected;
extern bool bleOldDeviceConnected;

void bleServerInit(const char* deviceName, const char* serviceUUID, const char* characteristicUUID);
void bleServerNotify(const char* data);
void bleServerHandleDisconnections();

#endif
