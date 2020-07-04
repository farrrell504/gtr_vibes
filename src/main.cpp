#include <Arduino.h>
#include <M5StickC.h>
#include <MIDI.h>
#include <stdio.h> 
#include <math.h>

// Headers for ESP32 BLE
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// Toggle Using the Serial Monitor
#define SERIAL_TOGGLE true

// BLE Device Name
#define DEVICE_NAME "hairy_whistle"

// BLE MIDI Info
#define SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

BLECharacteristic *pCharacteristic;

static boolean deviceConnected = false;

// GPIOs for M5Stick-C Buttons
const int side_button = 39;
const int front_button = 37;

int sample_count = 0;

float accX = 0;
float accY = 0;
float accZ = 0;

float gyroX = 0;
float gyroY = 0;
float gyroZ = 0;

// averaging over gyro readings and feeding that as the velocity (how hard to hit the note in MIDI lingo)
const int amount_to_avg = 48;
float gyroArray[amount_to_avg];

int USE_SENSOR = 0; //need to eventually switch to make this easier, but 1 is Accel, 2 is Gyro

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
      sample_count = 0;
      update_screen("BLE Connected!");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      char msg[64];
      strcpy(msg, "BLE Disconnected? Samples Sent: "); /* copy name into the new var */
      
      char sc_str[64];
      sprintf(sc_str, "%d", sample_count);
      strcat(msg, sc_str);

      update_screen(msg);
    }
};

void setup() {
  M5.begin();


  Serial.begin(115200); //if before M5 init, seems to not work?


  M5.MPU6886.Init();

  BLEDevice::init(DEVICE_NAME);

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

  // Configure Buttons
  pinMode(side_button, INPUT_PULLUP);
  pinMode(front_button, INPUT);

  // Init the screen
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 10);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.printf("guitar_vibe");
  M5.Lcd.setCursor(0, 30);
  M5.Lcd.printf("Device Name: %s",DEVICE_NAME);
  delay(500);
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
    sample_count++;

    // check to see if we are trying to change which sensor to use
    if (!digitalRead(front_button)){
        USE_SENSOR = 1;
    }
    if (!digitalRead(side_button)){
        USE_SENSOR = 2;
    }

    // scale sensors to be within valid note range (0-127) [127 is G8]
    // accel I'm only using values up to 1G
    int acc_scaled;
    if (accX > 1){
      acc_scaled = 127;
    }
    else if (accX < -1){
      acc_scaled = 127;
    }
    else{
      acc_scaled = abs(ceil(accX * 127));
    }

    // scaling gyro based off how much it has moved in that axis previously
    int gyro_scaled;
    for (int i = 1; i<24;i++){
      gyroArray[i] = gyroArray[i-1];
    }
    gyroArray[0]=abs(gyroX); //im lazy and dont want to deal with which direction its moving it, just care how fast

    float gyroAvg = 0;
    float gyroMax = 0;
    for (int i = 0; i<24;i++){
      gyroAvg += gyroArray[i];
      if (gyroArray[i] > gyroMax){
        gyroMax = gyroArray[i];
      }
    }
    gyroAvg = gyroAvg/24;

    // lazy
    // subtracting 1 because this should converge to 1
    gyro_scaled = abs(ceil( ((gyroMax/gyroAvg)-1) * 127));
    if (gyro_scaled > 127){
      gyro_scaled = 127; //im lazy and the max value for velocity in midi is 127. 
    }

    if (SERIAL_TOGGLE){
      printf("Acc: %d, Gyro: %d, Gyro Avg: %f, Gyro Max: %f, gyroMax/gyroAvg: %f, gyroX/gyroMax: %f  \r\n",acc_scaled,gyro_scaled,gyroAvg,gyroMax,((gyroMax/gyroAvg)-1),(abs(gyroX)/gyroMax));
    }

    if (USE_SENSOR == 1){
      // append sample count to screen
      strcat(combined_a, ". sample: ");
      char sc_str[64];
      sprintf(sc_str, "%d", sample_count);
      strcat(combined_a, sc_str);
      update_screen(combined_a);
      midiPacket[3] = acc_scaled; // note
      midiPacket[4] = gyro_scaled;  // velocity
    }
    else if (USE_SENSOR == 2){
      update_screen(combined_g);
      midiPacket[3] = gyro_scaled; // note
      midiPacket[4] = acc_scaled;  // velocity
      if (SERIAL_TOGGLE){
        Serial.println("connected, g");
      }
    }
    else{
      char msg[64];
      strcpy(msg, "No Sensor, Samples Sent: "); /* copy name into the new var */
      char sc_str[64];
      sprintf(sc_str, "%d", sample_count);
      strcat(msg, sc_str);
      update_screen(msg);
      midiPacket[3] = 0x3c; // middle c
    }
    
    // note down
    midiPacket[2] = 0x90; // note down, channel 0
    pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
    pCharacteristic->notify();

    // play note for 100ms
    delay(100);

    // note up
    midiPacket[2] = 0x20; // note up, channel 0
    midiPacket[4] = 0;    // velocity
    pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes)
    pCharacteristic->notify();

    delay(100);
  }

  else if (!digitalRead(side_button)){
    USE_SENSOR = 2;
    update_screen(combined_g);
    if (SERIAL_TOGGLE){
      Serial.println("side_button");
    }
  }

  else if (!digitalRead(front_button)){
    USE_SENSOR = 1;
    update_screen(combined_a);
    if (SERIAL_TOGGLE){
      Serial.println("front_button");
    }
  }

  delay(100);
}