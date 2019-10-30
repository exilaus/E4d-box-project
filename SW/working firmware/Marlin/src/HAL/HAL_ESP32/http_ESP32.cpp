#ifdef ARDUINO_ARCH_ESP32

#include "../../inc/MarlinConfigPre.h"

#if ENABLED(WIFISUPPORT)

#include "../../gcode/gcode.h"
#include "../../gcode/queue.h"
#include "../../gcode/parser.h"
#include "../../inc/Version.h"
#include "HAL.h"
#include <WiFi.h>
#include <Preferences.h>
#include "../../core/serial.h"
#include <StreamString.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include "ESP32SSDP.h"
#include <FS.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include "wifi_ESP32.h"
#include "sd_ESP32.h"
#include "ws_ESP32.h"
//embedded response file if no files on SPIFFS
#include "nofile.h"

extern void ESP_wait(uint32_t milliseconds);
typedef enum {
    UPLOAD_STATUS_NONE = 0,
    UPLOAD_STATUS_FAILED = 1,
    UPLOAD_STATUS_CANCELLED = 2,
    UPLOAD_STATUS_SUCCESSFUL = 3,
    UPLOAD_STATUS_ONGOING  = 4
} upload_status_type;

typedef enum {
    LEVEL_GUEST = 0,
    LEVEL_USER = 1,
    LEVEL_ADMIN = 2
} level_authenticate_type;

extern Preferences ESP_preferences;
AsyncWebServer * webserver = NULL;
uint16_t http_port = 0;
uint16_t data_port = 0;
String http_hostname;
uint8_t _upload_status = UPLOAD_STATUS_NONE;
long id_connection = 0;
AsyncWebSocket * web_socket = NULL;
AsyncEventSource * web_events = NULL;

//Default 404
const uint8_t PAGE_404 [] PROGMEM = "<HTML>\n<HEAD>\n<title>Redirecting...</title> \n</HEAD>\n<BODY>\n<CENTER>Unknown page : $QUERY$- you will be redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) \n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";

//Helpers

//just simple helper to convert mac address to string
char * mac2str (uint8_t mac [8])
{
    static char macstr [18];
    if (0 > sprintf (macstr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]) ) {
        strcpy (macstr, "00:00:00:00:00:00");
    }
    return macstr;
}

String get_Splited_Value(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}


