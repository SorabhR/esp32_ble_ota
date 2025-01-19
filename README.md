Repo contains:
1. esp32_ble_ota.ino which is the arduino code where the ble service is initialized and data exchaneg takes place between the client and the esp32.
2. ota.py is the python script to send the new firmware to esp32 using BLE.[python ota.py "74:4D:BD:A9:ED:E9" OTA_test.ino.bin -> can use this command to run the python script and send new firmware to esp32 (replace your ESP's MAC)]
3. OTA_test.ino.bin is the test firmware to be sent via OTA

ESP code flow:
1. First the size of new firmware is sent and ESP checks if it has space to store that file if not the OTA is aborted.
2. IF there is space available it recieves the number of parts in which the firmware is divided.
3. Then it recieves data and after recieving each part it requests client (python script here) for the next part.
4. After all parts are recieved it writes data from spiffs to memory and performs reboot.
5. Before reboot it informs the client that OTA is complete.

Python code flow:
1. It sends the file size of new firmware to ESP32 and waits to check if OTA can be performed.
2. IF yes then it reads the data from bin file and divides in parts of 16000 bytes and sends to ESP32.
3. After sending one part it waits for ESP to requent next part.
4. AFter sending all parts it waits for OTA complete command from esp32 and then aborts the OTA and disconnects.
   
   

