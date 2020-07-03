#include <Arduino.h>
#include <M5StickC.h>
#include <MIDI.h>
#include <stdio.h> 


// Headers for ESP32 BLE
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
//#include "BLEScan.h"

#define SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

BLECharacteristic *pCharacteristic;

static boolean deviceConnected = false;

const int side_button = 39;
const int front_button = 37;

float accX = 0;
float accY = 0;
float accZ = 0;

float gyroX = 0;
float gyroY = 0;
float gyroZ = 0;

uint8_t midiPacket[] = {
   0x80,  // header
   0x80,  // timestamp, not implemented 
   0x00,  // status
   0x3c,  // 0x3c == 60 == middle c
   0x00   // velocity
};

void update_screen(const char* x){
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 10);
  M5.Lcd.printf(x);
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      update_screen("BLE Connected!");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};


void setup() {
  M5.begin();
  Serial.begin(115200); //if before M5 init, seems to not work?
  M5.MPU6886.Init();

  BLEDevice::init("big buttholes");

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID));

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
    BLEUUID(CHARACTERISTIC_UUID),
    BLECharacteristic::PROPERTY_READ   |
    BLECharacteristic::PROPERTY_WRITE  |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_WRITE_NR
  );

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->start();

  pinMode(side_button, INPUT_PULLUP);
  pinMode(front_button, INPUT);

  // text print
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 10);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.printf("Display Test!");
  delay(500);

  // put your setup code here, to run once:
}

void loop() {
  M5.MPU6886.getGyroData(&gyroX,&gyroY,&gyroZ);
  M5.MPU6886.getAccelData(&accX,&accY,&accZ);
  
  char gx[64];
  char gy[64];
  char gz[64];

  gcvt(gyroX, 4, gx);
  gcvt(gyroY, 4, gy);
  gcvt(gyroZ, 4, gz);

  char combined_g[64];
  strcpy(combined_g, "Gyro: "); /* copy name into the new var */
  strcat(combined_g, gx); 
  strcat(combined_g, ", "); 
  strcat(combined_g, gy);
  strcat(combined_g, ", ");
  strcat(combined_g, gz);
  
  char ax[64];
  char ay[64];
  char az[64];

  gcvt(accX, 4, ax);
  gcvt(accY, 4, ay);
  gcvt(accZ, 4, az);

  char combined_a[64];
  strcpy(combined_a, "Accel: "); /* copy name into the new var */
  strcat(combined_a, ax); 
  strcat(combined_a, ", "); 
  strcat(combined_a, ay);
  strcat(combined_a, ", ");
  strcat(combined_a, az);

  if (deviceConnected) {
   // note down
   midiPacket[2] = 0x90; // note down, channel 0
   midiPacket[3] = 0x3c; // note down, channel 0
   midiPacket[4] = 127;  // velocity
   pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
   pCharacteristic->notify();

   // play note for 500ms
   delay(500);

   // note up
   midiPacket[2] = 0x20; // note up, channel 0
   midiPacket[4] = 0;    // velocity
   pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes)
   pCharacteristic->notify();

   delay(500);

      // note down
   midiPacket[2] = 0x90; // note down, channel 0
  //  midiPacket[3] = 0x03; // note down, channel 0
   midiPacket[3] = 4;
   midiPacket[4] = 127;  // velocity
   pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
   pCharacteristic->notify();

   // play note for 500ms
   delay(500);

   // note up
   midiPacket[2] = 0x20; // note up, channel 0
   midiPacket[4] = 0;    // velocity
   pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes)
   pCharacteristic->notify();

   delay(500);
  }

  if (!digitalRead(side_button)){
    update_screen(combined_g);
    // Serial.println(combined_g);
  }
  if (!digitalRead(front_button)){
    update_screen(combined_a);
    // Serial.println(inc);
  }

  delay(100);
}