//helper to format size to readable string
String formatBytes (uint32_t bytes)
{
    if (bytes < 1024) {
        return String (bytes) + " B";
    } else if (bytes < (1024 * 1024) ) {
        return String (bytes / 1024.0) + " KB";
    } else if (bytes < (1024 * 1024 * 1024) ) {
        return String (bytes / 1024.0 / 1024.0) + " MB";
    } else {
        return String (bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
    }
}

//helper to extract content type from file extension
//Check what is the content tye according extension file
String getContentType (String filename)
{
    if (filename.endsWith (".htm") ) {
        return "text/html";
    } else if (filename.endsWith (".html") ) {
        return "text/html";
    } else if (filename.endsWith (".css") ) {
        return "text/css";
    } else if (filename.endsWith (".js") ) {
        return "application/javascript";
    } else if (filename.endsWith (".png") ) {
        return "image/png";
    } else if (filename.endsWith (".gif") ) {
        return "image/gif";
    } else if (filename.endsWith (".jpeg") ) {
        return "image/jpeg";
    } else if (filename.endsWith (".jpg") ) {
        return "image/jpeg";
    } else if (filename.endsWith (".ico") ) {
        return "image/x-icon";
    } else if (filename.endsWith (".xml") ) {
        return "text/xml";
    } else if (filename.endsWith (".pdf") ) {
        return "application/x-pdf";
    } else if (filename.endsWith (".zip") ) {
        return "application/x-zip";
    } else if (filename.endsWith (".gz") ) {
        return "application/x-gzip";
    } else if (filename.endsWith (".txt") ) {
        return "text/plain";
    }
    return "application/octet-stream";
}

//check authentification
level_authenticate_type  is_authenticated()
{
#ifdef AUTHENTICATION_FEATURE
    /*if (web_server.hasHeader ("Cookie") ) {
        String cookie = web_server.header ("Cookie");
        int pos = cookie.indexOf ("ESPSESSIONID=");
        if (pos != -1) {
            int pos2 = cookie.indexOf (";", pos);
            String sessionID = cookie.substring (pos + strlen ("ESPSESSIONID="), pos2);
            IPAddress ip = web_server.client().remoteIP();
            //check if cookie can be reset and clean table in same time
            return ResetAuthIP (ip, sessionID.c_str() );
        }
    }
    return LEVEL_GUEST;*/
#else
    return LEVEL_ADMIN;
#endif
}

String get_param (String & cmd_params, const char * id, bool withspace)
{
    static String parameter;
    String sid = id;
    int start;
    int end = -1;
    parameter = "";
    //if no id it means it is first part of cmd
    if (strlen (id) == 0) {
        start = 0;
    }
    //else find id position
    else {
        start = cmd_params.indexOf (id);
    }
    //if no id found and not first part leave
    if (start == -1 ) {
        return parameter;
    }
    //password and SSID can have space so handle it
    //if no space expected use space as delimiter
    if (!withspace) {
        end = cmd_params.indexOf (" ", start);
    }
    //if no end found - take all
    if (end == -1) {
        end = cmd_params.length();
    }
    //extract parameter
    parameter = cmd_params.substring (start + strlen (id), end);
    //be sure no extra space
    parameter.trim();
    return parameter;
}

//functions
bool execute_internal_command (int cmd, String cmd_params, level_authenticate_type auth_level,  AsyncResponseStream  *espresponse)
{
    bool response = true;
    level_authenticate_type auth_type = auth_level;

    //manage parameters
    String parameter;
    String resp="";
    switch (cmd) {
        //*Get SD Card Status
        //[ESP200]
        case 200:
            {
            ESP_SD card;
            int8_t state = card.card_status();
            if (state == -1)resp="Busy";
            else if (state == 1)resp="SD card detected";
            else resp="No SD card";
            if (espresponse)espresponse->println (resp.c_str());
            }
            break;
            
         case 201:
            {
                ESP_SD card;
                resp ="";
                if (card.openDir(cmd_params)){
                    bool isfile;
                    uint32_t filesize;
                    char name[13];
                    while (card.readDir( name, &filesize, &isfile)){
                    if (resp.length()>0)resp +="\n";
                    resp +=name;
                    resp +=" ";
                    resp +=String(filesize);
                    resp +=" ";
                    resp +=isfile?"File":"Dir";
                    }
                } else resp = "Error opening";
                if (resp.length() == 0)resp="empty";
                if (espresponse)espresponse->println (resp.c_str());
            }
            break;
        //Get full ESP32 wifi settings content
        //[ESP400]
        case 400:
            { 
            String v;
            String defV;
            int8_t vi;
            resp = F("{\"EEPROM\":[");
            ESP_preferences.begin(NAMESPACE, true);
            //1 - Hostname
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += HOSTNAME_ENTRY;
            resp += F ("\",\"T\":\"S\",\"V\":\"");
            resp += http_hostname;
            resp += F ("\",\"H\":\"Hostname\" ,\"S\":\"");
            resp += String(MAX_HOSTNAME_LENGTH);
            resp += F ("\", \"M\":\"");
            resp += String(MIN_HOSTNAME_LENGTH);
            resp += F ("\"}");
            resp += F (",");
            
            //2 - http protocol mode
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += HTTP_ENABLE_ENTRY;
            resp += F ("\",\"T\":\"B\",\"V\":\"");
            vi = ESP_preferences.getChar(HTTP_ENABLE_ENTRY, 1);
            resp += String(vi);
            resp += F ("\",\"H\":\"HTTP protocol\",\"O\":[{\"Enabled\":\"1\"},{\"Disabled\":\"0\"}]}");
            resp += F (",");
            
            //3 - http port
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += HTTP_PORT_ENTRY;
            resp += F ("\",\"T\":\"I\",\"V\":\"");
            resp += String(http_port);
            resp += F ("\",\"H\":\"HTTP Port\",\"S\":\"");
            resp += String(MAX_HTTP_PORT);
            resp += F ("\",\"M\":\"");
            resp += String(MIN_HTTP_PORT);
            resp += F ("\"}");
            resp += F (",");
            
            //4 - telnet protocol mode
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += TELNET_ENABLE_ENTRY;
            resp += F ("\",\"T\":\"B\",\"V\":\"");
            vi = ESP_preferences.getChar(TELNET_ENABLE_ENTRY, 0);
            resp += String(vi);
            resp += F ("\",\"H\":\"Telnet protocol\",\"O\":[{\"Enabled\":\"1\"},{\"Disabled\":\"0\"}]}");
            resp += F (",");
            
            //5 - telnet Port
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += TELNET_PORT_ENTRY;
            resp += F ("\",\"T\":\"I\",\"V\":\"");
            resp += String(data_port);
            resp += F ("\",\"H\":\"Telnet Port\",\"S\":\"");
            resp += String(MAX_TELNET_PORT);
            resp += F ("\",\"M\":\"");
            resp += String(MIN_TELNET_PORT);
            resp += F ("\"}");
            resp += F (",");
            
            //6 - wifi mode
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += ESP_WIFI_MODE;
            resp += F ("\",\"T\":\"B\",\"V\":\"");
            vi = ESP_preferences.getChar(ESP_WIFI_MODE, ESP_WIFI_OFF);
            resp += String(vi);
            resp += F ("\",\"H\":\"Wifi mode\",\"O\":[{\"STA\":\"1\"},{\"AP\":\"2\"},{\"None\":\"0\"}]}");
            resp += F (",");

            //7 - STA SSID
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += STA_SSID_ENTRY;
            resp += F ("\",\"T\":\"S\",\"V\":\"");
            defV = DEFAULT_STA_SSID;
            resp += ESP_preferences.getString(STA_SSID_ENTRY, defV);
            resp += F ("\",\"S\":\"");
            resp += String(MAX_SSID_LENGTH);
            resp +=  F ("\",\"H\":\"Station SSID\",\"M\":\"");
            resp += String(MIN_SSID_LENGTH);
            resp += F ("\"}");
            resp += F (",");

            //8 - STA password
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += STA_PWD_ENTRY;
            resp += F ("\",\"T\":\"S\",\"V\":\"");
            resp += HIDDEN_PASSWORD;
            resp += F ("\",\"S\":\"");
            resp += String(MAX_PASSWORD_LENGTH);
            resp += F ("\",\"H\":\"Station Password\",\"M\":\"");
            resp += String(MIN_PASSWORD_LENGTH);
            resp += F ("\"}");
            resp += F (",");
            
            // 9 - STA IP mode
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += STA_IP_MODE_ENTRY;
            resp += F ("\",\"T\":\"B\",\"V\":\"");
            resp += String(ESP_preferences.getChar(STA_IP_MODE_ENTRY, DHCP_MODE));
            resp += F ("\",\"H\":\"Station IP Mode\",\"O\":[{\"DHCP\":\"0\"},{\"Static\":\"1\"}]}");
            resp += F (",");

            //10-STA static IP
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += STA_IP_ENTRY;
            resp += F ("\",\"T\":\"A\",\"V\":\"");
            resp += IP_string_from_int(ESP_preferences.getInt(STA_IP_ENTRY, 0));
            resp += F ("\",\"H\":\"Station Static IP\"}");
            resp += F (",");

            //11-STA static Gateway
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += STA_GW_ENTRY;
            resp += F ("\",\"T\":\"A\",\"V\":\"");
            resp += IP_string_from_int(ESP_preferences.getInt(STA_GW_ENTRY, 0));
            resp += F ("\",\"H\":\"Station Static Gateway\"}");
            resp += F (",");

            //12-STA static Mask
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += STA_MK_ENTRY;
            resp += F ("\",\"T\":\"A\",\"V\":\"");
            resp += IP_string_from_int(ESP_preferences.getInt(STA_MK_ENTRY, 0));
            resp += F ("\",\"H\":\"Station Static Mask\"}");
            resp += F (",");
            
            //13 - AP SSID
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += AP_SSID_ENTRY;
            resp += F ("\",\"T\":\"S\",\"V\":\"");
            defV = DEFAULT_AP_SSID;
            resp += ESP_preferences.getString(AP_SSID_ENTRY, defV);
            resp += F ("\",\"S\":\"");
            resp += String(MAX_SSID_LENGTH);
            resp +=  F ("\",\"H\":\"AP SSID\",\"M\":\"");
            resp += String(MIN_SSID_LENGTH);
            resp += F ("\"}");
            resp += F (",");

            //14 - AP password
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += AP_PWD_ENTRY;
            resp += F ("\",\"T\":\"S\",\"V\":\"");
            resp += HIDDEN_PASSWORD;
            resp += F ("\",\"S\":\"");
            resp += String(MAX_PASSWORD_LENGTH);
            resp += F ("\",\"H\":\"AP Password\",\"M\":\"");
            resp += String(MIN_PASSWORD_LENGTH);
            resp += F ("\"}");
            resp += F (",");
            
            //15-AP static IP
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += AP_IP_ENTRY;
            resp += F ("\",\"T\":\"A\",\"V\":\"");
            defV = DEFAULT_AP_IP;
            resp += IP_string_from_int(ESP_preferences.getInt(AP_IP_ENTRY, IP_int_from_string(defV)));
            resp += F ("\",\"H\":\"AP Static IP\"}");
            resp += F (",");
            
            //20-AP Channel
            resp += F ("{\"F\":\"network\",\"P\":\"");
            resp += AP_CHANNEL_ENTRY;
            resp += F ("\",\"T\":\"B\",\"V\":\"");
            resp += String(ESP_preferences.getChar(AP_CHANNEL_ENTRY, DEFAULT_AP_CHANNEL));
            resp += F ("\",\"H\":\"AP Channel\",\"O\":[");
            for (int i = MIN_CHANNEL; i <= MAX_CHANNEL ; i++) {
                resp += F ("{\"");
                resp += String(i);
                resp += F ("\":\"");
                resp += String(i);
                resp += F ("\"}");
                if (i < MAX_CHANNEL) {
                    resp += F (",");
                }
            }
            resp += F ("]}");
            
            resp += F ("]}");
            ESP_preferences.end();
            if (espresponse)espresponse->println (resp.c_str());
            }
            break;
            //Set EEPROM setting
    //[ESP401]P=<position> T=<type> V=<value> pwd=<user/admin password>
    case 401:    
    {
        //check validity of parameters
        String spos = get_param (cmd_params, "P=", false);
        String styp = get_param (cmd_params, "T=", false);
        String sval = get_param (cmd_params, "V=", true);
        spos.trim();
        sval.trim();
        if (spos.length() == 0) {
            response = false;
        }
        if (! (styp == "B" || styp == "S" || styp == "A" || styp == "I" || styp == "F") ) {
            response = false;
        }
        if (sval.length() == 0) {
            response = false;
        }

/*
#ifdef AUTHENTICATION_FEATURE
        if (response) {
            //check authentication
            level_authenticate_type auth_need = LEVEL_ADMIN;
            for (int i = 0; i < AUTH_ENTRY_NB; i++) {
                if (Setting[i][0] == pos ) {
                    auth_need = (level_authenticate_type) (Setting[i][1]);
                    i = AUTH_ENTRY_NB;
                }
            }
            if ( (auth_need == LEVEL_ADMIN && auth_type == LEVEL_USER) || (auth_type == LEVEL_GUEST) ) {
                response = false;
            }
        }
#endif*/
        if (response) {
            ESP_preferences.begin(NAMESPACE, false);
            //Byte value
            if ((styp == "B")  ||  (styp == "F")){
                int8_t bbuf = sval.toInt();
                if (ESP_preferences.putChar(spos.c_str(), bbuf) ==0 ) {
                    response = false;
                } else {
                    //dynamique refresh is better than restart the board
                    if (spos == ESP_WIFI_MODE){
                        //TODO
                    }
                    if (spos == AP_CHANNEL_ENTRY) {
                        //TODO
                    }
                    if (spos == HTTP_ENABLE_ENTRY) {
                        //TODO
                    }
                    if (spos == TELNET_ENABLE_ENTRY) {
                        //TODO
                    }
                }
            }
            //Integer value
            if (styp == "I") {
                int16_t ibuf = sval.toInt();
                if (ESP_preferences.putUShort(spos.c_str(), ibuf) == 0) {
                    response = false;
                } else {
                    if (spos == HTTP_PORT_ENTRY){
                        //TODO
                    }
                    if (spos == TELNET_PORT_ENTRY){
                        //TODO
                        //Serial.println(ibuf);
                    }
                }
                
            }
            //String value
            if (styp == "S") {
               if (ESP_preferences.putString(spos.c_str(), sval) == 0) {
                    response = false;
                } else {
                    if (spos == HOSTNAME_ENTRY){
                        //TODO
                    }
                    if (spos == STA_SSID_ENTRY){
                        //TODO
                    }
                    if (spos == STA_PWD_ENTRY){
                        //TODO
                    }
                    if (spos == AP_SSID_ENTRY){
                        //TODO
                    }
                    if (spos == AP_PWD_ENTRY){
                        //TODO
                    }
                }
                
            }
            //IP address
            if (styp == "A") {
                if (ESP_preferences.putInt(spos.c_str(), IP_int_from_string(sval)) == 0) {
                    response = false;
                } else {
                    if (spos == STA_IP_ENTRY){
                        //TODO
                    }
                    if (spos == STA_GW_ENTRY){
                        //TODO
                    }
                    if (spos == STA_MK_ENTRY){
                        //TODO
                    }
                    if (spos == AP_IP_ENTRY){
                        //TODO
                    }
                }
            }
            ESP_preferences.end();
        }
        if (!response) {
            if (espresponse)espresponse->println ("Error: Incorrect Command");
        } else {
            if (espresponse)espresponse->println ("ok");
        }

    }
    break;
         //Get ESP current status 
        case 420:
            {
            resp = F ("Chip ID: ");
            resp += String ( (uint16_t) (ESP.getEfuseMac() >> 32) );
            resp += F ("\n");
            resp += F ("CPU Frequency: ");
            resp += String (ESP.getCpuFreqMHz() );
            resp += F ("Mhz");
            resp += F ("\n");
            resp += F ("CPU Temperature: ");
            resp += String (temperatureRead(), 1);
            resp += F ("&deg;C");
            resp += F ("\n");
            resp += F ("Free memory: ");
            resp += formatBytes (ESP.getFreeHeap() );
            resp += F ("\n");
            resp += F ("SDK: ");
            resp += ESP.getSdkVersion();
            resp += F ("\n");
            resp += F ("Flash Size: ");
            resp += formatBytes (ESP.getFlashChipSize() );
            resp += F ("\n");
            resp += F ("Available Size for update: ");
            //Not OTA on 2Mb board per spec
            if (ESP.getFlashChipSize() > 0x20000) {
                resp += formatBytes (0x140000);
            } else {
                resp += formatBytes (0x0);
            }
            resp += F ("\n");
            resp += F ("Available Size for SPIFFS: ");
            resp += formatBytes (SPIFFS.totalBytes() );
            resp += F ("\n");
            resp += F ("Baud rate: ");
            long br = MYSERIAL0.baudRate();
            //workaround for ESP32
            if (br == 115201) {
                br = 115200;
            }
            if (br == 230423) {
                br = 230400;
            }
            resp += String(br);
            resp += F ("\n");
            resp += F ("Sleep mode: ");
            if (WiFi.getSleep())resp += F ("Modem");
            else resp += F ("None");
            resp += F ("\n");
            resp += F ("Web port: ");
            resp += String(http_port);
            resp += F ("\n");
            resp += F ("Data port: ");
            if (data_port!=0)resp += String(data_port);
            else resp += F ("Disabled");
            resp += F ("\n");
            resp += F ("Hostname: ");
            resp += http_hostname;
            resp += F ("\n");
            resp += F ("Active Mode: ");
            if (WiFi.getMode() == WIFI_STA) {
                 resp += F ("STA (");
                 resp += WiFi.macAddress();
                 resp += F (")");
                 resp += F ("\n");
                 resp += F ("Connected to: ");
                 if (WiFi.isConnected()){ //in theory no need but ...  
                     resp += WiFi.SSID();
                     resp += F ("\n");
                     resp += F ("Signal: ");
                     resp += String(getSignal (WiFi.RSSI()));
                     resp += F ("%");
                     resp += F ("\n");
                     uint8_t PhyMode;
                     esp_wifi_get_protocol (ESP_IF_WIFI_STA, &PhyMode);
                     resp += F ("Phy Mode: ");
                     if (PhyMode == (WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N)) resp += F ("11n");
                     else if (PhyMode == (WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G)) resp += F ("11g");
                     else if (PhyMode == (WIFI_PROTOCOL_11B )) resp += F ("11b");
                     else resp += F ("???");
                     resp += F ("\n");
                     resp += F ("Channel: ");
                     resp += String (WiFi.channel());
                     resp += F ("\n");
                     resp += F ("IP Mode: ");
                     tcpip_adapter_dhcp_status_t dhcp_status;
                     tcpip_adapter_dhcpc_get_status (TCPIP_ADAPTER_IF_STA, &dhcp_status);
                     if (dhcp_status == TCPIP_ADAPTER_DHCP_STARTED)resp += F ("DHCP");
                     else resp += F ("Static");
                     resp += F ("\n");
                     resp += F ("IP: ");
                     resp += WiFi.localIP().toString();
                     resp += F ("\n");
                     resp += F ("Gateway: ");
                     resp += WiFi.gatewayIP().toString();
                     resp += F ("\n");
                     resp += F ("Mask: ");
                     resp += WiFi.subnetMask().toString();
                     resp += F ("\n");
                     resp += F ("DNS: ");
                     resp += WiFi.dnsIP().toString();
                     resp += F ("\n");
                 } //this is web command so connection => no command 
                 resp += F ("Disabled Mode: ");
                 resp += F ("AP (");
                 resp += WiFi.softAPmacAddress();
                 resp += F (")");
                 resp += F ("\n");
            } else if (WiFi.getMode() == WIFI_AP) {
                 resp += F ("AP (");
                 resp += WiFi.softAPmacAddress();
                 resp += F (")");
                 resp += F ("\n");
                 wifi_config_t conf;
                 esp_wifi_get_config (ESP_IF_WIFI_AP, &conf);
                 resp += F ("SSID: ");
                 resp += (const char*) conf.ap.ssid;
                 resp += F ("\n");
                 resp += F ("Visible: ");
                 resp += (conf.ap.ssid_hidden == 0) ? F ("Yes") : F ("No");
                 resp += F ("\n");
                 resp += F ("Authentication: ");
                 if (conf.ap.authmode == WIFI_AUTH_OPEN) {
                    resp += F ("None");
                 } else if (conf.ap.authmode == WIFI_AUTH_WEP) {
                    resp += F ("WEP");
                 } else if (conf.ap.authmode == WIFI_AUTH_WPA_PSK) {
                    resp += F ("WPA");
                 } else if (conf.ap.authmode == WIFI_AUTH_WPA2_PSK) {
                    resp += F ("WPA2");
                 } else {
                    resp += F ("WPA/WPA2");
                 }
                resp += F ("\n");
                resp += F ("Max Connections: ");
                resp += String(conf.ap.max_connection);
                resp += F ("\n");
                resp += F ("DHCP Server: ");
                tcpip_adapter_dhcp_status_t dhcp_status;
                tcpip_adapter_dhcps_get_status (TCPIP_ADAPTER_IF_AP, &dhcp_status);
                if (dhcp_status == TCPIP_ADAPTER_DHCP_STARTED)resp += F ("Started");
                else resp += F ("Stopped");
                resp += F ("\n");
                resp += F ("IP: ");
                resp += WiFi.softAPIP().toString();
                resp += F ("\n");
                tcpip_adapter_ip_info_t ip_AP;
                tcpip_adapter_get_ip_info (TCPIP_ADAPTER_IF_AP, &ip_AP);
                resp += F ("Gateway: ");
                resp += IPAddress (ip_AP.gw.addr).toString();
                resp += F ("\n");
                resp += F ("Mask: ");
                resp += IPAddress (ip_AP.netmask.addr).toString();
                resp += F ("\n");
                resp += F ("Connected clients: ");
                wifi_sta_list_t station;
                tcpip_adapter_sta_list_t tcpip_sta_list;
                esp_wifi_ap_get_sta_list (&station);
                tcpip_adapter_get_sta_list (&station, &tcpip_sta_list);
                resp += String(station.num);
                resp += F ("\n");
                for (int i = 0; i < station.num; i++) {
                    resp +=  mac2str (tcpip_sta_list.sta[i].mac);
                    resp +=  F (" ");
                    resp +=  IPAddress (tcpip_sta_list.sta[i].ip.addr).toString();
                    resp += F ("\n");
                    }
                resp += F ("Disabled Mode: ");
                resp += F ("STA (");
                resp += WiFi.macAddress();
                resp += F (")");
                resp += F ("\n");
            } else if (WiFi.getMode() == WIFI_AP_STA) //we should not be in this state but just in case ....
            {
               resp += F ("Mixed");
               resp += F ("\n");
               resp += F ("STA (");
               resp += WiFi.macAddress();
               resp += F (")");
               resp += F ("\n");
               resp += F ("AP (");
               resp += WiFi.softAPmacAddress();
               resp += F (")");
               resp += F ("\n");
        
            } else { //we should not be there if no wifi ....
               resp += F ("Wifi Off");
               resp += F ("\n");
            }
            //TODO to complete
            resp += F ("FW version: ");
            resp += SHORT_BUILD_VERSION;
            resp += F (" (ESP32)");
            if (espresponse)espresponse->println (resp.c_str());
            }
            break;
        //get fw version / fw target / hostname / authentication
        //[ESP800]
        case 800: 
            resp = "FW version:";
            resp += SHORT_BUILD_VERSION;
            resp += " # FW target:marlin-embedded  # FW HW:Direct SD  # primary sd:/sd # secondary sd:none # authentication:";
            #ifdef AUTHENTICATION_FEATURE
            resp += "yes";
            #else
            resp += "no";
            #endif
            resp += " # webcommunication: Async # hostname:";
            resp += http_hostname;
            if (WiFi.getMode() == WIFI_AP)resp += "(AP mode)";
            if (espresponse)espresponse->println (resp.c_str());
            break;
        default:
            if (espresponse)espresponse->println ("Error: Incorrect Command");
            response = false;
            break;
    }
    return response;
}

//Handlers

//Not found Page handler
void handle_not_found (AsyncWebServerRequest *request)
{
    //if we are here it means no index.html
    if (request->url() == "/") {
        AsyncWebServerResponse * response = request->beginResponse_P (200, "text/html", PAGE_NOFILES, PAGE_NOFILES_SIZE);
        response->addHeader ("Content-Encoding", "gzip");
        request->send (response);
    } else {
        String path = F ("/404.htm");
        String pathWithGz =  path + F (".gz");
        if (SPIFFS.exists (pathWithGz) || SPIFFS.exists (path) ) {
            request->send (SPIFFS, path);
        } else {
            //if not template use default page
            String contentpage = FPSTR (PAGE_404);
            String stmp;
            if (WiFi.getMode() == WIFI_STA ) {
                stmp = WiFi.localIP().toString();
            } else {
                stmp = WiFi.softAPIP().toString();
            }
            //Web address = ip + port
            String KEY_IP = F ("$WEB_ADDRESS$");
            String KEY_QUERY = F ("$QUERY$");
            if (http_port != 80) {
                stmp += ":";
                stmp += String(http_port);
            }
            contentpage.replace (KEY_IP, stmp);
            contentpage.replace (KEY_QUERY, request->url() );
            request->send (404, "text/html", contentpage.c_str() );
        }
    }
}

//Web Update handler 
void handleUpdate (AsyncWebServerRequest *request)
{
    level_authenticate_type auth_level = is_authenticated();
    if (auth_level != LEVEL_ADMIN) {
        _upload_status = UPLOAD_STATUS_NONE;
        request->send (403, "text/plain", "Not allowed, log in first!\n");
        return;
    }
    String jsonfile = "{\"status\":\"" ;
    jsonfile += String(_upload_status);
    jsonfile += "\"}";
    //send status
    AsyncResponseStream  *response = request->beginResponseStream ("application/json");
    response->addHeader ("Cache-Control", "no-cache");
    response->print (jsonfile.c_str() );
    request->send (response);
    //if success restart
    if (_upload_status == UPLOAD_STATUS_SUCCESSFUL) {
        restart_ESP_module = true;
    } else {
        _upload_status = UPLOAD_STATUS_NONE;
    }
}

//File upload for Web update
void WebUpdateUpload (AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    static size_t last_upload_update;
    static size_t totalSize;
    static uint32_t maxSketchSpace ;
    //only admin can update FW
    if (is_authenticated() != LEVEL_ADMIN) {
        _upload_status = UPLOAD_STATUS_CANCELLED;
        request->client()->abort();
        SERIAL_ERROR_START();
        SERIAL_ERRORLNPGM("Upload rejected");
        return;
    }
    //Upload start
    //**************
    if (!index) {
        MYSERIAL0.println (F ("Update Firmware"));
        _upload_status = UPLOAD_STATUS_ONGOING;

        //Not sure can do OTA on 2Mb board if conf file is modified
        maxSketchSpace = (ESP.getFlashChipSize() > 0x20000) ? 0x140000 : 0x140000 / 2;
        last_upload_update = 0;
        totalSize = 0;
        if (!Update.begin (maxSketchSpace) ) { //start with max available size
            _upload_status = UPLOAD_STATUS_CANCELLED;
            SERIAL_ERROR_START();
            SERIAL_ERRORLNPGM("Upload cancelled");
            request->client()->abort();
            return;
        } else {
            MYSERIAL0.println (F ("Update 0%"));
        }
    }
    //Upload write
    //**************
    if (_upload_status == UPLOAD_STATUS_ONGOING) {
        //we do not know the total file size yet but we know the available space so let's use it
        totalSize += len;
        if ( ( (100 * totalSize) / maxSketchSpace) != last_upload_update) {
            last_upload_update = (100 * totalSize) / maxSketchSpace;
            String s = "Update ";
            s+= String(last_upload_update);
            s+="%";
            MYSERIAL0.println (s);
        }
        if (Update.write (data, len) != len) {
            _upload_status = UPLOAD_STATUS_CANCELLED;
            SERIAL_ERROR_START();
            SERIAL_ERRORLNPGM("Upload error");
            Update.end();
            request->client()->abort();
        }
    }
    //Upload end
    //**************
    if (final) {
        String  sizeargname  = filename + "S";
        if (request->hasArg (sizeargname.c_str()) ) {
             if (request->arg (sizeargname.c_str()) != String(totalSize)) {
                 _upload_status = UPLOAD_STATUS_FAILED;
                 SERIAL_ERROR_START();
                 SERIAL_ERRORLNPGM("Upload rejected");
                 Update.end();
                 request->client()->abort();
             }
        }
        if (Update.end (true) ) { //true to set the size to the current progress
            //Update is done
            MYSERIAL0.println (F ("Update 100%"));
            _upload_status = UPLOAD_STATUS_SUCCESSFUL;
        } 
    }
}

//SD File upload with direct access to SD///////////////////////////////
void SDFile_direct_upload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    //TODO use the tmp obj of the request instead
    static ESP_SD sdfile;
    //Guest cannot upload - only admin
    if (is_authenticated() != LEVEL_ADMIN) {
        _upload_status = UPLOAD_STATUS_CANCELLED;
        request->client()->abort();
        return;
    }
    String upload_filename = filename;
    if (upload_filename[0] != '/') {
        upload_filename = "/" + filename;
    }

    //Upload start
    //**************
    if (!index)
     {
        upload_filename=  sdfile.makepath83(upload_filename);
        if ( sdfile.card_status() != 1) {
            _upload_status=UPLOAD_STATUS_CANCELLED;
            request->client()->abort();
            return;
        }
        if (sdfile.exists (upload_filename.c_str()) ) {
            sdfile.remove (upload_filename.c_str());
        } 
        
        if (sdfile.isopen())sdfile.close();
        if (!sdfile.open (upload_filename.c_str(),false)) {
            request->client()->abort();
            _upload_status = UPLOAD_STATUS_FAILED;
            return ;
        } else {
            _upload_status = UPLOAD_STATUS_ONGOING;
        }
     }
      //Upload write
    //**************
    //we need to check SD is inside
    if ( sdfile.card_status() != 1) {
            sdfile.close();
            request->client()->abort();
            _upload_status = UPLOAD_STATUS_FAILED;
            return;
        }
    if (sdfile.isopen()) {
        if ( (_upload_status = UPLOAD_STATUS_ONGOING) && len) {
            sdfile.write (data, len);
        }
    }
    //Upload end
    //**************
    if(final)
     {
         sdfile.close();
         uint32_t filesize = sdfile.size();
        String  sizeargname  = filename + "S";
        if (request->hasArg (sizeargname.c_str()) ) {
            if (request->arg (sizeargname.c_str()) != String(filesize)) {
                _upload_status = UPLOAD_STATUS_FAILED;
                }
            } 
        if (_upload_status == UPLOAD_STATUS_ONGOING) {
            _upload_status = UPLOAD_STATUS_SUCCESSFUL;
        }
    }
}

