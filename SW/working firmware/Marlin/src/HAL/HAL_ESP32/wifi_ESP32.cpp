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
#include "../../lcd/ultralcd.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SPIFFS.h>
#include "HAL.h"
#include <Preferences.h>
#include "wifi_ESP32.h"
#include "http_ESP32.h"
#include "ota.h"

Preferences ESP_preferences;

bool restart_ESP_module = false;

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_task_wdt_reset();
#ifdef __cplusplus
}
#endif
/**
 * Check if Hostname string is valid
 */



uint32_t IP_int_from_string(String & s){
    uint32_t ip_int = 0;
    IPAddress ipaddr;
    if (ipaddr.fromString(s)) ip_int = ipaddr;
    return ip_int;
}

String IP_string_from_int(uint32_t ip_int){
    IPAddress ipaddr(ip_int);
    return ipaddr.toString();
}

bool isHostnameValid (const char * hostname)
{
    //limited size
    char c;
    if (strlen (hostname) > MAX_HOSTNAME_LENGTH || strlen (hostname) < MIN_HOSTNAME_LENGTH) {
        return false;
    }
    //only letter and digit
    for (int i = 0; i < strlen (hostname); i++) {
        c = hostname[i];
        if (! (isdigit (c) || isalpha (c) || c == '_') ) {
            return false;
        }
        if (c == ' ') {
            return false;
        }
    }
    return true;
}


/**
 * Check if SSID string is valid
 */

bool isSSIDValid (const char * ssid)
{
    //limited size
    //char c;
    if (strlen (ssid) > MAX_SSID_LENGTH || strlen (ssid) < MIN_SSID_LENGTH) {
        return false;
    }
    //only printable
    for (int i = 0; i < strlen (ssid); i++) {
        if (!isPrintable (ssid[i]) ) {
            return false;
        }
    }
    return true;
}

/**
 * Check if password string is valid
 */

bool isPasswordValid (const char * password)
{
    if (strlen (password) == 0) return true; //open network
    //limited size
    if ((strlen (password) > MAX_PASSWORD_LENGTH) || (strlen (password) < MIN_PASSWORD_LENGTH)) {
        return false;
    }
    //no space allowed ?
  /*  for (int i = 0; i < strlen (password); i++)
        if (password[i] == ' ') {
            return false;
        }*/
    return true;
}

/**
 * Check if IP string is valid
 */
bool isValidIP(const char * string){
    IPAddress ip;
    return ip.fromString(string);
}

/*
 * delay is to avoid with asyncwebserver and may need to wait sometimes
 */
void ESP_wait(uint32_t milliseconds){
    uint32_t timeout = millis();
    //wait feeding WDT
    while ( (millis() - timeout) < milliseconds) {
       esp_task_wdt_reset();
    }
}

/**
 * WiFi events 
 * SYSTEM_EVENT_WIFI_READY               < ESP32 WiFi ready
 * SYSTEM_EVENT_SCAN_DONE                < ESP32 finish scanning AP
 * SYSTEM_EVENT_STA_START                < ESP32 station start
 * SYSTEM_EVENT_STA_STOP                 < ESP32 station stop
 * SYSTEM_EVENT_STA_CONNECTED            < ESP32 station connected to AP
 * SYSTEM_EVENT_STA_DISCONNECTED         < ESP32 station disconnected from AP
 * SYSTEM_EVENT_STA_AUTHMODE_CHANGE      < the auth mode of AP connected by ESP32 station changed
 * SYSTEM_EVENT_STA_GOT_IP               < ESP32 station got IP from connected AP
 * SYSTEM_EVENT_STA_LOST_IP              < ESP32 station lost IP and the IP is reset to 0
 * SYSTEM_EVENT_STA_WPS_ER_SUCCESS       < ESP32 station wps succeeds in enrollee mode
 * SYSTEM_EVENT_STA_WPS_ER_FAILED        < ESP32 station wps fails in enrollee mode
 * SYSTEM_EVENT_STA_WPS_ER_TIMEOUT       < ESP32 station wps timeout in enrollee mode
 * SYSTEM_EVENT_STA_WPS_ER_PIN           < ESP32 station wps pin code in enrollee mode
 * SYSTEM_EVENT_AP_START                 < ESP32 soft-AP start
 * SYSTEM_EVENT_AP_STOP                  < ESP32 soft-AP stop
 * SYSTEM_EVENT_AP_STACONNECTED          < a station connected to ESP32 soft-AP
 * SYSTEM_EVENT_AP_STADISCONNECTED       < a station disconnected from ESP32 soft-AP
 * SYSTEM_EVENT_AP_PROBEREQRECVED        < Receive probe request packet in soft-AP interface
 * SYSTEM_EVENT_GOT_IP6                  < ESP32 station or ap or ethernet interface v6IP addr is preferred
 * SYSTEM_EVENT_ETH_START                < ESP32 ethernet start
 * SYSTEM_EVENT_ETH_STOP                 < ESP32 ethernet stop
 * SYSTEM_EVENT_ETH_CONNECTED            < ESP32 ethernet phy link up
 * SYSTEM_EVENT_ETH_DISCONNECTED         < ESP32 ethernet phy link down
 * SYSTEM_EVENT_ETH_GOT_IP               < ESP32 ethernet got IP from connected AP
 * SYSTEM_EVENT_MAX
 */

