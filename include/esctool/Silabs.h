#pragma once
#include <cstdint>
#include <optional>

namespace esctool {
struct SilabsMcu {
  uint16_t page_size;
  uint32_t flash_size;
  uint16_t firmware_start;
  uint16_t bootloader_address;
  uint16_t eeprom_offset;
  uint16_t lockbyte_address;
};

const SilabsMcu* silabs_from_sig(uint16_t sig);
uint16_t silabs_eeprom_for(uint16_t sig);
}