//direct SD files list//////////////////////////////////////////////////
void handle_direct_SDFileList(AsyncWebServerRequest *request)
{
    //this is only for admin an user
    if (is_authenticated() == LEVEL_GUEST) {
        _upload_status=UPLOAD_STATUS_NONE;
        request->send(401, "application/json", "{\"status\":\"Authentication failed!\"}");
        return;
    }
    

    String path="/";
    String sstatus="Ok";
    if ((_upload_status == UPLOAD_STATUS_FAILED) || (_upload_status == UPLOAD_STATUS_CANCELLED)) {
        sstatus = "Upload failed";
        _upload_status = UPLOAD_STATUS_NONE;
    }
    bool list_files = true;
    ESP_SD card;
    int8_t state = card.card_status();
    if (state != 1){
        request->send(200, "application/json", "{\"status\":\"No SD Card\"}");
        return;
    }

    //get current path
    if(request->hasArg("path")) {
        path += request->arg("path") ;
    }
    //to have a clean path
    path.trim();
    path.replace("//","/");
    
    //check if query need some action
    if(request->hasArg("action")) {
        //delete a file
        if(request->arg("action") == "delete" && request->hasArg("filename")) {
            String filename;
            String shortname = request->arg("filename");
            filename = path + shortname;
            shortname.replace("/","");
            filename.replace("//","/");
            
            if(!card.exists(filename.c_str())) {
                sstatus = shortname + F(" does not exist!");
            } else {
                if (card.remove(filename.c_str())) {
                    sstatus = shortname + F(" deleted");
                } else {
                    sstatus = F("Cannot deleted ") ;
                    sstatus+=shortname ;
                }
            }
        }
        //delete a directory
        if( request->arg("action") == "deletedir" &&  request->hasArg("filename")) {
            String filename;
            String shortname = request->arg("filename");
            shortname.replace("/","");
            filename = path + "/" + shortname;
            filename.replace("//","/");
            if (filename != "/") {
                if(!card.dir_exists(filename.c_str())) {
                    sstatus = shortname + F(" does not exist!");
                } else {
                    if (!card.rmdir(filename.c_str())) {
                        sstatus ="Error deleting: ";
                        sstatus += shortname ;
                    } else {
                        sstatus = shortname ;
                        sstatus+=" deleted";
                    }
                }
            } else {
                sstatus ="Cannot delete root";
            }
        }
        //create a directory
        if( request->arg("action")=="createdir" &&  request->hasArg("filename")) {
            String filename;
            String shortname =  request->arg("filename"); 
            filename = path + shortname;
            shortname.replace("/","");
            filename.replace("//","/");
            if(card.exists(filename.c_str())) {
                sstatus = shortname + F(" already exists!");
            } else {
                if (!card.mkdir(filename.c_str())) {
                    sstatus = F("Cannot create ");
                    sstatus += shortname ;
                } else {
                    sstatus = shortname + F(" created");
                }
            }
        }  
    }
  
    //check if no need build file list
    if( request->hasArg("dontlist")) {
        if( request->arg("dontlist") == "yes") {
            list_files = false;
        }
    }
    String jsonfile = "{" ;
    jsonfile+="\"files\":[";
    if (!card.openDir(path)){
        String s =  "{\"status\":\" ";
        s += path;
        s+=  " does not exist on SD Card\"}";
        request->send(200, "application/json", s.c_str());
        return;
    }
    if (list_files) {
        char name[13];
        uint32_t size;
        bool isFile;
        uint i = 0;
        while (card.readDir(name,&size ,&isFile)) {
            if (i>0) {
                jsonfile+=",";
            }
            jsonfile+="{\"name\":\"";
            jsonfile+=name;
            jsonfile+="\",\"shortname\":\"";
            jsonfile+=name;
            jsonfile+="\",\"size\":\"";
            if (isFile)jsonfile+=formatBytes(size);
            else jsonfile+="-1";
            jsonfile+="\",\"datetime\":\""; 
            //TODO datatime
            jsonfile+="\"}";
            i++;
            ESP_wait (1);
        }
        jsonfile+="],\"path\":\"";
        jsonfile+=path + "\",";
    }
    static uint32_t volTotal = card.card_total_space();
    static uint32_t volUsed = card.card_used_space();;
    //TODO 
    //Get right values
    uint32_t  occupedspace = (volUsed/volTotal)*100;
    jsonfile+="\"total\":\"";
    if ( (occupedspace <= 1) && (volTotal!=volUsed)) {
            occupedspace=1;
        }
    jsonfile+= formatBytes(volTotal); ;
    
    jsonfile+="\",\"used\":\"";
    jsonfile+= formatBytes(volUsed); ;
    jsonfile+="\",\"occupation\":\"";
    jsonfile+=String(occupedspace);
    jsonfile+= "\",";

    jsonfile+= "\"mode\":\"direct\",";
    jsonfile+= "\"status\":\"";
    jsonfile+=sstatus + "\"";
    jsonfile+= "}";
    
    AsyncResponseStream  *response = request->beginResponseStream ("application/json");
    response->addHeader ("Cache-Control", "no-cache");
    response->print (jsonfile.c_str() );
    request->send (response);
    _upload_status=UPLOAD_STATUS_NONE;
}

