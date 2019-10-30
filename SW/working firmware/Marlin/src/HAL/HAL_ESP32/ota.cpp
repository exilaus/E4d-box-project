/**
 * Marlin 3D Printer Firmware
 * Copyright (C) 2016 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 * Copyright (c) 2016 Bob Cousins bobcousins42@googlemail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef ARDUINO_ARCH_ESP32

#include "../../inc/MarlinConfigPre.h"

#if ENABLED(WIFISUPPORT)

#include <WiFi.h>
#include <WiFiUdp.h>
#include "HAL.h"
#include <ArduinoOTA.h>
#include "driver/timer.h"

void OTA_init() {
  ArduinoOTA
    .onStart([]() {
      timer_pause(TIMER_GROUP_0, TIMER_0);
      timer_pause(TIMER_GROUP_0, TIMER_1);

      // U_FLASH or U_SPIFFS
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      MYSERIAL0.println("Start updating " + type);
    })
    .onEnd([]() {
      MYSERIAL0.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      MYSERIAL0.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      MYSERIAL0.printf("Error[%u]: ", error);
      String str;
      switch (error) {
        case OTA_AUTH_ERROR:    str = "Auth Failed";    break;
        case OTA_BEGIN_ERROR:   str = "Begin Failed";   break;
        case OTA_CONNECT_ERROR: str = "Connect Failed"; break;
        case OTA_RECEIVE_ERROR: str = "Receive Failed"; break;
        case OTA_END_ERROR:     str = "End Failed";     break;
      }
      MYSERIAL0.println(str.c_str());
    });

  ArduinoOTA.begin();
}

void OTA_handle() {
  ArduinoOTA.handle();
}

void OTA_end() {
  ArduinoOTA.end();
}

#endif // WIFISUPPORT

#endif // ARDUINO_ARCH_ESP32
