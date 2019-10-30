/**
 * Marlin 3D Printer Firmware
 * Copyright (C) 2016 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (C) 2011 Camiel Gubbels / Erik van der Zalm
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
 *
 */

/**
 * Espressif ESP32 (Tensilica Xtensa LX6) pin assignments
 */

#ifndef BOARD_NAME
  #define BOARD_NAME "Espressif ESP32"
#endif

//
// Limit Switches
//
#define X_MIN_PIN          34
#define Y_MIN_PIN          35
#define Z_MIN_PIN          16//15

//
// Steppers
//
#define X_STEP_PIN         12//34//27
#define X_DIR_PIN          13//35//26
#define X_ENABLE_PIN       17//0//17//25 // used free pin
//#define X_CS_PIN            0

#define Y_STEP_PIN         32//33
#define Y_DIR_PIN          33//32
#define Y_ENABLE_PIN       X_ENABLE_PIN
//#define Y_CS_PIN           13

#define Z_STEP_PIN         25//14
#define Z_DIR_PIN          26//12
#define Z_ENABLE_PIN       X_ENABLE_PIN
//#define Z_CS_PIN            5 // SS_PIN

#define E0_STEP_PIN        27//16
#define E0_DIR_PIN         14//17
#define E0_ENABLE_PIN      X_ENABLE_PIN
//#define E0_CS_PIN          21


//
// Temperature Sensors
//
#define TEMP_0_PIN         36   // Analog Input
#define TEMP_BED_PIN       39   // Analog Input

//
// Heaters / Fans
//
#define HEATER_0_PIN        2//4//2//(D8)
#define FAN_PIN            0//2//15//13 (D9)
#define HEATER_BED_PIN      15//15//0 //(D10)

// //ender 3 lcd 
// // #if defined(CR10_STOCKDISPLAY)
// //  CR10_STOCKDISPLAY
// #define ST7920_CLK_PIN  22// SCL pin on esp32 //LCD_PINS_D4
// #define LCD_PINS_D4 ST7920_CLK_PIN
// #define ST7920_DAT_PIN  21//=LCD_PINS_ENABLE = 21//SDA pin on esp32//LCD_PINS_ENABLE
// #define LCD_PINS_ENABLE ST7920_DAT_PIN
// #define ST7920_CS_PIN   4//Free from initial schematics //LCD_PINS_RS
// #define LCD_PINS_RS ST7920_CS_PIN



// #endif // 