//direct SD Content
void handle_SDCARD(AsyncWebServerRequest *request)
{
    String filepath=request->url();
    request->_tempObject = new ESP_SD;
    //in case of disconnection
    request->onDisconnect([request](){
            if(request->_tempObject) delete  ((ESP_SD *)(request->_tempObject));
            request->_tempObject = NULL;
        });
    bool file_exists=false;
    filepath.replace("/SD", "");
    if (((ESP_SD *)(request->_tempObject))->exists (filepath.c_str())){
        if (((ESP_SD *)(request->_tempObject))->open(filepath.c_str())){
               
               String ContentType = getContentType(filepath);
               AsyncWebServerResponse *response = request->beginResponse (ContentType.c_str(),  ((ESP_SD *)(request->_tempObject))->size(), [request](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                                                       //better to chop for long download
                                                       size_t packetsize = (maxLen > 1200)?1200:maxLen;
                                                       size_t readsize = 0;
                                                       readsize = ((ESP_SD *)request->_tempObject)->read(buffer, packetsize);
                                                       if((index + readsize) >= (((ESP_SD *)request->_tempObject)->size())){
                                                        //close file
                                                       ((ESP_SD *)request->_tempObject)->close();
                                                       delete ((ESP_SD *)(request->_tempObject));
                                                       request->_tempObject = NULL;     
                                                      }
                                                      return readsize;
                                                    });
            response->addHeader ("Cache-Control", "no-cache");
            request->send (response);
            file_exists=true;
        } 
    } 
        
   if (!file_exists) {
       handle_not_found(request);
       delete ((ESP_SD *)(request->_tempObject));
       request->_tempObject = NULL;
    }
}


