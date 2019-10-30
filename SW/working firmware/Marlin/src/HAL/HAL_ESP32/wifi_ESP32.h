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

//Preferences entries
#define NAMESPACE "MARLIN"
#define HOSTNAME_ENTRY "ESP_HOSTNAME"
#define STA_SSID_ENTRY "STA_SSID"
#define STA_PWD_ENTRY "STA_PWD"
#define STA_IP_ENTRY "STA_IP"
#define STA_GW_ENTRY "STA_GW"
#define STA_MK_ENTRY "STA_MK"
#define ESP_WIFI_MODE "WIFI_MODE"
#define AP_SSID_ENTRY "AP_SSID"
#define AP_PWD_ENTRY "AP_PWD"
#define AP_IP_ENTRY "AP_IP"
#define AP_CHANNEL_ENTRY "AP_CHANNEL"
#define HTTP_ENABLE_ENTRY "HTTP_ON"
#define HTTP_PORT_ENTRY "HTTP_PORT"
#define TELNET_ENABLE_ENTRY "TELNET_ON"
#define TELNET_PORT_ENTRY "TELNET_PORT"
#define STA_IP_MODE_ENTRY "STA_IP_MODE"

//Wifi Mode
#define ESP_WIFI_OFF 0
#define ESP_WIFI_STA 1
#define ESP_WIFI_AP  2

#define DHCP_MODE   0
#define STATIC_MODE   0

//Switch 
#define ESP_SAVE_ONLY 0
#define ESP_APPLY_NOW 1

//defaults values
#define DEFAULT_HOSTNAME "MARLIN_ESP"
#define DEFAULT_STA_SSID "MARLIN_ESP"
#define DEFAULT_STA_PWD "12345678"
#define DEFAULT_STA_IP "0.0.0.0"
#define DEFAULT_STA_GW "0.0.0.0"
#define DEFAULT_STA_MK "0.0.0.0"
#define DEFAULT_WIFI_MODE 0
#define DEFAULT_AP_SSID "MARLIN_ESP"
#define DEFAULT_AP_PWD "12345678"
#define DEFAULT_AP_IP "192.168.0.1"
#define DEFAULT_AP_MK "255.255.255.0"
#define DEFAULT_AP_CHANNEL 1
#define DEFAULT_WEBSERVER_PORT 80
#define HIDDEN_PASSWORD "********"

//boundaries
#define MAX_SSID_LENGTH         32
#define MIN_SSID_LENGTH         1
#define MAX_PASSWORD_LENGTH     64
//min size of password is 0 or upper than 8 char
//so let set min is 8
#define MIN_PASSWORD_LENGTH     8
#define MAX_HOSTNAME_LENGTH     32
#define MIN_HOSTNAME_LENGTH     1
#define MAX_HTTP_PORT			65001
#define MIN_HTTP_PORT			1
#define MAX_TELNET_PORT			65001
#define MIN_TELNET_PORT			1
#define MIN_CHANNEL			1
#define MAX_CHANNEL			14


#ifndef _HAL_WIFI_ESP32_H
#define _HAL_WIFI_ESP32_H

extern bool restart_ESP_module;

bool isValidIP(const char * string);
bool isPasswordValid (const char * password);
bool isSSIDValid (const char * ssid);
bool isHostnameValid (const char * hostname);

uint32_t IP_int_from_string(String & s);
String IP_string_from_int(uint32_t ip_int);

void StartAP();
void StartSTA();
void StopWiFi();
int32_t getSignal (int32_t RSSI);

void StartServers();
void StopServers();

void WIFI_init();
void WIFI_handle();

#endif