void WiFiEvent(WiFiEvent_t event)
{
 switch (event)
    {
    case SYSTEM_EVENT_STA_GOT_IP:
        MYSERIAL0.println ("Connected");
        MYSERIAL0.println(WiFi.localIP());
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        MYSERIAL0.println("WiFi lost connection");
        break;
    }
    
}

void StartServers(){
    //Sanity check
    if(WiFi.getMode() == WIFI_OFF) return;
    String h;
    //Get hostname
    String defV = DEFAULT_HOSTNAME;
    ESP_preferences.begin(NAMESPACE, true);
    h = ESP_preferences.getString(HOSTNAME_ENTRY, defV);
    ESP_preferences.end();
    MYSERIAL0.print("Start mDNS with hostname:");
    MYSERIAL0.println(h.c_str());
#if NUM_SERIAL > 1
        MYSERIAL1.print("Start mDNS with hostname:");
        MYSERIAL1.println(h.c_str());
#endif    

    //Start SPIFFS
    SPIFFS.begin(true);
    
  
    
     //start mDns
    if (!MDNS.begin(h.c_str())) {
        SERIAL_ERROR_START();
        SERIAL_ERRORLNPGM("Cannot start mDNS service");
    }
    //start OTA
    OTA_init();  
    
    //start http server /services 
    http_start();
}

void StopServers(){
    
    //stop http server / services
    http_stop();
    
    //Stop  OTA
    OTA_end();
    
    //Stop mDNS
    MDNS.end();
    
    //Stop SPIFFS
    SPIFFS.end();
}

/*
 * Get WiFi signal strength
 */
int32_t getSignal (int32_t RSSI)
{
    if (RSSI <= -100) {
        return 0;
    }
    if (RSSI >= -50) {
        return 100;
    }
    return (2 * (RSSI + 100) );
}

bool ConnectSTA2AP(){
    String msg, msg_out;
    uint8_t count = 0;
    uint8_t dot = 0;
    wl_status_t status = WiFi.status();
    while (status != WL_CONNECTED && count < 40) {
         
         switch (status) {
            case WL_NO_SSID_AVAIL:
                msg="No SSID";
                break;
            case WL_CONNECT_FAILED:
                msg="Connection failed";
                break;
            case WL_CONNECTED:
                break;
            default:
                if ((dot>3) || (dot==0) ){
                    dot=0;
                    msg_out = "Connecting";
                }
                msg_out+=".";
                msg= msg_out;
                dot++;
                break;
        }
        MYSERIAL0.println(msg.c_str());
#if NUM_SERIAL > 1
        MYSERIAL1.println(msg.c_str());
#endif    
        lcd_setstatus(msg.c_str());
        ESP_wait (500);
        count++;
        status = WiFi.status();
     }
    return (status == WL_CONNECTED);
}

