#include "ble_ota.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void setup(){
  Serial.begin(115200);
  delay(1000);

  init_for_ota();
  delay(1000);

  // Create ota task pinned to core 1
  xTaskCreatePinnedToCore(
    ota_task,    // Task function
    "OTA Task", // Task name
    10000,         // Stack size
    NULL,         // Parameters
    1,            // Priority
    NULL,         // Task handle
    1             // Core 1
  );
}

void loop(){
  
}