//http SSDP xml presentation
void handle_SSDP (AsyncWebServerRequest *request)
{
    StreamString sschema ;
    if (sschema.reserve (1024) ) {
        String templ =  "<?xml version=\"1.0\"?>"
                        "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
                        "<specVersion>"
                        "<major>1</major>"
                        "<minor>0</minor>"
                        "</specVersion>"
                        "<URLBase>http://%s:%u/</URLBase>"
                        "<device>"
                        "<deviceType>upnp:rootdevice</deviceType>"
                        "<friendlyName>%s</friendlyName>"
                        "<presentationURL>/</presentationURL>"
                        "<serialNumber>%s</serialNumber>"
                        "<modelName>ESP32</modelName>"
                        "<modelNumber>Marlin</modelNumber>"
                        "<modelURL>http://espressif.com/en/products/hardware/esp-wroom-32/overview</modelURL>"
                        "<manufacturer>Espressif Systems</manufacturer>"
                        "<manufacturerURL>http://espressif.com</manufacturerURL>"
                        "<UDN>uuid:%s</UDN>"
                        "</device>"
                        "</root>\r\n"
                        "\r\n";
        char uuid[37];
        String sip = WiFi.localIP().toString();
        uint32_t chipId = (uint16_t) (ESP.getEfuseMac() >> 32);
        sprintf (uuid, "38323636-4558-4dda-9188-cda0e6%02x%02x%02x",
                 (uint16_t) ( (chipId >> 16) & 0xff),
                 (uint16_t) ( (chipId >>  8) & 0xff),
                 (uint16_t)   chipId        & 0xff  );
        String serialNumber = String (chipId);
        sschema.printf (templ.c_str(),
                        sip.c_str(),
                        http_port,
                        http_hostname.c_str(),
                        serialNumber.c_str(),
                        uuid);
        request->send (200, "text/xml", (String) sschema);
    } else {
        request->send (500);
    }
}

