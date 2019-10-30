#ifdef ARDUINO_ARCH_ESP32

#include "../../inc/MarlinConfigPre.h"

#if ENABLED(WIFISUPPORT)

#include "../../gcode/gcode.h"
#include "../../gcode/parser.h"
#include "HAL.h"
#include <WiFi.h>
#include <Preferences.h>
#include "../../core/serial.h"

#if NUM_SERIAL > 1
  #include "../../gcode/queue.h"
#endif
#include "wifi_ESP32.h"

extern Preferences ESP_preferences;

/**
 * Helper for invalid msg
 */
void error_msg_invalid(const char * title, const char * item){
    #if NUM_SERIAL > 1
    const int16_t port = command_queue_port[cmd_queue_index_r];
    #endif
    SERIAL_ECHO_START_P(port);
    SERIAL_ECHO_P(port,title);
    SERIAL_ECHO_P(port, " '");
    SERIAL_ECHO_P(port, item);
    SERIAL_ECHO_P(port, "' is not valid");
    SERIAL_EOL_P(port);
}

void print_msg(const char * title, const char * item){
    #if NUM_SERIAL > 1
    const int16_t port = command_queue_port[cmd_queue_index_r];
    #endif
    SERIAL_ECHO_START_P(port);
    SERIAL_ECHO_P(port,title);
    SERIAL_ECHO_P(port, "#");
    SERIAL_ECHO_P(port, item);
    SERIAL_EOL_P(port);
}

/**
 * Get param string content with spaces defined by <Param>"<string>" 
 */
const char * getStringFromParam(const char * string, const char * param){
    //Define start param     
    String sparam= " ";
    sparam += param;
    sparam += "\"";
    //be sure string start with space
    String s = string;
    if (s[0]!=' ')s = " " + s;
    String result = "";
    //look for start
    int pos = s.indexOf(sparam);
    if (pos !=-1 && (s.length() > (pos+sparam.length()))){
        int pos2 = s.indexOf("\"", pos+sparam.length());
        result = s.substring(pos+sparam.length(),pos2);
    }
    return result.c_str();
}

/**
 * Get param string content without spaces defined by <Param><string>
 */

const char * getValueFromParam(const char * string, const char * param){
    //Define start param     
    String sparam= " ";
    sparam += param;
    //be sure string start with space
    String s = string;
    if (s[0]!=' ')s = " " + s;
    String result = "";
    //look for start
    int pos = s.indexOf(sparam);
    if (pos !=-1 && (s.length() > (pos+sparam.length()))){
        int pos2 = s.indexOf(" ", pos+sparam.length());
        //if no parameter after
        if (pos2==-1) pos2 = (s.length());
        result = s.substring(pos+sparam.length(),pos2);
    }
    return result.c_str();
}

/**
 * M585: Set/Get Hostname
 * M585<hostname> :to Set Hostname
 * M585           :to get current Hostname
 */
void GcodeSuite::M585(){
    String v = parser.string_arg;
    if (v.length() > 0){
        if (isHostnameValid(v.c_str())){
            ESP_preferences.begin(NAMESPACE, false);
            ESP_preferences.putString(HOSTNAME_ENTRY, v);
            ESP_preferences.end();
            //TODO Apply without restart
            
        } else {
            error_msg_invalid("Hostname", parser.string_arg);
            return;
        }
    } else {
            String h;
            String defV = DEFAULT_HOSTNAME;
            ESP_preferences.begin(NAMESPACE, true);
            h = ESP_preferences.getString(HOSTNAME_ENTRY, defV);
            ESP_preferences.end();
            print_msg("Hostname", h.c_str());
    }
}

/**
 * M586: Configure network protocols
 * P: 0 http (1 websocket 2 telnet)
 * S: 0 disable 1 enable 
 * R: <port value for protocol>
 * 
 */
void GcodeSuite::M586(){
    if (parser.seenval('P')) {
        int8_t p = parser.byteval('P', 0);
        int8_t s = -1;
        int16_t r = -1;
        if (parser.seenval('S')){
            s = parser.byteval('S', 0);
        }
        if (parser.seenval('R')){
            r = parser.ushortval('R', 0);
        }
        if ((s!=-1) || !((r==-1) || (r==0))){
            //Save protocol status
            String sname, rname;
            switch(p){
                case 0:
                    sname = HTTP_ENABLE_ENTRY;
                    rname = HTTP_PORT_ENTRY;
                    break;
                default: 
                    sname = p;
                    error_msg_invalid("Unknown protocol P", sname.c_str());
                    return;
                    break;
            }
            ESP_preferences.begin(NAMESPACE, false);
            if (s!=-1){
        
                ESP_preferences.putChar(sname.c_str(), s);
            }
            if (r!=-1){
                ESP_preferences.putUShort(rname.c_str(), r);
            }
            ESP_preferences.end();
            //restart servers
            StopServers();
            StartServers();
            
        } else {
            error_msg_invalid("M586", parser.string_arg);
        }
       
    } else {
        //show all protocols status
        String s;
        ESP_preferences.begin(NAMESPACE, true);
        uint8_t penabled = ESP_preferences.getChar(HTTP_ENABLE_ENTRY, 0);
        uint16_t port = ESP_preferences.getUShort(HTTP_PORT_ENTRY, DEFAULT_WEBSERVER_PORT);
        if (penabled !=0) s = "Enabled";
        else s = "Disabled";
        s+="(port:";
        s+=port;
        s+=")";
        print_msg("HTTP", s.c_str());
        ESP_preferences.end();
    }
}

