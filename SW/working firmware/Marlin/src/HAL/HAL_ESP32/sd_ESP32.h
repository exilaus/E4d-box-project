#ifndef _ESP_SD_H_
#define _ESP_SD_H_
class ESP_SD{
    public:
    ESP_SD();
    ~ESP_SD();
    int8_t card_status();
    uint32_t card_total_space();
    uint32_t card_used_space();
    bool open(const char * path, bool readonly = true );
    void close();
    int16_t write(const uint8_t * data, uint16_t len);
    int16_t write(const uint8_t byte);
    uint16_t read(uint8_t * buf, uint16_t nbyte);
    int16_t read();
    uint32_t size();
    uint32_t available();
    bool exists(const char * path);
    bool dir_exists(const char * path);
    bool remove(const char * path);
    bool rmdir(const char * path);
    bool mkdir(const char * path);
    bool isopen();
    String makepath83(String longpath);
    String makeshortname(String longname, uint8_t index = 1);
    bool openDir(String path);
    bool readDir(char name[13], uint32_t * size, bool * isFile);
    bool * isFile;
    private:
    void * _sdfile;
    uint32_t _size;
    uint32_t _pos;
    bool _readonly;
    String get_path_part(String data, int index);
    
};
#endif