//login status check 
void handle_login(AsyncWebServerRequest *request)
{
#ifdef AUTHENTICATION_FEATURE
#else
	AsyncWebServerResponse * response = request->beginResponse (200, "application/json", "{\"status\":\"Ok\",\"authentication_lvl\":\"admin\"}");
	response->addHeader("Cache-Control","no-cache");
    request->send(response);
#endif
}

//filter to intercept command line on root
bool filterOnRoot (AsyncWebServerRequest *request)
{
    if (request->hasArg ("forcefallback") ) {
        String stmp = request->arg ("forcefallback");
        //to use all case
        stmp.toLowerCase();
        if ( stmp == "yes") {
            return false;
        }
    }
    return true;
}

//SPIFFS
//SPIFFS files list and file commands
void handleFileList (AsyncWebServerRequest *request)
{
    level_authenticate_type auth_level = is_authenticated();
    if (auth_level == LEVEL_GUEST) {
        _upload_status = UPLOAD_STATUS_NONE;
        request->send (401, "text/plain", "Authentication failed!\n");
        return;
    }
    String path ;
    String status = "Ok";
    if ( (_upload_status == UPLOAD_STATUS_FAILED) || (_upload_status == UPLOAD_STATUS_CANCELLED) ) {
        status = "Upload failed";
    }
    //be sure root is correct according authentication
    if (auth_level == LEVEL_ADMIN) {
        path = "/";
    } else {
        path = "/user";
    }
    //get current path
    if (request->hasArg ("path") ) {
        path += request->arg ("path") ;
    }
    //to have a clean path
    path.trim();
    path.replace ("//", "/");
    if (path[path.length() - 1] != '/') {
        path += "/";
    }
    //check if query need some action
    if (request->hasArg ("action") ) {
        //delete a file
        if (request->arg ("action") == "delete" && request->hasArg ("filename") ) {
            String filename;
            String shortname = request->arg ("filename");
            shortname.replace ("/", "");
            filename = path + request->arg ("filename");
            filename.replace ("//", "/");
            if (!SPIFFS.exists (filename) ) {
                status = shortname + F (" does not exists!");
            } else {
                if (SPIFFS.remove (filename) ) {
                    status = shortname + F (" deleted");
                    //what happen if no "/." and no other subfiles ?
                    String ptmp = path;
                    if ( (path != "/") && (path[path.length() - 1] = '/') ) {
                        ptmp = path.substring (0, path.length() - 1);
                    }
                    File dir = SPIFFS.open (ptmp);
                    File dircontent = dir.openNextFile();
                    if (!dircontent) {
                        //keep directory alive even empty
                        File r = SPIFFS.open (path + "/.", FILE_WRITE);
                        if (r) {
                            r.close();
                        }
                    }
                } else {
                    status = F ("Cannot deleted ") ;
                    status += shortname ;
                }
            }
        }
        //delete a directory
        if (request->arg ("action") == "deletedir" && request->hasArg ("filename") ) {
            String filename;
            String shortname = request->arg ("filename");
            shortname.replace ("/", "");
            filename = path + request->arg ("filename");
            filename += "/";
            filename.replace ("//", "/");
            if (filename != "/") {
                bool delete_error = false;
                File dir = SPIFFS.open (path + shortname);
                {
                    File file2deleted = dir.openNextFile();
                    while (file2deleted) {
                        String fullpath = file2deleted.name();
                        if (!SPIFFS.remove (fullpath) ) {
                            delete_error = true;
                            status = F ("Cannot deleted ") ;
                            status += fullpath;
                        }
                        file2deleted = dir.openNextFile();
                    }
                }
                if (!delete_error) {
                    status = shortname ;
                    status += " deleted";
                }
            }
        }
        //create a directory
        if (request->arg ("action") == "createdir" && request->hasArg ("filename") ) {
            String filename;
            filename = path + request->arg ("filename") + "/.";
            String shortname = request->arg ("filename");
            shortname.replace ("/", "");
            filename.replace ("//", "/");
            if (SPIFFS.exists (filename) ) {
                status = shortname + F (" already exists!");
            } else {
                File r = SPIFFS.open (filename, FILE_WRITE);
                if (!r) {
                    status = F ("Cannot create ");
                    status += shortname ;
                } else {
                    r.close();
                    status = shortname + F (" created");
                }
            }
        }
    }
    String jsonfile = "{";
    String ptmp = path;
    if ( (path != "/") && (path[path.length() - 1] = '/') ) {
        ptmp = path.substring (0, path.length() - 1);
    }
    File dir = SPIFFS.open (ptmp);
    jsonfile += "\"files\":[";
    bool firstentry = true;
    String subdirlist = "";
    File fileparsed = dir.openNextFile();
    while (fileparsed) {
        String filename = fileparsed.name();
        String size = "";
        bool addtolist = true;
        //remove path from name
        filename = filename.substring (path.length(), filename.length() );
        //check if file or subfile
        if (filename.indexOf ("/") > -1) {
            //Do not rely on "/." to define directory as SPIFFS upload won't create it but directly files
            //and no need to overload SPIFFS if not necessary to create "/." if no need
            //it will reduce SPIFFS available space so limit it to creation
            filename = filename.substring (0, filename.indexOf ("/") );
            String tag = "*";
            tag += filename + "*";
            if (subdirlist.indexOf (tag) > -1 || filename.length() == 0) { //already in list
                addtolist = false; //no need to add
            } else {
                size = "-1"; //it is subfile so display only directory, size will be -1 to describe it is directory
                if (subdirlist.length() == 0) {
                    subdirlist += "*";
                }
                subdirlist += filename + "*"; //add to list
            }
        } else {
            //do not add "." file
            if (! ( (filename == ".") || (filename == "") ) ) {
                size = formatBytes (fileparsed.size() );
            } else {
                addtolist = false;
            }
        }
        if (addtolist) {
            if (!firstentry) {
                jsonfile += ",";
            } else {
                firstentry = false;
            }
            jsonfile += "{";
            jsonfile += "\"name\":\"";
            jsonfile += filename;
            jsonfile += "\",\"size\":\"";
            jsonfile += size;
            jsonfile += "\"";
            jsonfile += "}";
        }
        fileparsed = dir.openNextFile();
    }
    jsonfile += "],";
    jsonfile += "\"path\":\"" + path + "\",";
    jsonfile += "\"status\":\"" + status + "\",";
    size_t totalBytes;
    size_t usedBytes;
    totalBytes = SPIFFS.totalBytes();
    usedBytes = SPIFFS.usedBytes();
    jsonfile += "\"total\":\"" + formatBytes (totalBytes) + "\",";
    jsonfile += "\"used\":\"" + formatBytes (usedBytes) + "\",";
    jsonfile.concat (F ("\"occupation\":\"") );
    jsonfile += String (100 * usedBytes / totalBytes);
    jsonfile += "\"";
    jsonfile += "}";
    path = "";
    AsyncResponseStream  *response = request->beginResponseStream ("application/json");
    response->addHeader ("Cache-Control", "no-cache");
    response->print (jsonfile.c_str() );
    request->send (response);
    _upload_status = UPLOAD_STATUS_NONE;
}

 //SPIFFS files uploader handle