/**
 * M587: Set WiFi STA host network parameters
 */
void GcodeSuite::M587(){
    String cmd = parser.string_arg;
    String error_title;
    int8_t IP_mode = DHCP_MODE;
    if (cmd.length() > 0) {
        //Sanity check for each parameter
        String SSID = getStringFromParam(cmd.c_str(), "S");
        if (!isSSIDValid(SSID.c_str())) {
            error_msg_invalid("SSID", SSID.c_str());
            return;
        }
        String password = getStringFromParam(cmd.c_str(), "P");
        if (!isPasswordValid(password.c_str())) {
            error_msg_invalid("Password", password.c_str());
            return;
        }
        String IP = getValueFromParam(cmd.c_str(), "I");
        //IP is optional if DHCP
        if (IP.length() == 0) {
            IP = "0.0.0.0";
            IP_mode = DHCP_MODE;
            }
        else IP_mode = STATIC_MODE;
        
        if (!isValidIP(IP.c_str())){
            error_msg_invalid("IP", IP.c_str());
            return;
        }
        //GW is optional if DHCP
        String GW = getValueFromParam(cmd.c_str(), "J");
        if (GW.length() == 0) GW = "0.0.0.0";
        if ( !isValidIP(GW.c_str())){
            error_msg_invalid("Gateway IP", GW.c_str());
            return;
        }
        //MK is optional if DHCP
        String MK = getValueFromParam(cmd.c_str(), "K");
        if (MK.length() == 0) MK = "0.0.0.0";
        if (!isValidIP(MK.c_str())){
            error_msg_invalid("Mask", MK.c_str());
            return;
        }
        
       //save to preferences
       ESP_preferences.begin(NAMESPACE, false);
       ESP_preferences.putString(STA_SSID_ENTRY, SSID);
       ESP_preferences.putString(STA_PWD_ENTRY, password);
       ESP_preferences.putChar(STA_IP_MODE_ENTRY, IP_mode);
       ESP_preferences.putInt(STA_IP_ENTRY,IP_int_from_string(IP));
       ESP_preferences.putInt(STA_GW_ENTRY,IP_int_from_string(GW));
       ESP_preferences.putInt(STA_MK_ENTRY,IP_int_from_string(MK));
       ESP_preferences.end();
       print_msg("Saved, to apply type", "M588 S1 P1");
        
    } else {
        String v;
        String defV;
        int32_t vip;
        wifi_mode_t m = WiFi.getMode();
        print_msg("Mode Client", ((m == WIFI_STA) || (m == WIFI_AP_STA))?"Enabled":"Disabled");
        //Get STA Mac address
        WiFi.mode(WIFI_STA);
        v = WiFi.macAddress();
        WiFi.mode(m);
        print_msg("MAC", v.c_str());
        ESP_preferences.begin(NAMESPACE, true);
        //SSID
        defV = DEFAULT_STA_SSID;
        v = ESP_preferences.getString(STA_SSID_ENTRY, defV);
        print_msg("SSID", v.c_str());
        //Do not show password
        //IP mode
        IP_mode = ESP_preferences.getChar(STA_IP_MODE_ENTRY, DHCP_MODE);
        //IP
        defV = DEFAULT_STA_IP;
        vip = ESP_preferences.getInt(STA_IP_ENTRY, IP_int_from_string(defV));
        if (IP_mode == DHCP_MODE){
            v = "(DHCP)";
            v+=WiFi.localIP().toString();
        } else v = IP_string_from_int(vip);
        print_msg("IP", v.c_str());
        //GW
        defV = DEFAULT_STA_GW;
        vip = ESP_preferences.getInt(STA_GW_ENTRY, IP_int_from_string(defV));
        if (IP_mode == DHCP_MODE){
            v = "(DHCP)";
            v+=WiFi.gatewayIP().toString();
        } else v = IP_string_from_int(vip);
        print_msg("Gateway", v.c_str());
        //MK
        defV = DEFAULT_STA_MK;
        vip = ESP_preferences.getInt(STA_MK_ENTRY, IP_int_from_string(defV));
        if (IP_mode == DHCP_MODE){
            v = "(DHCP)";
            v+=WiFi.subnetMask().toString();
        } else v = IP_string_from_int(vip);
        print_msg("Mask", v.c_str());
        if((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)){
            if(WiFi.status() == WL_CONNECTED ) {
                print_msg("Status", "Connected");
                v= getSignal (WiFi.RSSI ());
                v+= '%';
                print_msg("AP Signal", v.c_str());
            } else print_msg("Status", "Not connected");
        }
        ESP_preferences.end();
    }
}

