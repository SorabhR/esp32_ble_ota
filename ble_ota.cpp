#include "ble_ota.h"
#include <Update.h>
#include "FFat.h"
#include "SPIFFS.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#define BUILTINLED 2
#define FORMAT_SPIFFS_IF_FAILED true
#define FORMAT_FFAT_IF_FAILED true

#define USE_SPIFFS  //comment to use FFat

#ifdef USE_SPIFFS
#define FLASH SPIFFS
#define FASTMODE false    //SPIFFS write is slow
#else
#define FLASH FFat
#define FASTMODE true    //FFat is faster
#endif

#define NORMAL_MODE   0   // normal
#define UPDATE_MODE   1   // receiving firmware
#define OTA_MODE      2   // installing firmware

uint8_t updater[16384];
uint8_t updater2[16384];

#define SERVICE_UUID              "fb1e4001-54ae-4a28-9f74-dfccb248601d"
#define CHARACTERISTIC_UUID_RX    "fb1e4002-54ae-4a28-9f74-dfccb248601d"
#define CHARACTERISTIC_UUID_TX    "fb1e4003-54ae-4a28-9f74-dfccb248601d"

static BLECharacteristic* pCharacteristicTX;
static BLECharacteristic* pCharacteristicRX;

static bool deviceConnected = false, sendMode = false, sendSize = true;
static bool writeFile = false, request = false;
static int writeLen = 0, writeLen2 = 0;
static bool current = true;
static int parts = 0, next = 0, cur = 0, MTU = 0;
static int MODE = NORMAL_MODE;
unsigned long rParts, tParts,avail_space;

unsigned long data_counter = 0;
bool full_partition_recv = false;
bool updater_1_busy = false;
bool updater_2_busy = false;
static void rebootEspWithReason(String reason) {
  Serial.println(reason);
  delay(1000);
  ESP.restart();
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;

    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {

    void onNotify(BLECharacteristic *pCharacteristic) {
      uint8_t* pData;
      String value = pCharacteristic->getValue();
      int len = value.length();
      pData = pCharacteristic->getData();
      if (pData != NULL) {
        Serial.print("TX  ");
        for (int i = 0; i < len; i++) {
          Serial.printf("%02X ", pData[i]);
        }
        Serial.println();
      }
    }

    void onWrite(BLECharacteristic *pCharacteristic) {
      uint8_t* pData;
      String value = pCharacteristic->getValue();
      int len = value.length();
      pData = pCharacteristic->getData();
      if (pData != NULL) {

        if (pData[0] == 0xFB) { //so basically here you get the data around 500 bytes in one packet pos=0-32
          int pos = pData[1];
          for (int x = 0; x < len - 2; x++) {
            if (current) {
              updater[(pos * MTU) + x] = pData[x + 2];
            } else {
              updater2[(pos * MTU) + x] = pData[x + 2];
            }
            data_counter++;
          }

        } else if  (pData[0] == 0xFC) { //here you get how much total data is to be sent and the part number incoming
          if (current) {
            writeLen = (pData[1] * 256) + pData[2];
            if (data_counter == writeLen)
              full_partition_recv = true;
          } else {
            writeLen2 = (pData[1] * 256) + pData[2];
            if (data_counter == writeLen2)
              full_partition_recv = true;
          }
          current = !current;
          if (full_partition_recv){ //verification that if complete data is recv only then update the partition number and save the current one to spiffs
            cur = (pData[3] * 256) + pData[4];
            writeFile = true;
            Serial.print("Full partition recieved for ");
            Serial.println(cur);
          }
          if (cur < parts - 1) {
            request = true;
          }
          data_counter = 0;
          full_partition_recv = false;
        } else if (pData[0] == 0xFE) { //in this packet it sends us total file size //so we print available spiffs and the recv ota size
          rParts = 0;
          tParts = (pData[1] * 256 * 256 * 256) + (pData[2] * 256 * 256) + (pData[3] * 256) + pData[4];
          
          if (FLASH.exists("/update.bin")) { //remove if this file exists
            FLASH.remove("/update.bin");
          }
          avail_space = FLASH.totalBytes() - FLASH.usedBytes();

          Serial.print("Available space: ");
          Serial.println(avail_space);
          Serial.print("File Size: ");
          Serial.println(tParts);
          uint8_t size_verified_cmd[2] = {0xA1,0};
          if (avail_space > tParts){
            size_verified_cmd[1] = 1;
          }
          else{
            size_verified_cmd[1] = 0;
          }

          pCharacteristicTX->setValue(size_verified_cmd, 2);
          pCharacteristicTX->notify();
          delay(50);


        } else if  (pData[0] == 0xFF) { //here we recv the number of fragments and MTU = 500
          parts = (pData[1] * 256) + pData[2];
          MTU = (pData[3] * 256) + pData[4];
          MODE = UPDATE_MODE;
          uint8_t ota_start_cmd[] = {0xAA, 1};
          if (avail_space < tParts){
            ota_start_cmd[1] = 0;
          }
          pCharacteristicTX->setValue(ota_start_cmd, 2);
          pCharacteristicTX->notify();
          delay(50);

        }
      }

    }


};

static void writeBinary(fs::FS &fs, const char * path, uint8_t *dat, int len) {

  //Serial.printf("Write binary file %s\r\n", path);

  File file = fs.open(path, FILE_APPEND);

  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  size_t bytesWritten = file.write(dat, len);
  if (!(bytesWritten == len))
    Serial.println("this failed");
  file.close();
  writeFile = false;
  rParts += len;
}

void sendOtaResult(String result) {
  pCharacteristicTX->setValue(result.c_str());
  pCharacteristicTX->notify();
  delay(200);
}


void performUpdate(Stream &updateSource, size_t updateSize) {
  char s1 = 0x0F;
  String result = String(s1);
  if (Update.begin(updateSize)) {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize) {
      Serial.println("Written : " + String(written) + " successfully");
    }
    else {
      Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
    }
    result += "Written : " + String(written) + "/" + String(updateSize) + " [" + String((written / updateSize) * 100) + "%] \n";
    if (Update.end()) {
      Serial.println("OTA done!");
      result += "OTA Done: ";
      if (Update.isFinished()) {
        Serial.println("Update successfully completed. Rebooting...");
        result += "Success!\n";
      }
      else {
        Serial.println("Update not finished? Something went wrong!");
        result += "Failed!\n";
      }

    }
    else {
      Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      result += "Error #: " + String(Update.getError());
    }
  }
  else
  {
    Serial.println("Not enough space to begin OTA");
    result += "Not enough space for OTA";
  }
  if (deviceConnected) {
    sendOtaResult(result);
    delay(5000);
  }
}

