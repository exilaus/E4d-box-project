#ifndef _ESP_WS_H_
#define _ESP_WS_H_

#include "Print.h"

class ESP_WS: public Print{
    public:
    ESP_WS();
    ~ESP_WS();
    size_t write(uint8_t c);
    size_t write(const uint8_t *buffer, size_t size);

    inline size_t write(const char * s)
    {
        return write((uint8_t*) s, strlen(s));
    }
    inline size_t write(unsigned long n)
    {
        return write((uint8_t) n);
    }
    inline size_t write(long n)
    {
        return write((uint8_t) n);
    }
    inline size_t write(unsigned int n)
    {
        return write((uint8_t) n);
    }
    inline size_t write(int n)
    {
        return write((uint8_t) n);
    }
    long baudRate();
    void begin(long speed);
    void end();
    int available();
    int peek(void);
    int read(void);
    void flush(void);
    void handle_flush();
    operator bool() const;
    bool attachWS(void * web_socket);
    bool detachWS();
    private:
    uint32_t _lastflush;
    void * _web_socket;
    uint8_t _buffer[1200];
    uint16_t _bufferSize;
};


extern ESP_WS Serial2Socket;

#endif