/**
 * M588: Set/Display WiFi mode
 */
void GcodeSuite::M588(){
    if (parser.seenval('S')){
        //Get WiFi mode
        int8_t v = parser.byteval('S', ESP_WIFI_OFF);
        ESP_preferences.begin(NAMESPACE, false);
        ESP_preferences.putChar(ESP_WIFI_MODE, v);
        ESP_preferences.end();
        if (parser.byteval('P', ESP_SAVE_ONLY) == 1){
            if (v == ESP_WIFI_OFF) StopWiFi();
            else if (v == ESP_WIFI_STA) StartSTA();
            else if (v == ESP_WIFI_AP) StartAP();
        } else {
            String s = "M588 S";
            s += v;
            s+= " P1";
            print_msg("Saved, to apply type", s.c_str());
        }

    } else {
         if (WiFi.getMode() == WIFI_OFF) print_msg("Current Mode", "WiFi off");
         else if (WiFi.getMode() == WIFI_STA) print_msg("Current Mode", "Client Station");
         else if (WiFi.getMode() == WIFI_AP) print_msg("Current Mode", "Access Point");
         else if (WiFi.getMode() == WIFI_AP_STA) print_msg("Current Mode", "Mixed");
         ESP_preferences.begin(NAMESPACE, true);
         int8_t wifiMode = ESP_preferences.getChar(ESP_WIFI_MODE, ESP_WIFI_OFF);
         ESP_preferences.end();
         if (wifiMode == ESP_WIFI_OFF) print_msg("Saved Mode", "WiFi off");
         else if (wifiMode == ESP_WIFI_STA) print_msg("Saved Mode", "Client Station");
         else if (wifiMode == ESP_WIFI_AP) print_msg("Saved Mode", "Access Point");
         else print_msg("Saved Mode", "Not defined");
    }
}

/**
 * M587: Configure access point parameters
 */
void GcodeSuite::M589(){
    String cmd = parser.string_arg;
    String error_title;
    if (cmd.length() > 0) {
        //Sanity check for each parameter
        String SSID = getStringFromParam(cmd.c_str(), "S");
        if (!isSSIDValid(SSID.c_str())) {
            error_msg_invalid("SSID", SSID.c_str());
            return;
        }
        String password = getStringFromParam(cmd.c_str(), "P");
        if (!isPasswordValid(password.c_str())) {
            error_msg_invalid("Password", password.c_str());
            return;
        }
        String IP = getValueFromParam(cmd.c_str(), "I");
        if ((IP.length() == 0) || !isValidIP(IP.c_str())){
            error_msg_invalid("IP", IP.c_str());
            return;
        }
       String channel = getValueFromParam(cmd.c_str(), "C");
       //channel is optional
        if (channel.length() == 0)channel = String(DEFAULT_AP_CHANNEL);
        if ((channel.toInt() > MAX_CHANNEL) || (channel.toInt() < MIN_CHANNEL)){
            error_msg_invalid("Channel", channel.c_str());
            return;
        }
       //save to preferences
       ESP_preferences.begin(NAMESPACE, false);
       ESP_preferences.putString(AP_SSID_ENTRY, SSID);
       ESP_preferences.putString(AP_PWD_ENTRY, password);
       ESP_preferences.putInt(AP_IP_ENTRY,IP_int_from_string(IP));
       ESP_preferences.putChar(AP_CHANNEL_ENTRY, channel.toInt());
       ESP_preferences.end();
       print_msg("Saved, to apply type", "M588 S2 P1");
        
    } else {
        String v;
        int32_t vip;
        String defV;
        wifi_mode_t m = WiFi.getMode();
        print_msg("Mode Access point", ((m == WIFI_AP) || (m == WIFI_AP_STA))?"Enabled":"Disabled");
        //Get STA Mac address
        WiFi.mode(WIFI_AP);
        v = WiFi.softAPmacAddress();
        WiFi.mode(m);
        print_msg("MAC", v.c_str());
        ESP_preferences.begin(NAMESPACE, true);
        //SSID
        defV = DEFAULT_AP_SSID;
        v = ESP_preferences.getString(AP_SSID_ENTRY, defV);
        print_msg("SSID", v.c_str());
        //Do not show password
        //IP
        defV = DEFAULT_AP_IP;
        vip = ESP_preferences.getInt(AP_IP_ENTRY, IP_int_from_string(defV));
        if (vip == 0){
            v = DEFAULT_AP_IP;
        } else v = IP_string_from_int(vip);
        if((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA))v=WiFi.softAPIP().toString();
        print_msg("IP", v.c_str());
        //Channel
        int8_t vi = ESP_preferences.getChar(AP_CHANNEL_ENTRY, DEFAULT_AP_CHANNEL);
        if (vi==0){
            vi = DEFAULT_AP_CHANNEL;
        }
        v= String(vi);
        print_msg("Channel", v.c_str());
        ESP_preferences.end();
    }
}     
#endif // WIFISUPPORT

#endif // ARDUINO_ARCH_ESP32
