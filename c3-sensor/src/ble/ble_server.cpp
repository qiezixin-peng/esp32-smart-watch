#include <Arduino.h>
#include "ble_server.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

/*
 * BLE Server Implementation
 * ==========================
 * C3 acts as BLE Server advertising as "C3-Sensor",
 * T-Watch (Client) connects and receives JSON data via Notify.
 */

bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;

static BLEServer* pServer = nullptr;
static BLECharacteristic* pCharacteristic = nullptr;

/* Server callbacks */
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* srv) override {
        bleDeviceConnected = true;
        Serial.println("[BLE] client connected");
    }
    void onDisconnect(BLEServer* srv) override {
        bleDeviceConnected = false;
        Serial.println("[BLE] client disconnected");
    }
};

/* Initialize BLE server: create service, characteristic, start advertising */
void bleServerInit(const char* deviceName, const char* serviceUUID, const char* characteristicUUID) {
    BLEDevice::init(deviceName);                     // 1. Init BLE stack
    BLEDevice::setMTU(256);                          // 2. Set MTU (256 > 65-byte JSON payload)
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService* pService = pServer->createService(serviceUUID);
    pCharacteristic = pService->createCharacteristic(
        characteristicUUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    pCharacteristic->addDescriptor(new BLE2902());
    pService->start();

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(serviceUUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    BLEDevice::startAdvertising();

    Serial.printf("[BLE] advertising as %s\n", deviceName);
}

/* Send notification to connected client */
void bleServerNotify(const char* data) {
    if (bleDeviceConnected) {
        pCharacteristic->setValue(data);
        pCharacteristic->notify();
    }
}

/* Handle disconnection: restart advertising when client disconnects */
void bleServerHandleDisconnections() {
    if (!bleDeviceConnected && bleOldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("[BLE] restarting advertising...");
        bleOldDeviceConnected = false;
    }
    if (bleDeviceConnected && !bleOldDeviceConnected) {
        bleOldDeviceConnected = true;
    }
}
