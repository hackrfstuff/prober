#include "esctool/Silabs.h"

using namespace esctool;

static const SilabsMcu BB10x { 512,   8192,  0x0000, 0x1C00, 0x1A00, 0x1FFF };
static const SilabsMcu BB21x { 512,   8192,  0x0000, 0x1C00, 0x1A00, 0x3FFF }; // app 8KB, boot @1C00
static const SilabsMcu BB31x { 512,  24576,  0x0000, 0x5C00, 0x1A00, 0x5FFF };
static const SilabsMcu BB41x { 512,  32768,  0x0000, 0x7C00, 0x1A00, 0x7FFF };
static const SilabsMcu BB51x { 2048, 63488,  0x0000, 0xF000, 0x3000, 0xF7FF }; // page 2KB; erase idx units=512B

const SilabsMcu* esctool::silabs_from_sig(uint16_t sig){
  switch(sig){
    case 0xE8B1: return &BB10x;
    case 0xE8B2: return &BB21x;
    case 0xE8B3: return &BB31x;
    case 0xE8B4: return &BB41x;
    case 0xE8B5: return &BB51x;
    default: return nullptr;
  }
}

uint16_t esctool::silabs_eeprom_for(uint16_t sig){
  const auto* m = silabs_from_sig(sig);
  return m? m->eeprom_offset : 0x1A00;
}