void SPIFFSFileupload (AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
     //get authentication status
    level_authenticate_type auth_level= is_authenticated();
    //Guest cannot upload - only admin
    if (auth_level == LEVEL_GUEST) {
        _upload_status = UPLOAD_STATUS_CANCELLED;
        SERIAL_ERROR_START();
        SERIAL_ERRORLNPGM("Upload rejected");
        request->client()->abort();
        return;
    }
    String upload_filename = filename;
    if (upload_filename[0] != '/') {
        upload_filename = "/" + filename;
    }
    if(auth_level != LEVEL_ADMIN) {
		String filename = upload_filename;
        upload_filename = "/user" + filename;
        }
    //Upload start
    //**************
    if (!index) {
        if (SPIFFS.exists (upload_filename) ) {
            SPIFFS.remove (upload_filename);
        }
        if (request->_tempFile) {
            request->_tempFile.close();
        }
        request->_tempFile = SPIFFS.open (upload_filename, FILE_WRITE);
        if (!request->_tempFile) {
            request->client()->abort();
            _upload_status = UPLOAD_STATUS_FAILED;
        } else {
            _upload_status = UPLOAD_STATUS_ONGOING;
        }

    }
    //Upload write
    //**************
    if ( request->_tempFile) {
        if ( (_upload_status = UPLOAD_STATUS_ONGOING) && len) {
            request->_tempFile.write (data, len);
        }
    }
    //Upload end
    //**************
    if (final) {
        request->_tempFile.flush();
        request->_tempFile.close();
        request->_tempFile = SPIFFS.open (upload_filename, FILE_READ);
        uint32_t filesize = request->_tempFile.size();
        request->_tempFile.close();
        String  sizeargname  = filename + "S";
        if (request->hasArg (sizeargname.c_str()) ) {
            if (request->arg (sizeargname.c_str()) != String(filesize)) {
                _upload_status = UPLOAD_STATUS_FAILED;
                SPIFFS.remove (upload_filename);
                }
            } 
        if (_upload_status == UPLOAD_STATUS_ONGOING) {
            _upload_status = UPLOAD_STATUS_SUCCESSFUL;
        }
    }
}


//Handle web command query and send answer//////////////////////////////
void handle_web_command (AsyncWebServerRequest *request)
{
     //to save time if already disconnected
     if (request->hasArg ("PAGEID") ) {
        if (request->arg ("PAGEID").length() > 0 ) {
           if (request->arg ("PAGEID").toInt() != id_connection) {
           request->send (200, "text/plain", "Invalid command");
           return;
           }
        }
    }
    level_authenticate_type auth_level = is_authenticated();
    String cmd = "";
    if (request->hasArg ("plain") || request->hasArg ("commandText") ) {
        if (request->hasArg ("plain") ) {
            cmd = request->arg ("plain");
        } else {
            cmd = request->arg ("commandText");
        }
    } else {
        request->send (200, "text/plain", "Invalid command");
        return;
    }
    //if it is internal command [ESPXXX]<parameter>
    cmd.trim();
    int ESPpos = cmd.indexOf ("[ESP");
    if (ESPpos > -1) {
        //is there the second part?
        int ESPpos2 = cmd.indexOf ("]", ESPpos);
        if (ESPpos2 > -1) {
            //Split in command and parameters
            String cmd_part1 = cmd.substring (ESPpos + 4, ESPpos2);
            String cmd_part2 = "";
            //only [ESP800] is allowed login free if authentication is enabled
            if ( (auth_level == LEVEL_GUEST)  && (cmd_part1.toInt() != 800) ) {
                request->send (401, "text/plain", "Authentication failed!\n");
                return;
            }
            //is there space for parameters?
            if (ESPpos2 < cmd.length() ) {
                cmd_part2 = cmd.substring (ESPpos2 + 1);
            }
            //if command is a valid number then execute command
            if (cmd_part1.toInt() != 0) {
                AsyncResponseStream  *response = request->beginResponseStream ("text/html");
                response->addHeader ("Cache-Control", "no-cache");
                //commmand is web only 
                execute_internal_command (cmd_part1.toInt(), cmd_part2, auth_level, response);
                request->send (response);
            }
            //if not is not a valid [ESPXXX] command
        }
    } else { //execute GCODE
        if (auth_level == LEVEL_GUEST) {
            request->send (401, "text/plain", "Authentication failed!\n");
            return;
        }
        //Instead of send several commands one by one by web  / send full set and split here
        String scmd;
        uint8_t sindex = 0;
        scmd = get_Splited_Value(cmd,'\n', sindex);
        while ( scmd !="" ){
        enqueue_and_echo_command(scmd.c_str());
        sindex++;
        scmd = get_Splited_Value(cmd,'\n', sindex);
        }
        request->send (200, "text/plain", "ok");
    }
}


