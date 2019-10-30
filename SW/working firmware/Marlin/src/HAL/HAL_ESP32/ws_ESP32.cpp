#ifdef ARDUINO_ARCH_ESP32
#include "../../inc/MarlinConfigPre.h"

#include <ESPAsyncWebServer.h>
#include "ws_ESP32.h"
#include <WiFi.h>
extern long id_connection;
ESP_WS Serial2Socket;


ESP_WS::ESP_WS(){
    _web_socket = NULL;
    _bufferSize=0;
}
ESP_WS::~ESP_WS(){
    if (_web_socket) detachWS();
    _bufferSize=0;
}
void ESP_WS::begin(long speed){
    //TODO
    
}

void ESP_WS::end(){
    //TODO
    _bufferSize=0;
}

long ESP_WS::baudRate(){
 return 0;
}

bool ESP_WS::attachWS(void * web_socket){
    if (web_socket) {
        _web_socket = web_socket;
        _bufferSize=0;
        return true;
    }
    return false;
}

bool ESP_WS::detachWS(){
     _web_socket = NULL;
}
int ESP_WS::available(){
    //TODO
    return 0;
}

ESP_WS::operator bool() const
{
    return true;
}

size_t ESP_WS::write(uint8_t c)
{
    //TODO
    if(!_web_socket) return 0;
    write(&c,1);
    return 1;
}

size_t ESP_WS::write(const uint8_t *buffer, size_t size)
{
     if((buffer == NULL) ||(!_web_socket)) {
            return 0;
        }
    if (_bufferSize==0)_lastflush = millis();
    //TODO add timer to force to flush in case of no end 
    for (int i = 0; i < size;i++){
        _buffer[_bufferSize] = buffer[i];
        _bufferSize++;
        handle_flush();
    }
    return size;
}

int ESP_WS::peek(void){
    //TODO but not used
    return 0;
}

int ESP_WS::read(void){
    //TODO
    return 0;
}

void ESP_WS::handle_flush() {
    if ((_bufferSize>1200) || ((millis()- _lastflush) > 500)) {
            flush();
        }
}
void ESP_WS::flush(void){
    if (_bufferSize > 0){
        ((AsyncWebSocket *)_web_socket)->binaryAll(_buffer,_bufferSize);
        _bufferSize = 0;
    }
}

#endif
