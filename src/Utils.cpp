#include "esctool/Utils.h"
#include <sstream>

using namespace esctool;

std::string esctool::hxd(const std::vector<uint8_t>& v) {
  std::ostringstream oss; bool first=true;
  for (auto b : v) { if (!first) oss << ' '; first=false; char buf[4]; snprintf(buf,4,"%02X", b); oss<<buf; }
  return oss.str();
}

uint16_t esctool::crc16_xmodem(const uint8_t* data, size_t len) {
  uint16_t crc = 0x0000;
  for (size_t i=0;i<len;++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j=0;j<8;++j) crc = (crc & 0x8000) ? (uint16_t)((crc<<1) ^ 0x1021) : (uint16_t)(crc<<1);
  }
  return crc;
}

uint32_t esctool::crc32(const uint8_t* data, size_t len) {
  static bool inited=false; static uint32_t table[256];
  if (!inited) {
    for (uint32_t i=0;i<256;++i){
      uint32_t c=i; for(int j=0;j<8;++j) c = (c & 1) ? (0xEDB88320u ^ (c>>1)) : (c>>1); table[i]=c;
    }
    inited=true;
  }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i=0;i<len;++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}