void StartSTA(){
    String defV;
    //stop active service
    StopServers();
    //Sanity check
    if((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA))WiFi.disconnect();
    if((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA))WiFi.softAPdisconnect();
    WiFi.enableAP (false);
    WiFi.mode(WIFI_STA);
    //Get parameters for STA
    ESP_preferences.begin(NAMESPACE, true);
    //SSID
    defV = DEFAULT_STA_SSID;
    String SSID = ESP_preferences.getString(STA_SSID_ENTRY, defV);
    if (SSID.length() == 0)SSID = DEFAULT_STA_SSID;
    //password
    defV = DEFAULT_STA_PWD;
    String password = ESP_preferences.getString(STA_PWD_ENTRY, defV);
    int8_t IP_mode = ESP_preferences.getChar(STA_IP_MODE_ENTRY, DHCP_MODE);
    //IP
    defV = DEFAULT_STA_IP;
    int32_t IP = ESP_preferences.getInt(STA_IP_ENTRY, IP_int_from_string(defV));
    //GW
    defV = DEFAULT_STA_GW;
    int32_t GW = ESP_preferences.getInt(STA_GW_ENTRY, IP_int_from_string(defV));
    //MK
    defV = DEFAULT_STA_MK;
    int32_t MK = ESP_preferences.getInt(STA_MK_ENTRY, IP_int_from_string(defV));
    ESP_preferences.end(); 
    //if not DHCP
    if (IP_mode != DHCP_MODE) {
        IPAddress ip(IP), mask(MK), gateway(GW);
        WiFi.config(ip, gateway,mask);
    }
    WiFi.begin(SSID.c_str(), (password.length() > 0)?password.c_str():NULL);
    //TODO: all serial should have info not only one listed here 
    MYSERIAL0.println("Station Started");
#if NUM_SERIAL > 1
    MYSERIAL1.println("Station Started");
#endif 
    if (ConnectSTA2AP())  StartServers();
}

/**
 * Setup and start Accass point
 */

void StartAP(){
    String defV;
    //stop active services
    StopServers();
    //Sanity check
    if((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA))WiFi.disconnect();
    if((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA))WiFi.softAPdisconnect();
    WiFi.enableSTA (false);
    WiFi.mode(WIFI_AP);
    //Get parameters for AP
    ESP_preferences.begin(NAMESPACE, true);
    //SSID
    defV = DEFAULT_AP_SSID;
    String SSID = ESP_preferences.getString(AP_SSID_ENTRY, defV);
    if (SSID.length() == 0)SSID = DEFAULT_AP_SSID;
    //password
    defV = DEFAULT_AP_PWD;
    String password = ESP_preferences.getString(AP_PWD_ENTRY, defV);
    //channel
    int8_t channel = ESP_preferences.getChar(AP_CHANNEL_ENTRY, DEFAULT_AP_CHANNEL);
    if (channel == 0)channel = DEFAULT_AP_CHANNEL;
    //IP
    defV = DEFAULT_AP_IP;
    int32_t IP = ESP_preferences.getInt(AP_IP_ENTRY, IP_int_from_string(defV));
    if (IP==0){
        IP = IP_int_from_string(defV);
    } 
    ESP_preferences.end(); 
    IPAddress ip(IP);
    IPAddress mask;
    mask.fromString(DEFAULT_AP_MK);
    //Set static IP
    WiFi.softAPConfig(ip, ip, mask);
    //Start AP
    WiFi.softAP(SSID.c_str(), (password.length() > 0)?password.c_str():NULL, channel);
    //TODO: all serial should have info not only one listed here 
    MYSERIAL0.println("AP Started");
#if NUM_SERIAL > 1
    MYSERIAL1.println("AP Started");
#endif 
    
    MYSERIAL0.println(WiFi.softAPIP().toString().c_str());
#if NUM_SERIAL > 1
    MYSERIAL1.println(WiFi.softAPIP().toString().c_str());
#endif    
    lcd_setstatus(WiFi.softAPIP().toString().c_str());
    StartServers();
}

/**
 * Stop WiFi
 */

void StopWiFi(){
    //Sanity check
    if((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA))WiFi.disconnect(true);
    if((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA))WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    String s = "WiFi Off";
    MYSERIAL0.println(s.c_str());
#if NUM_SERIAL > 1
    MYSERIAL1.println(s.c_str());
#endif    
    lcd_setstatus(s.c_str());
    StopServers();
}


void WIFI_init() {
   //setup events
   WiFi.onEvent(WiFiEvent);
   //open preferences as read-only
   ESP_preferences.begin(NAMESPACE, true);
   int8_t wifiMode = ESP_preferences.getChar(ESP_WIFI_MODE, ESP_WIFI_OFF);
   ESP_preferences.end();
   if (wifiMode == ESP_WIFI_AP) {
       StartAP();
   } else if (wifiMode == ESP_WIFI_STA){
       StartSTA();
   }else WiFi.mode(WIFI_OFF);
}



void WIFI_handle() {
    //in case of restart requested
    if (restart_ESP_module) {
        ESP.restart();
        while (1) {};
    }
    Serial2Socket.handle_flush();
}


#endif // WIFISUPPORT

#endif // ARDUINO_ARCH_ESP32