//Handle web command query and send answer//////////////////////////////
void handle_web_command_silent (AsyncWebServerRequest *request)
{
     //to save time if already disconnected
     if (request->hasArg ("PAGEID") ) {
        if (request->arg ("PAGEID").length() > 0 ) {
           if (request->arg ("PAGEID").toInt() != id_connection) {
           request->send (200, "text/plain", "Invalid command");
           return;
           }
        }
    }
    level_authenticate_type auth_level = is_authenticated();
    String cmd = "";
    if (request->hasArg ("plain") || request->hasArg ("commandText") ) {
        if (request->hasArg ("plain") ) {
            cmd = request->arg ("plain");
        } else {
            cmd = request->arg ("commandText");
        }
    } else {
        request->send (200, "text/plain", "Invalid command");
        return;
    }
    //if it is internal command [ESPXXX]<parameter>
    cmd.trim();
    int ESPpos = cmd.indexOf ("[ESP");
    if (ESPpos > -1) {
        //is there the second part?
        int ESPpos2 = cmd.indexOf ("]", ESPpos);
        if (ESPpos2 > -1) {
            //Split in command and parameters
            String cmd_part1 = cmd.substring (ESPpos + 4, ESPpos2);
            String cmd_part2 = "";
            //only [ESP800] is allowed login free if authentication is enabled
            if ( (auth_level == LEVEL_GUEST)  && (cmd_part1.toInt() != 800) ) {
                request->send (401, "text/plain", "Authentication failed!\n");
                return;
            }
            //is there space for parameters?
            if (ESPpos2 < cmd.length() ) {
                cmd_part2 = cmd.substring (ESPpos2 + 1);
            }
            //if command is a valid number then execute command
            if (cmd_part1.toInt() != 0) {
                //commmand is web only 
                execute_internal_command (cmd_part1.toInt(), cmd_part2, auth_level, NULL);
                request->send (200, "text/plain", "ok");
            }
            //if not is not a valid [ESPXXX] command
        }
    } else { //execute GCODE
        if (auth_level == LEVEL_GUEST) {
            request->send (401, "text/plain", "Authentication failed!\n");
            return;
        }
        //Instead of send several commands one by one by web  / send full set and split here
        String scmd;
        uint8_t sindex = 0;
        scmd = get_Splited_Value(cmd,'\n', sindex);
        while ( scmd !="" ){
        enqueue_and_echo_command(scmd.c_str());
        sindex++;
        scmd = get_Splited_Value(cmd,'\n', sindex);
        }
        request->send (200, "text/plain", "ok");
    }
}



//on event connect function
void handle_onevent_connect(AsyncEventSourceClient *client) 
{       
        if (!client->lastId()){
            //Init active ID
            id_connection++;
            client->send(String(id_connection).c_str(), "InitID", id_connection, 1000);
            //Dispatch who is active ID
            web_events->send( String(id_connection).c_str(),"ActiveID");        
            }
}

void handle_Websocket_Event(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  //Handle WebSocket event
}

//Start http function
void http_start(){
   ESP_preferences.begin(NAMESPACE, true);
   int8_t penabled = ESP_preferences.getChar(HTTP_ENABLE_ENTRY, 0);
   //Get http port
   http_port = ESP_preferences.getUShort(HTTP_PORT_ENTRY, DEFAULT_WEBSERVER_PORT);
   //Get hostname
   String defV = DEFAULT_HOSTNAME;
   ESP_preferences.begin(NAMESPACE, true);
   http_hostname = ESP_preferences.getString(HOSTNAME_ENTRY, defV);
   ESP_preferences.end();
   if (penabled == 0) return;
   //create instance
   webserver= new AsyncWebServer(http_port);
   //add mDNS
   MDNS.addService("http","tcp",http_port);
   
   web_events = new AsyncEventSource("/events");
   web_socket = new AsyncWebSocket("/ws");
   Serial2Socket.attachWS(web_socket);
    
   //Web server handlers
   //trick to catch command line on "/" before file being processed
   webserver->serveStatic ("/", SPIFFS, "/").setDefaultFile ("index.html").setFilter (filterOnRoot);
   webserver->serveStatic ("/", SPIFFS, "/Nowhere");
    
   //Page not found handler
   webserver->onNotFound (handle_not_found);
   
   //SSDP service presentation
   webserver->on ("/description.xml", HTTP_GET, handle_SSDP);
   
   //need to be there even no authentication to say to UI no authentication
   webserver->on("/login", HTTP_ANY, handle_login);
   
   //SPIFFS
   webserver->on ("/files", HTTP_ANY, handleFileList, SPIFFSFileupload);
   
   //web update
   webserver->on ("/updatefw", HTTP_ANY, handleUpdate, WebUpdateUpload);
   
   //web commands
   webserver->on ("/command", HTTP_ANY, handle_web_command);
   webserver->on ("/command_silent", HTTP_ANY, handle_web_command_silent);
    
   //Direct SD management
    webserver->on("/upload", HTTP_ANY, handle_direct_SDFileList,SDFile_direct_upload);
    webserver->on("/SD", HTTP_ANY, handle_SDCARD);
    
    //events functions
    web_events->onConnect(handle_onevent_connect);
    //events management
    webserver->addHandler(web_events);

    //Websocket function
    web_socket->onEvent(handle_Websocket_Event);
    //Websocket management
    webserver->addHandler(web_socket);
   
   //Add specific for SSDP
   SSDP.setSchemaURL ("description.xml");
   SSDP.setHTTPPort (http_port);
   SSDP.setName (http_hostname);
   SSDP.setURL ("/");
   SSDP.setDeviceType ("upnp:rootdevice");
    /*Any customization could be here
    SSDP.setModelName (ESP32_MODEL_NAME);
    SSDP.setModelURL (ESP32_MODEL_URL);
    SSDP.setModelNumber (ESP_MODEL_NUMBER);
    SSDP.setManufacturer (ESP_MANUFACTURER_NAME);
    SSDP.setManufacturerURL (ESP_MANUFACTURER_URL);
    */
        
    //Start SSDP
    if (WiFi.getMode() != WIFI_AP ) {
        MYSERIAL0.println("SSDP Started");
        SSDP.begin();
    }
    MYSERIAL0.println("HTTP Started");
#if NUM_SERIAL > 1
   MYSERIAL1.println("HTTP Started");
#endif 
   //start webserver
   webserver->begin();

}

//stop http function
void http_stop(){
    if (webserver) {
        
        webserver->reset();
        delete webserver;
        }
    webserver = NULL;
    if (web_events){
        web_events->close();
        delete web_events;
    }
    web_events = NULL;
    if (web_socket){
        Serial2Socket.detachWS();
        web_socket->closeAll();
        delete web_socket;
    }
    web_socket = NULL;
    //TODO what about mDNS service on http port?
    
}



#endif // WIFISUPPORT

#endif // ARDUINO_ARCH_ESP32