void updateFromFS(fs::FS &fs) {
  File updateBin = fs.open("/update.bin");
  if (updateBin) {
    if (updateBin.isDirectory()) {
      Serial.println("Error, update.bin is not a file");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0) {
      Serial.println("Trying to start update");
      performUpdate(updateBin, updateSize);
    }
    else {
      Serial.println("Error, file is empty");
    }

    updateBin.close();

    // when finished remove the binary from spiffs to indicate end of the process
    Serial.println("Removing update file");
    fs.remove("/update.bin");

    rebootEspWithReason("Rebooting to complete OTA update");
  }
  else {
    Serial.println("Could not load update.bin from spiffs root");
  }
}

void initBLE() {
  BLEDevice::init("ESP32 OTA");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristicTX = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY );
  pCharacteristicRX = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pCharacteristicRX->setCallbacks(new MyCallbacks());
  pCharacteristicTX->setCallbacks(new MyCallbacks());
  pCharacteristicTX->addDescriptor(new BLE2902());
  pCharacteristicTX->setNotifyProperty(true);
  pService->start();


  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Characteristic defined! Now you can read it in your phone!");
}


void init_for_ota(){

    pinMode(BUILTINLED, OUTPUT);

    #ifdef USE_SPIFFS
    if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    #else
    if (!FFat.begin()) {
        Serial.println("FFat Mount Failed");
        if (FORMAT_FFAT_IF_FAILED) FFat.format();
        return;
    }
    #endif


    initBLE();

    if (FLASH.exists("/update.bin")) { //remove if this file exists
      FLASH.remove("/update.bin");
    }
}

void ota_task(void *parameter) {
  for(;;){
    switch (MODE) {

      case NORMAL_MODE:
        if (deviceConnected) {
          digitalWrite(BUILTINLED, HIGH);
        } else {
          digitalWrite(BUILTINLED, LOW);
        }
        break;

      case UPDATE_MODE:
        //Serial.println("now in update mode");
        if (request) {
          while((current && updater_1_busy) || (!current && updater_2_busy)); //this gives time in case data copy to spiffs is slow dont request the next packet
          uint8_t rq[] = {0xF1, (cur + 1) / 256, (cur + 1) % 256}; //this sends which is the next packet
          pCharacteristicTX->setValue(rq, 3);
          pCharacteristicTX->notify();
          delay(50);
          request = false;
        }

        if (cur + 1 == parts) { // received complete file
          uint8_t com[] = {0xF2, (cur + 1) / 256, (cur + 1) % 256};
          pCharacteristicTX->setValue(com, 3);
          pCharacteristicTX->notify();
          delay(50);
          MODE = OTA_MODE;
        }

        if (writeFile) { //this is circular buffer concept to alow writing data to spiffs while reading
          if (!current) {
            updater_1_busy = true;
            writeBinary(FLASH, "/update.bin", updater, writeLen);
            updater_1_busy = false;
          } else {
            updater_2_busy = true;
            writeBinary(FLASH, "/update.bin", updater2, writeLen2);
            updater_2_busy = false;
          }
        }

        break;

      case OTA_MODE:
        if (writeFile) {
          if (!current) {
            writeBinary(FLASH, "/update.bin", updater, writeLen);
          } else {
            writeBinary(FLASH, "/update.bin", updater2, writeLen2);
          }
        }


        if (rParts == tParts) {
          Serial.println("Complete");
          delay(5000);
          updateFromFS(FLASH);
        } else {
          Serial.println("Incomplete");
          Serial.print("Expected: ");
          Serial.print(tParts);
          Serial.print("Received: ");
          Serial.println(rParts);
          delay(2000);
          rebootEspWithReason("Incomplete data recv");
        }
        break;

    }
    vTaskDelay(1 / portTICK_PERIOD_MS); // Delay 500ms
  }

}
