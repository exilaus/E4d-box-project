#if defined(ARDUINO_ARCH_ESP32)

#include "../../inc/MarlinConfig.h"

#if ENABLED(EEPROM_SETTINGS)

#include "../persistent_store_api.h"
#include "EEPROM.h"

#define EEPROM_SIZE 4096

namespace HAL {
namespace PersistentStore {

bool access_start() {
  return EEPROM.begin(EEPROM_SIZE);
}

bool access_finish() {
  EEPROM.end();
  return true;
}

bool write_data(int &pos, const uint8_t *value, uint16_t size, uint16_t *crc) {
  
  for (uint16_t i = 0; i < size; i++) {
      EEPROM.write(pos, value[i]);
      crc16(crc, &value[i], 1);
      pos++;
  }
  return false;
}

bool read_data(int &pos, uint8_t* value, uint16_t size, uint16_t *crc, const bool writing/*=true*/) {
  for (uint16_t i = 0; i < size; i++) {
    uint8_t c = EEPROM.read(pos);
    if (writing) value[i] = c;
    crc16(crc, &c, 1);
    pos++;
  }
}

} // PersistentStore
} // HAL

#endif // EEPROM_SETTINGS
#endif // ARDUINO_ARCH_ESP32
