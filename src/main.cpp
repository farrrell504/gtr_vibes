#include <Arduino.h>
#include <M5StickC.h>
#include <driver/i2s.h>
#include <MIDI.h>

#include <stdio.h> 
#include <math.h>

// Headers for ESP32 BLE
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>


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

// Accel and Gyro variables
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


// microphone stuff
#define PIN_CLK  0
#define PIN_DATA 34
#define READ_LEN (2 * 256)
#define GAIN_FACTOR 3
uint8_t MIC_BUFFER[READ_LEN] = {0};

uint16_t oldy[160];
int16_t *adcBuffer = NULL;

void i2s_init()
{
  // straight copied from m5stickc example for microphone
   i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate =  44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, // is fixed at 12bit, stereo, MSB
    .channel_format = I2S_CHANNEL_FMT_ALL_RIGHT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 2,
    .dma_buf_len = 128,
   };

   i2s_pin_config_t pin_config;
   pin_config.bck_io_num   = I2S_PIN_NO_CHANGE;
   pin_config.ws_io_num    = PIN_CLK;
   pin_config.data_out_num = I2S_PIN_NO_CHANGE;
   pin_config.data_in_num  = PIN_DATA;
	
   
   i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
   i2s_set_pin(I2S_NUM_0, &pin_config);
   i2s_set_clk(I2S_NUM_0, 44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

uint8_t midiPacket[] = {
   0x80,  // header
   0x80,  // timestamp, not implemented 
   0x00,  // status
   0x3c,  // 0x3c == 60 == middle c
   0x00   // velocity
};

// typedef struct midi_msg {
//   uint8_t note;
//   uint8_t pkt_buf[5];
//   int16_t accel;
// } midi_msg;

// void set_note(midi_msg *pm, uint8_t note) {
//   pm->note = note;
//   pm->pkt_buf[3] = note;
// }

// midi_msg m = {
//   .note = 1
//   .pkt_buf = {0x80, 0x80, 0x00, 0x3c, 0x00},
//   .accel = 1
// };
// midi_msg *pm = &m;

// means midiPacket[3] = 11

// setting MPU interrupt to be active low, open drain 
// MPU to ESP32 for interrupting from motion

// actual struct
// midi_msg m;
// m.pkt_buf[0] = 1;
// sizeof(m) should equal 5 + 2 = 7 

// do not delete
// pointer to struct
// midi_msg *pm = &m;
// pm->pkt_buf[0] = 1;
// sizeof(pm) should equal 4 on a 32-bit system, or 8 on a 64-bit system


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

std::string conv_data_to_char(const char* sensor, float X, float Y, float Z){
  char x[64];
  char y[64];
  char z[64];

  gcvt(X, 4, x);
  gcvt(Y, 4, y);
  gcvt(Z, 4, z);

  char combined[64];
  strcpy(combined, sensor);
  strcat(combined, ": "); /* copy name into the new var */
  strcat(combined, x); 
  strcat(combined, ", "); 
  strcat(combined, y);
  strcat(combined, ", ");
  strcat(combined, z);

  return combined;
}

void setup() {
  M5.begin();

  Serial.begin(115200); //if before M5 init, seems to not work?

  M5.MPU6886.Init();

  i2s_init();

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

  // make easy to print to lcd strings of the accel and gyro values
  const std::string gyro_for_lcd_str=conv_data_to_char("Gyro",gyroX,gyroY,gyroZ);
  const char* gyro_for_lcd = gyro_for_lcd_str.c_str();

  const std::string accel_for_lcd_str=conv_data_to_char("Accel",accX,accY,accZ);
  const char* accel_for_lcd = accel_for_lcd_str.c_str();

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

    printf("Acc: %d, Gyro: %d, Gyro Avg: %f, Gyro Max: %f, gyroMax/gyroAvg: %f, gyroX/gyroMax: %f  \r\n",acc_scaled,gyro_scaled,gyroAvg,gyroMax,((gyroMax/gyroAvg)-1),(abs(gyroX)/gyroMax));

    if (USE_SENSOR == 1){
      // append sample count to screen
      char msg[64];
      sprintf(msg, "%s . sample: %d", accel_for_lcd,sample_count);
      update_screen(msg);
      midiPacket[3] = acc_scaled; // note
      midiPacket[4] = gyro_scaled;  // velocity
    }
    else if (USE_SENSOR == 2){
      update_screen(gyro_for_lcd);
      midiPacket[3] = gyro_scaled; // note
      midiPacket[4] = acc_scaled;  // velocity
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
    update_screen(gyro_for_lcd);
    Serial.println("side_button");
  }

  else if (!digitalRead(front_button)){
    USE_SENSOR = 1;
    update_screen(accel_for_lcd);
    Serial.println("front_button");
  }

  delay(100);
}