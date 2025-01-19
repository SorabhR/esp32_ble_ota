#ifndef BLE_OTA_H
#define BLE_OTA_H

#include "FS.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void rebootEspWithReason(String reason);
static void writeBinary(fs::FS &fs, const char * path, uint8_t *dat, int len);
void sendOtaResult(String result);
void performUpdate(Stream &updateSource, size_t updateSize);
void updateFromFS(fs::FS &fs);
void initBLE();
void init_for_ota();
void ota_task(void *parameter);


#endif